#include "include/memory.h"
#include "include/screen.h"
#include "include/system.h" // For cpu_get_cr0, cpu_set_cr0, etc.
#include "include/panic.h"  // For kernel_panic
#include "include/string.h" // For memset
#include "include/debuglog.h"

#define VMM_DEFAULT_IDENTITY_LIMIT_BYTES (64 * 1024 * 1024)   // Map first 64MB identity for firmware tables
#define KERNEL_HIGHER_HALF_BASE   0xC0000000

// Temporary mapping area for page table access
#define VMM_TEMP_MAP_BASE         0x10000000  // 256MB, temporary mapping area
#define VMM_TEMP_MAP_SIZE         0x400000    // 4MB for temporary mappings
#define VMM_TEMP_MAP_PAGES        (VMM_TEMP_MAP_SIZE / MEMORY_PAGE_SIZE)  // 1024 pages

#define VMM_DEBUG_LOG 0

extern char kernel_start;
extern char kernel_end;

static inline uint32 align_up(uint32 value, uint32 align) {
    return (value + align - 1) & ~(align - 1);
}

static inline bool vmm_is_addr_valid(uint32 addr) {
    return addr <= MEMORY_MAX_ADDR;
}

static inline void vmm_pretouch_identity_page(uint32 addr) {
    if (addr < MEMORY_PRETOUCH_LIMIT_BYTES) {
        volatile uint8_t* ptr = (volatile uint8_t*)addr;
        (void)*ptr;
    }
}

#if VMM_DEBUG_LOG
static inline void vmm_log_text(const char* text) {
    if (debuglog_is_ready()) {
        debuglog_write(text);
    }
}

static inline void vmm_log_hex(uint32 value) {
    if (debuglog_is_ready()) {
        debuglog_write_hex(value);
    }
}

static inline void vmm_log_dec(uint32 value) {
    if (debuglog_is_ready()) {
        debuglog_write_dec(value);
    }
}

#define print(text) do { vmm_log_text(text); } while (0)
#define print_hex(value) do { vmm_log_hex(value); } while (0)
#define print_dec(value) do { vmm_log_dec(value); } while (0)
#else
#define print(text) do {} while (0)
#define print_hex(value) do {} while (0)
#define print_dec(value) do {} while (0)
#endif

// VMM internal state
static struct {
    bool initialized;
    page_directory_t* kernel_directory; // Physical address of the kernel page directory
    page_directory_t* current_directory; // Physical address of the current active page directory
    uint32 temp_map_next; // Next available temporary mapping slot
    bool paging_enabled;  // Track if paging is enabled
} vmm_state = {0};

// =============================================================================
// TEMPORARY MAPPING FOR PAGE TABLE ACCESS
// =============================================================================

// Temporarily map a physical page to a virtual address for access.
//
// INVARIANT: The page tables for the VMM_TEMP_MAP_BASE area are allocated
// *before* paging is enabled (in vmm_init) and are identity-mapped, meaning
// their physical frame address == their virtual address.  It is therefore safe
// to dereference (pde->frame << PAGE_SHIFT) as a virtual pointer here and in
// vmm_temp_unmap_page — but ONLY for this temp-map region's page tables.
// DO NOT copy this pattern elsewhere; physical-address-as-virtual-pointer will
// fault for any frame above the identity-mapped window.
static void* vmm_temp_map_page(uint32 phys_addr) {
    if (!vmm_state.paging_enabled) {
        // Paging not yet on — physical == virtual, access directly.
        return (void*)phys_addr;
    }

    // Round-robin slot in the temporary mapping window.
    uint32 slot = vmm_state.temp_map_next % VMM_TEMP_MAP_PAGES;
    vmm_state.temp_map_next++;

    uint32 temp_vaddr = VMM_TEMP_MAP_BASE + (slot * MEMORY_PAGE_SIZE);

    uint32 page_num = temp_vaddr / MEMORY_PAGE_SIZE;
    uint32 pd_index = page_num / 1024;
    uint32 pt_index = page_num % 1024;

    page_entry_t* pde = &(*vmm_state.current_directory)[pd_index];

    if (!pde->present) {
        // The temp-map PDE must be set up during vmm_init.  If it isn't,
        // fall back to direct physical access rather than crashing.
        return (void*)phys_addr;
    }

    // Safe: temp-map page tables are pre-allocated before paging is enabled,
    // so (pde->frame << PAGE_SHIFT) is their identity-mapped virtual address.
    uint32 pt_phys = pde->frame << MEMORY_PAGE_SHIFT;
    page_table_t* pt = (page_table_t*)pt_phys;  /* identity-mapped — safe */
    page_entry_t* pte = &(*pt)[pt_index];

    pte->frame = phys_addr >> MEMORY_PAGE_SHIFT;
    pte->present = 1;
    pte->writable = 1;
    pte->user = 0;

    __asm__ __volatile__("invlpg (%0)" :: "r"(temp_vaddr) : "memory");

    return (void*)temp_vaddr;
}

// Unmap a temporarily mapped page (clears its PTE and flushes TLB).
// See vmm_temp_map_page for the identity-mapped invariant on page table access.
static void vmm_temp_unmap_page(void* vaddr) {
    if (!vmm_state.paging_enabled) {
        return;
    }

    uint32 temp_vaddr = (uint32)vaddr;

    if (temp_vaddr < VMM_TEMP_MAP_BASE ||
        temp_vaddr >= VMM_TEMP_MAP_BASE + VMM_TEMP_MAP_SIZE) {
        return;
        }

        uint32 page_num = temp_vaddr / MEMORY_PAGE_SIZE;
    uint32 pd_index = page_num / 1024;
    uint32 pt_index = page_num % 1024;

    page_entry_t* pde = &(*vmm_state.current_directory)[pd_index];
    if (!pde->present) {
        return;
    }

    /* Safe: temp-map page tables are identity-mapped (pre-paging allocation). */
    uint32 pt_phys = pde->frame << MEMORY_PAGE_SHIFT;
    page_table_t* pt = (page_table_t*)pt_phys;  /* identity-mapped — safe */
    page_entry_t* pte = &(*pt)[pt_index];

    pte->present = 0;

    __asm__ __volatile__("invlpg (%0)" :: "r"(temp_vaddr) : "memory");
}

// Helper function to get a page table entry for a given virtual address
// If make is true, and the page table doesn't exist, it allocates one.
static page_entry_t* get_page_entry(uint32 vaddr, bool make, page_directory_t* dir) {
    print("[VMM_DBG] get_page_entry: vaddr=0x"); print_hex(vaddr); print(", make="); print_dec(make); print("\n");

    if (!dir) {
        return NULL;
    }

    uint32 page_num = vaddr / MEMORY_PAGE_SIZE; // Convert to page number
    uint32 pd_index = page_num / 1024; // Page Directory Index
    uint32 pt_index = page_num % 1024; // Page Table Index

    if (pd_index >= 1024 || pt_index >= 1024) {
        return NULL;
    }

    print("[VMM_DBG] pd_index="); print_dec(pd_index); print(", pt_index="); print_dec(pt_index); print("\n");

    // Check if the page directory entry exists
    page_entry_t* pde = &(*dir)[pd_index];

    print("[VMM_DBG] PDE (phys_addr) for pd_index "); print_dec(pd_index); print(": 0x"); print_hex((uint32)pde); print(", present="); print_dec(pde->present); print("\n");

    if (!pde->present) { // Page table not present
        if (!make) {
            print("[VMM_DBG] PDE not present and make is false. Returning NULL.\n");
            return NULL; // Don't create if not requested
        }

        print("[VMM_DBG] PDE not present, allocating new page table...\n");
        // Allocate a new page table
        uint32 pt_phys_addr = pmm_alloc_frame();
        if (pt_phys_addr == 0) {
            print("[VMM] CRITICAL: Failed to allocate frame for new page table!\n");
            // Try to report available memory
            uint32 free_frames = (uint32)pmm_get_free_frames();
            print("[VMM] Free frames available: ");
            print_dec(free_frames);
            print("\n");
            return NULL; // Out of physical memory
        }
        print("[VMM_DBG] New page table physical address: 0x"); print_hex(pt_phys_addr); print("\n");

        // Clear the new page table using temporary mapping
        void* temp_pt = vmm_temp_map_page(pt_phys_addr);
        memset(temp_pt, 0, MEMORY_PAGE_SIZE);
        vmm_temp_unmap_page(temp_pt);
        print("[VMM_DBG] New page table cleared at physical 0x"); print_hex(pt_phys_addr); print("\n");

        // Set up the page directory entry
        pde->frame = pt_phys_addr >> MEMORY_PAGE_SHIFT;
        pde->present = 1;
        pde->writable = 1; // Default to writable
        pde->user = 0;     // Default to kernel access
        pde->pwt = 0;
        pde->pcd = 0;
        print("[VMM_DBG] PDE updated. pde->frame=0x"); print_hex(pde->frame << MEMORY_PAGE_SHIFT); print(", present=1\n");
    }

    // Now, the page table should exist (either pre-existing or newly created)
    // Access the page table using temporary mapping to avoid physical address access
    uint32 pt_phys_addr = pde->frame << MEMORY_PAGE_SHIFT;
    void* temp_pt = vmm_temp_map_page(pt_phys_addr);
    page_table_t* pt = (page_table_t*)temp_pt;
    print("[VMM_DBG] Page table mapped to temporary address: 0x"); print_hex((uint32)temp_pt); print("\n");

    // Note: We don't unmap here because the caller needs to access the returned pointer
    // The temporary mapping will be reused in a round-robin fashion
    return &(*pt)[pt_index];
}

// Map a virtual address to a physical address
// NOTE: For identity mapping (vaddr == paddr), we allow high virtual addresses
// like kernel higher-half (0xC0000000+)
memory_result_t vmm_map_page(page_directory_t* dir, uint32 vaddr, uint32 paddr, uint32 flags) {
    print("[VMM_DBG] vmm_map_page: vaddr=0x"); print_hex(vaddr); print(", paddr=0x"); print_hex(paddr); print(", flags=0x"); print_hex(flags); print("\n");

    if (!dir) {
        return MEMORY_ERROR_NULL_PTR;
    }

    // Only validate physical address - virtual can be any 32-bit value
    // This allows identity mapping of kernel higher-half
    if (!vmm_is_addr_valid(paddr)) {
        return MEMORY_ERROR_INVALID_ADDR;
    }

    // Ensure addresses are page-aligned
    if ((vaddr & MEMORY_PAGE_MASK) != 0 || (paddr & MEMORY_PAGE_MASK) != 0) {
        print("[VMM_DBG] vmm_map_page: Address not page-aligned. Returning INVALID_ADDR.\n");
        return MEMORY_ERROR_INVALID_ADDR;
    }

    // Ensure the page directory entry allows user-mode access when requested.
    uint32 page_num = vaddr / MEMORY_PAGE_SIZE;
    uint32 pd_index = page_num / 1024;
    if (pd_index >= 1024) {
        return MEMORY_ERROR_INVALID_ADDR;
    }
    page_entry_t* pde = &(*dir)[pd_index];

    page_entry_t* page = get_page_entry(vaddr, true, dir);
    if (page == NULL) {
        print("[VMM_DBG] vmm_map_page: get_page_entry returned NULL. Out of memory for page table. Returning OUT_OF_MEMORY.\n");
        return MEMORY_ERROR_OUT_OF_MEMORY;
    }

    if (page->present) {
        print("[VMM_DBG] vmm_map_page: Page already mapped. Returning ALREADY_MAPPED.\n");
        return MEMORY_ERROR_ALREADY_MAPPED; // Page already mapped
    }

    page->frame = paddr >> MEMORY_PAGE_SHIFT;
    page->present = (flags & PAGE_PRESENT) ? 1 : 0;
    page->writable = (flags & PAGE_WRITABLE) ? 1 : 0;
    page->user = (flags & PAGE_USER) ? 1 : 0;
    if (flags & PAGE_USER) {
        pde->user = 1; // Without this, ring 3 cannot access the PTE even if page->user is set.
    }
    // Copy other flags like accessed, dirty, global, etc.
    page->accessed = (flags & PAGE_ACCESSED) ? 1 : 0;
    page->dirty = (flags & PAGE_DIRTY) ? 1 : 0;
    print("[VMM_DBG] vmm_map_page: PTE updated. page->frame=0x"); print_hex(page->frame << MEMORY_PAGE_SHIFT); print(", present="); print_dec(page->present); print("\n");

    // Invalidate TLB for this virtual address if paging is enabled
    // If paging is enabled, we need to invalidate the TLB entry.
    // asm volatile("invlpg (%0)" ::"r" (vaddr) : "memory"); // This is for later, when paging is actually active.

    return MEMORY_OK;
}

// Unmap a virtual address
memory_result_t vmm_unmap_page(page_directory_t* dir, uint32 vaddr) {
    if (!dir) {
        return MEMORY_ERROR_NULL_PTR;
    }

    if (!vmm_is_addr_valid(vaddr)) {
        return MEMORY_ERROR_INVALID_ADDR;
    }

    if ((vaddr & MEMORY_PAGE_MASK) != 0) {
        return MEMORY_ERROR_INVALID_ADDR;
    }

    page_entry_t* page = get_page_entry(vaddr, false, dir);
    if (page == NULL || !page->present) {
        return MEMORY_ERROR_NOT_MAPPED; // Page not mapped or page table not present
    }

    page->present = 0; // Mark as not present
    // Clear other flags if necessary, but hardware usually ignores if not present

    // Invalidate TLB
    // asm volatile("invlpg (%0)" ::"r" (vaddr) : "memory"); // This is for later

    // TODO: if page table becomes empty, free its frame

    return MEMORY_OK;
}

// Get physical address for a virtual address
uint32 vmm_get_physical_addr(page_directory_t* dir, uint32 vaddr) {
    if (!dir) {
        return 0;
    }

    if (!vmm_is_addr_valid(vaddr)) {
        return 0;
    }

    if ((vaddr & MEMORY_PAGE_MASK) != 0) {
        return 0; // Not page-aligned
    }

    page_entry_t* page = get_page_entry(vaddr, false, dir);
    if (page == NULL || !page->present) {
        return 0; // Not mapped
    }

    return (page->frame << MEMORY_PAGE_SHIFT) | (vaddr & MEMORY_PAGE_MASK);
}

// Set up the initial kernel page directory and enable paging
memory_result_t vmm_init(void) {
    print("[VMM] Initializing Virtual Memory Manager (new)...\n");

    // Allocate a frame for the kernel page directory
    uint32 kernel_dir_phys = pmm_alloc_frame();
    if (kernel_dir_phys == 0) {
        kernel_panic("VMM: Failed to allocate frame for kernel page directory!");
        return MEMORY_ERROR_OUT_OF_MEMORY;
    }

    // Point vmm_state.kernel_directory to the physical address
    vmm_state.kernel_directory = (page_directory_t*)kernel_dir_phys;
    // Clear the new page directory
    memset((void*)vmm_state.kernel_directory, 0, MEMORY_PAGE_SIZE);

    vmm_state.current_directory = vmm_state.kernel_directory;

    // Identity map a reasonable range of low memory (default 64MB)
    // This covers the kernel, PMM bitmap, and early data structures.
    // Large page tables will be accessed via temporary mapping.
    uint32 identity_limit_kb = memory_get_usable_kb();
    if (identity_limit_kb == 0) {
        identity_limit_kb = VMM_DEFAULT_IDENTITY_LIMIT_BYTES / 1024;
    }

    if (identity_limit_kb < MEMORY_BOOTSTRAP_MIN_IDENTITY_KB) {
        identity_limit_kb = MEMORY_BOOTSTRAP_MIN_IDENTITY_KB;
    }
    if (identity_limit_kb > MEMORY_BOOTSTRAP_MAX_IDENTITY_KB) {
        identity_limit_kb = MEMORY_BOOTSTRAP_MAX_IDENTITY_KB;
    }

    uint32 identity_limit = identity_limit_kb * 1024;
    // Ensure identity limit covers all possible physical frame allocations
    // For now, identity map enough memory to handle a 512MB system
    if (identity_limit < 0x04000000) {
        identity_limit = 0x04000000; // always map at least first 64MB
    }
    if (identity_limit > MEMORY_MAX_ADDR) {
        identity_limit = MEMORY_MAX_ADDR;
    }
    identity_limit = (identity_limit + MEMORY_PAGE_SIZE - 1) & ~(MEMORY_PAGE_SIZE - 1);

    print("[VMM] Identity mapping first ");
    print_hex(identity_limit);
    print(" bytes...\n");
    for (uint32 addr = 0; addr < identity_limit; addr += MEMORY_PAGE_SIZE) {
        memory_result_t res = vmm_map_page(vmm_state.kernel_directory, addr, addr, PAGE_PRESENT | PAGE_WRITABLE);
        if (res == MEMORY_ERROR_ALREADY_MAPPED) {
            continue;
        }
        if (res != MEMORY_OK) {
            kernel_panic("VMM: Failed to identity map low memory!");
            return res;
        }

        vmm_pretouch_identity_page(addr);
    }

    // Explicitly identity map VGA text buffer for direct access
    print("[VMM] Identity mapping VGA text buffer 0xB8000...\n");
    memory_result_t res_vga_id = vmm_map_page(vmm_state.kernel_directory, 0xB8000, 0xB8000, PAGE_PRESENT | PAGE_WRITABLE);
    if (res_vga_id != MEMORY_OK && res_vga_id != MEMORY_ERROR_ALREADY_MAPPED) {
        kernel_panic("VMM: Failed to identity map VGA text buffer!");
        return res_vga_id;
    }

    // Map the actual kernel image into the higher half starting at 0xC0000000.
    uint32 kernel_phys_start = (uint32)&kernel_start;
    uint32 kernel_phys_end = align_up((uint32)&kernel_end, MEMORY_PAGE_SIZE);
    uint32 kernel_size = kernel_phys_end - kernel_phys_start;
    print("[VMM] Mapping kernel higher-half. phys_start=0x");
    print_hex(kernel_phys_start);
    print(", phys_end=0x");
    print_hex(kernel_phys_end);
    print("\n");
    for (uint32 offset = 0; offset < kernel_size; offset += MEMORY_PAGE_SIZE) {
        uint32 virt = KERNEL_HIGHER_HALF_BASE + offset;
        uint32 phys = kernel_phys_start + offset;
        memory_result_t map_res = vmm_map_page(
            vmm_state.kernel_directory,
            virt,
            phys,
            PAGE_PRESENT | PAGE_WRITABLE);
        if (map_res != MEMORY_OK && map_res != MEMORY_ERROR_ALREADY_MAPPED) {
            kernel_panic("VMM: Failed to map kernel into higher half!");
            return map_res;
        }
    }

    // Explicitly map VGA text buffer to its higher-half address (e.g., 0xC00B8000)
    print("[VMM] Mapping VGA text buffer to higher-half 0xC00B8000...\n");
    memory_result_t res_vga_hh = vmm_map_page(vmm_state.kernel_directory, 0xC00B8000, 0xB8000, PAGE_PRESENT | PAGE_WRITABLE);
    if (res_vga_hh != MEMORY_OK && res_vga_hh != MEMORY_ERROR_ALREADY_MAPPED) {
        kernel_panic("VMM: Failed to higher-half map VGA text buffer!");
        return res_vga_hh;
    }


    // Set up temporary mapping area for page table access
    // We need to do this manually to avoid circular dependency with get_page_entry
    print("[VMM] Setting up temporary mapping area...\n");

    // Calculate page directory and page table indices for temp mapping area
    uint32 temp_start_page = VMM_TEMP_MAP_BASE / MEMORY_PAGE_SIZE;
    uint32 temp_pd_index = temp_start_page / 1024;
    uint32 temp_pages = VMM_TEMP_MAP_SIZE / MEMORY_PAGE_SIZE;

    // Allocate page tables for the temporary mapping area
    for (uint32 i = 0; i < (temp_pages + 1023) / 1024; i++) {
        uint32 pd_index = temp_pd_index + i;

        if (pd_index >= 1024) {
            kernel_panic("VMM: Temporary mapping area too large!");
            return MEMORY_ERROR_INVALID_ADDR;
        }

        page_entry_t* pde = &(*vmm_state.kernel_directory)[pd_index];

        if (!pde->present) {
            uint32 pt_phys_addr = pmm_alloc_frame();
            if (pt_phys_addr == 0) {
                kernel_panic("VMM: Failed to allocate page table for temporary mapping area!");
                return MEMORY_ERROR_OUT_OF_MEMORY;
            }

            // Clear the page table directly (before paging is enabled)
            memset((void*)pt_phys_addr, 0, MEMORY_PAGE_SIZE);

            pde->frame = pt_phys_addr >> MEMORY_PAGE_SHIFT;
            pde->present = 1;
            pde->writable = 1;
            pde->user = 0;
        }
    }

    vmm_state.initialized = true;
    vmm_state.temp_map_next = 0;
    print("[VMM] VMM Initialized. Kernel directory at 0x");
    print_hex((uint32)vmm_state.kernel_directory);
    print("\n");

    return MEMORY_OK;
}

// Enable paging for the current_directory
void vmm_enable_paging(void) {
    print("[VMM] Attempting to enable paging.\n");
    if (!vmm_state.initialized) {
        kernel_panic("VMM: Attempt to enable paging before VMM initialization!");
    }

    // Load the physical address of the current page directory into CR3
    cpu_set_cr3((uintptr_t)vmm_state.current_directory);

    // Enable PAE (if needed, but for 32-bit non-PAE, it's not)
    // For now, assuming 32-bit non-PAE protected mode without explicit PAE.
    // If PAE were needed, it would be:
    // uint32 cr4 = cpu_get_cr4();
    // cr4 |= (1 << 5); // Set PAE bit
    // cpu_set_cr4(cr4);

    // Enable paging (PG bit in CR0) and Protected Mode (PE bit in CR0)
    uint32 cr0 = cpu_get_cr0();
    cr0 |= (1 << 31); // Set PG bit (Paging Enable)
    cr0 |= (1 << 0);  // Set PE bit (Protected Mode Enable) - ensure it's set
    cpu_set_cr0(cr0);

    // Mark paging as enabled for the VMM state
    vmm_state.paging_enabled = true;

    print("[VMM] Paging enabled!\n");
}

// Switch the current page directory
void vmm_switch_page_directory(page_directory_t* dir) {
    if (!dir) {
        kernel_panic("VMM: Attempt to switch to NULL page directory!");
    }
    vmm_state.current_directory = dir;
    // If paging is already enabled, update CR3
    if ((cpu_get_cr0() & (1 << 31)) != 0) {
        cpu_set_cr3((uintptr_t)dir);
    }
}

// Get the current page directory
page_directory_t* vmm_get_current_page_directory(void) {
    return vmm_state.current_directory;
}

page_directory_t* vmm_create_page_directory(void) {
    if (!vmm_state.initialized) {
        return NULL;
    }
    uint32 dir_phys = pmm_alloc_frame();
    if (!dir_phys) {
        return NULL;
    }

    // The new directory's physical frame must be accessible in the CURRENT
    // address space before we can write to it.  Identity-map it first.
    vmm_map_page(vmm_state.current_directory,
                 dir_phys, dir_phys,
                 PAGE_PRESENT | PAGE_WRITABLE);

    page_directory_t* new_dir = (page_directory_t*)dir_phys;
    memset(new_dir, 0, MEMORY_PAGE_SIZE);

    // Copy the ENTIRE current directory into the new one.
    // This is a shallow copy — both directories share the same physical page
    // tables for every PDE that was present at copy time.  New kernel heap
    // allocations that go into an existing page table are automatically visible
    // in both directories (they share the PT frame).  New allocations that
    // require a *new* page table (new PDE) will NOT be visible until we
    // explicitly sync that PDE — see task_switch for the runtime sync path.
    page_directory_t* src_dir = vmm_state.current_directory;
    memcpy(new_dir, src_dir, MEMORY_PAGE_SIZE);

    // CRITICAL — ensure the new directory maps its OWN physical frame so that
    // kernel writes through this CR3 (during ELF segment loading) don't fault.
    vmm_identity_map_range(new_dir,
                           dir_phys,
                           dir_phys + MEMORY_PAGE_SIZE,
                           PAGE_WRITABLE);

    // CRITICAL — identity-map every page table referenced by the source
    // directory.  Page tables live at arbitrary physical addresses (often above
    // the early identity window).  Without identity-mapping them, get_page_entry
    // would fault when it tries to dereference (pde->frame << PAGE_SHIFT) as a
    // virtual address after a CR3 switch.
    //
    // Note: this does not apply to the temp-map area page tables because those
    // are accessed via the identity-mapped invariant (see vmm_temp_map_page).
    page_entry_t* src_pde = (page_entry_t*)src_dir;
    for (int i = 0; i < 1024; i++) {
        if (src_pde[i].present) {
            uint32 pt_phys = src_pde[i].frame << MEMORY_PAGE_SHIFT;
            memory_result_t res = vmm_map_page(new_dir, pt_phys, pt_phys,
                                               PAGE_PRESENT | PAGE_WRITABLE);
            if (res != MEMORY_OK && res != MEMORY_ERROR_ALREADY_MAPPED) {
                // Cannot map page table — clean up and fail.
                pmm_free_frame(dir_phys);
                return NULL;
            }
        }
    }

    // RUNTIME PDE SYNC NOTE:
    // Because this is a shallow copy, any kernel heap allocation made AFTER
    // this point that causes a NEW page table to be created (new PDE) will
    // only appear in the kernel's directory, not here.  The task_switch path
    // must sync all kernel PDEs into the active task's directory before
    // calling task_switch_asm.  See task_sync_kernel_pdes() below.

    return new_dir;
}

// Synchronise all kernel-space PDEs (those used for the kernel heap and other
// kernel-only mappings) from the current kernel directory into a task's
// directory.  Call this in task_switch() before task_switch_asm() to ensure
// any kernel heap pages allocated after the task was created are visible.
//
// "Kernel space" is defined as any PDE whose source entry is present AND whose
// corresponding virtual range (i >= kernel_pde_start) falls above the user
// space boundary.  We use a conservative range: PDE 0 through the identity
// limit are already synced via the initial memcpy; we resync ALL PDEs to be
// safe (cost is one pass of 1024 comparisons — negligible).
void vmm_sync_kernel_pdes(page_directory_t* task_dir) {
    if (!task_dir || !vmm_state.current_directory) {
        return;
    }

    page_entry_t* kernel_pde = (page_entry_t*)vmm_state.current_directory;
    page_entry_t* task_pde   = (page_entry_t*)task_dir;

    // Sync ALL kernel PDEs to ensure the new task's page directory has
    // complete visibility of the entire kernel address space. This includes
    // the kernel heap (around PDE 43-46), temp-map area (PDE 64), and any
    // other kernel memory regions that might have been allocated since
    // task_create.
    for (int i = 1; i < 1024; i++) {
        if (kernel_pde[i].present) {
            // Ensure the task's PD has this kernel PDE
            if (!task_pde[i].present) {
                task_pde[i] = kernel_pde[i];
            }
            // Also ensure PDE permissions and frame numbers are consistent with kernel
            if (task_pde[i].present && task_pde[i].frame != kernel_pde[i].frame) {
                task_pde[i] = kernel_pde[i];
            }
            
            // Always ensure the page table is identity-mapped so get_page_entry works
            uint32 pt_phys = kernel_pde[i].frame << MEMORY_PAGE_SHIFT;
            vmm_map_page(task_dir, pt_phys, pt_phys,
                         PAGE_PRESENT | PAGE_WRITABLE);
        }
    }
    
    // DEBUG: Verify we're syncing the right PDE for the kernel stack
    uint32 kernel_stack_pde_idx = 0x2d;  // From debug log: [TASK] new_pd[0x2d] (PDE 45)
    if (kernel_pde[kernel_stack_pde_idx].present) {
        debuglog(DEBUG_INFO, "[VMM_SYNC] Kernel stack PDE %u: present=%d, frame=0x%x\n",
                 kernel_stack_pde_idx,
                 task_pde[kernel_stack_pde_idx].present,
                 task_pde[kernel_stack_pde_idx].frame);
        if (task_pde[kernel_stack_pde_idx].present && 
            task_pde[kernel_stack_pde_idx].frame != kernel_pde[kernel_stack_pde_idx].frame) {
            debuglog(DEBUG_WARN, "[VMM_SYNC] Kernel stack PDE mismatch! Kernel: 0x%x, Task: 0x%x\n",
                     kernel_pde[kernel_stack_pde_idx].frame,
                     task_pde[kernel_stack_pde_idx].frame);
        }
        
        // Explicitly check and sync the PTE for the kernel stack
        // Kernel stack is at ~0xb700000 which is PDE 45, PTE ~777
        uint32 kernel_stack_va = 0xb7093b8;  // From debug log: [TASK] Switching: next_kstack=0xb7093b8
        page_entry_t* kernel_stack_pte = get_page_entry(kernel_stack_va, false, vmm_state.current_directory);
        page_entry_t* task_stack_pte = get_page_entry(kernel_stack_va, false, task_dir);
        
        if (kernel_stack_pte && kernel_stack_pte->present) {
            if (!task_stack_pte || !task_stack_pte->present) {
                debuglog(DEBUG_WARN, "[VMM_SYNC] Kernel stack PTE not present in task's page directory! Syncing...\n");
                // Create the PTE if it doesn't exist
                task_stack_pte = get_page_entry(kernel_stack_va, true, task_dir);
                if (task_stack_pte) {
                    *task_stack_pte = *kernel_stack_pte;
                }
            } else if (task_stack_pte->frame != kernel_stack_pte->frame) {
                debuglog(DEBUG_WARN, "[VMM_SYNC] Kernel stack PTE mismatch! Kernel: 0x%x, Task: 0x%x\n",
                         kernel_stack_pte->frame,
                         task_stack_pte->frame);
                *task_stack_pte = *kernel_stack_pte;
            } else {
                debuglog(DEBUG_INFO, "[VMM_SYNC] Kernel stack PTE is present and matches (0x%x)\n",
                         task_stack_pte->frame);
            }
        } else {
            debuglog(DEBUG_ERROR, "[VMM_SYNC] Kernel stack PTE not found in kernel page directory! (VA=0x%x)\n",
                     kernel_stack_va);
        }
    }
}

void vmm_destroy_page_directory(page_directory_t* dir) {
    if (!dir || dir == vmm_state.kernel_directory) {
        return;
    }
    pmm_free_frame((uint32)dir);
}

bool vmm_is_mapped(page_directory_t* dir, uint32 vaddr) {
    if (!dir) {
        return false;
    }
    page_entry_t* page = get_page_entry(vaddr, false, dir);
    return page && page->present;
}

memory_result_t vmm_identity_map_range(page_directory_t* dir, uint32 start, uint32 end, uint32 flags) {
    if (!dir || start > end) {
        return MEMORY_ERROR_INVALID_ADDR;
    }

    // Skip vmm_is_addr_valid check for identity mapping.
    // Identity mapping maps virtual = physical, so if physical memory is valid (below 4GB),
    // the virtual should be allowed too. This fixes crashes when mapping kernel
    // higher-half (0xC0000000+) in user page directories.

    uint32 aligned_start = start & ~MEMORY_PAGE_MASK;
    uint32 aligned_end = (end + MEMORY_PAGE_MASK) & ~MEMORY_PAGE_MASK;
    for (uint32 addr = aligned_start; addr < aligned_end; addr += MEMORY_PAGE_SIZE) {
        memory_result_t res = vmm_map_page(dir, addr, addr, flags | PAGE_PRESENT);
        if (res != MEMORY_OK && res != MEMORY_ERROR_ALREADY_MAPPED) {
            return res;
        }
    }
    return MEMORY_OK;
}
