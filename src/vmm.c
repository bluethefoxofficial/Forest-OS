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

// Temporarily map a physical page to a virtual address for access
static void* vmm_temp_map_page(uint32 phys_addr) {
    if (!vmm_state.paging_enabled) {
        // If paging not enabled yet, access directly
        return (void*)phys_addr;
    }
    
    // Use a simple round-robin allocation for temporary mapping slots
    uint32 slot = vmm_state.temp_map_next % VMM_TEMP_MAP_PAGES;
    vmm_state.temp_map_next++;
    
    uint32 temp_vaddr = VMM_TEMP_MAP_BASE + (slot * MEMORY_PAGE_SIZE);
    
    // Map the physical page to the temporary virtual address
    // We need to access the page directory directly since this is used by get_page_entry
    uint32 page_num = temp_vaddr / MEMORY_PAGE_SIZE;
    uint32 pd_index = page_num / 1024;
    uint32 pt_index = page_num % 1024;
    
    page_entry_t* pde = &(*vmm_state.current_directory)[pd_index];
    
    // Ensure the page table exists for the temporary mapping area
    if (!pde->present) {
        // This should not happen if we properly set up the temp mapping area
        return (void*)phys_addr; // Fall back to direct access
    }
    
    // Access the page table and set up the mapping
    page_table_t* pt = (page_table_t*)(pde->frame << MEMORY_PAGE_SHIFT);
    page_entry_t* pte = &(*pt)[pt_index];
    
    pte->frame = phys_addr >> MEMORY_PAGE_SHIFT;
    pte->present = 1;
    pte->writable = 1;
    pte->user = 0;
    
    // Invalidate TLB for this address
    __asm__ __volatile__("invlpg (%0)" :: "r"(temp_vaddr) : "memory");
    
    return (void*)temp_vaddr;
}

// Unmap a temporarily mapped page
static void vmm_temp_unmap_page(void* vaddr) {
    if (!vmm_state.paging_enabled) {
        return; // Nothing to do if paging not enabled
    }
    
    uint32 temp_vaddr = (uint32)vaddr;
    
    // Verify this is in the temp mapping range
    if (temp_vaddr < VMM_TEMP_MAP_BASE || 
        temp_vaddr >= VMM_TEMP_MAP_BASE + VMM_TEMP_MAP_SIZE) {
        return; // Not a temp mapping
    }
    
    // Clear the page table entry
    uint32 page_num = temp_vaddr / MEMORY_PAGE_SIZE;
    uint32 pd_index = page_num / 1024;
    uint32 pt_index = page_num % 1024;
    
    page_entry_t* pde = &(*vmm_state.current_directory)[pd_index];
    if (!pde->present) {
        return;
    }
    
    page_table_t* pt = (page_table_t*)(pde->frame << MEMORY_PAGE_SHIFT);
    page_entry_t* pte = &(*pt)[pt_index];
    
    pte->present = 0;
    
    // Invalidate TLB
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
            kernel_panic("VMM: Failed to allocate frame for new page table!");
            return NULL; // Should panic, but for safety return NULL
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
memory_result_t vmm_map_page(page_directory_t* dir, uint32 vaddr, uint32 paddr, uint32 flags) {
    print("[VMM_DBG] vmm_map_page: vaddr=0x"); print_hex(vaddr); print(", paddr=0x"); print_hex(paddr); print(", flags=0x"); print_hex(flags); print("\n");

    if (!dir) {
        return MEMORY_ERROR_NULL_PTR;
    }

    if (!vmm_is_addr_valid(vaddr) || !vmm_is_addr_valid(paddr)) {
        return MEMORY_ERROR_INVALID_ADDR;
    }

    // Ensure addresses are page-aligned
    if ((vaddr & MEMORY_PAGE_MASK) != 0 || (paddr & MEMORY_PAGE_MASK) != 0) {
        print("[VMM_DBG] vmm_map_page: Address not page-aligned. Returning INVALID_ADDR.\n");
        return MEMORY_ERROR_INVALID_ADDR;
    }

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
    cpu_set_cr3((uint32)vmm_state.current_directory);

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
        cpu_set_cr3((uint32)dir);
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
    page_directory_t* new_dir = (page_directory_t*)dir_phys;
    memset(new_dir, 0, MEMORY_PAGE_SIZE);
    memcpy(new_dir, vmm_state.kernel_directory, MEMORY_PAGE_SIZE);
    return new_dir;
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

    if (!vmm_is_addr_valid(start) || !vmm_is_addr_valid(end)) {
        return MEMORY_ERROR_INVALID_ADDR;
    }
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
