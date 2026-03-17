#include "include/mm.h"
#include "include/list.h"
#include "include/atomic_mm.h"
#include "include/memory.h"
#include "include/string.h"
#include "include/screen.h"

// =============================================================================
// VIRTUAL MEMORY AREAS (VMA) IMPLEMENTATION
// =============================================================================
// Linux-style VMA management for process memory layout
// Provides memory regions with different permissions and backing stores
// =============================================================================

// Memory protection flags (from mman.h)
#ifndef PROT_READ
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define PROT_NONE       0x0
#endif

// Memory mapping flags
#ifndef MAP_SHARED
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_GROWSDOWN   0x0100
#endif

// Current memory descriptor (placeholder)
static mm_struct_t *current_mm = NULL;

// Note: Semaphore stubs (down_read, up_read, etc.) are defined as macros in mm.h
// Note: struct rb_node, rb_root, and rb_color macros are defined in mm.h

// File operations (simplified for now)
struct file {
    void *private_data;
    unsigned long f_pos;
    unsigned long f_flags;
};

// VMA caches
static kmem_cache_t *vm_area_cache;
static kmem_cache_t *mm_cache;

// Global VMA management state
static struct {
    bool initialized;
    unsigned long total_vmas;
    unsigned long total_mms;
} vma_state = { .initialized = false };

// =============================================================================
// RED-BLACK TREE OPERATIONS (SIMPLIFIED)
// =============================================================================

// Simple BST operations for now (can be upgraded to full RB-tree later)
static void rb_insert_vma(struct rb_root *root, vm_area_struct_t *vma)
{
    struct rb_node **new = &(root->rb_node);
    struct rb_node *parent = NULL;
    
    while (*new) {
        vm_area_struct_t *this = container_of(*new, vm_area_struct_t, vm_rb);
        parent = *new;
        
        if (vma->vm_start < this->vm_start) {
            new = &((*new)->rb_left);
        } else {
            new = &((*new)->rb_right);
        }
    }
    
    vma->vm_rb.rb_parent = parent;
    vma->vm_rb.rb_left = NULL;
    vma->vm_rb.rb_right = NULL;
    vma->vm_rb.rb_color = RB_RED;
    *new = &vma->vm_rb;
}

static void rb_erase_vma(struct rb_root *root, vm_area_struct_t *vma)
{
    // Simplified removal - full RB-tree operations would be more complex
    if (!vma->vm_rb.rb_parent) {
        root->rb_node = NULL;
    } else {
        // Just clear the reference for now
        if (vma->vm_rb.rb_parent->rb_left == &vma->vm_rb) {
            vma->vm_rb.rb_parent->rb_left = NULL;
        } else {
            vma->vm_rb.rb_parent->rb_right = NULL;
        }
    }
}

// =============================================================================
// VMA OPERATIONS
// =============================================================================

// Default VMA operations
static void vma_open_default(vm_area_struct_t *vma)
{
    // Default: nothing to do
}

static void vma_close_default(vm_area_struct_t *vma)
{
    // Default: nothing to do
}

static int vma_fault_default(vm_area_struct_t *vma, struct vm_fault *vmf)
{
    return VM_FAULT_SIGBUS; // Default: invalid access
}

static const struct vm_operations_struct default_vm_ops = {
    .open = vma_open_default,
    .close = vma_close_default,
    .fault = vma_fault_default,
};

// Anonymous VMA fault handler
static int vma_fault_anonymous(vm_area_struct_t *vma, struct vm_fault *vmf)
{
    // Allocate zero page for anonymous memory
    page_t *page = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (!page) {
        return VM_FAULT_OOM;
    }
    
    vmf->page = page;
    return VM_FAULT_MINOR;
}

static const struct vm_operations_struct anonymous_vm_ops = {
    .open = vma_open_default,
    .close = vma_close_default,
    .fault = vma_fault_anonymous,
};

// Create new VMA
static vm_area_struct_t *vma_alloc(void)
{
    vm_area_struct_t *vma = kmem_cache_alloc(vm_area_cache, GFP_KERNEL);
    if (vma) {
        memset(vma, 0, sizeof(*vma));
        INIT_LIST_HEAD(&vma->vm_list);
        vma_state.total_vmas++;
    }
    return vma;
}

// Free VMA
static void vma_free(vm_area_struct_t *vma)
{
    if (vma) {
        if (vma->vm_ops && vma->vm_ops->close) {
            vma->vm_ops->close(vma);
        }
        kmem_cache_free(vm_area_cache, vma);
        vma_state.total_vmas--;
    }
}

// Insert VMA into mm_struct
static void vma_link(mm_struct_t *mm, vm_area_struct_t *vma)
{
    down_write(&mm->mmap_sem);
    
    // Add to linear list
    list_add_tail(&vma->vm_list, &mm->mmap->vm_list);
    
    // Add to red-black tree
    rb_insert_vma(&mm->mm_rb, vma);
    
    // Update statistics
    mm->total_vm += (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    
    if (vma->vm_flags & VM_EXEC) {
        mm->exec_vm += (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    }
    if (vma->vm_flags & VM_SHARED) {
        mm->shared_vm += (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    }
    if (vma->vm_flags & VM_LOCKED) {
        mm->locked_vm += (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    }
    if (vma->vm_flags & VM_GROWSDOWN) {
        mm->stack_vm += (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    }
    
    up_write(&mm->mmap_sem);
}

// Remove VMA from mm_struct
static void vma_unlink(mm_struct_t *mm, vm_area_struct_t *vma)
{
    down_write(&mm->mmap_sem);
    
    // Remove from linear list
    list_del(&vma->vm_list);
    
    // Remove from red-black tree
    rb_erase_vma(&mm->mm_rb, vma);
    
    // Update statistics
    mm->total_vm -= (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    
    if (vma->vm_flags & VM_EXEC) {
        mm->exec_vm -= (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    }
    if (vma->vm_flags & VM_SHARED) {
        mm->shared_vm -= (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    }
    if (vma->vm_flags & VM_LOCKED) {
        mm->locked_vm -= (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    }
    if (vma->vm_flags & VM_GROWSDOWN) {
        mm->stack_vm -= (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    }
    
    up_write(&mm->mmap_sem);
}

// =============================================================================
// MM_STRUCT OPERATIONS
// =============================================================================

// Allocate new mm_struct
mm_struct_t *mm_alloc(void)
{
    mm_struct_t *mm = kmem_cache_alloc(mm_cache, GFP_KERNEL);
    if (!mm) {
        return NULL;
    }
    
    // Initialize mm_struct
    memset(mm, 0, sizeof(*mm));
    
    // Initialize lists and locks
    INIT_LIST_HEAD(&mm->mmap->vm_list);
    mm->mm_rb = RB_ROOT;
    init_rwsem(&mm->mmap_sem);
    spin_lock_init(&mm->page_table_lock);
    
    // Initialize reference counts
    atomic_set(&mm->mm_users, 1);
    atomic_set(&mm->mm_count, 1);
    
    // Allocate page directory
    mm->pgd = (pgd_t *)pmm_alloc_frame();
    if (!mm->pgd) {
        kmem_cache_free(mm_cache, mm);
        return NULL;
    }
    
    // Clear page directory
    memset((void *)mm->pgd, 0, PAGE_SIZE);
    
    vma_state.total_mms++;
    
    print("[VMA] Allocated new mm_struct at 0x"); print_hex((uint32)mm);
    print(" with PGD at 0x"); print_hex((uint32)mm->pgd); print("\n");
    
    return mm;
}

// Free mm_struct
void mm_free(mm_struct_t *mm)
{
    if (!mm) {
        return;
    }
    
    // Decrement user count
    if (!atomic_dec_and_test(&mm->mm_users)) {
        return; // Still has users
    }
    
    print("[VMA] Freeing mm_struct at 0x"); print_hex((uint32)mm); print("\n");
    
    // Free all VMAs
    vm_area_struct_t *vma, *next;
    list_for_each_entry_safe(vma, next, &mm->mmap->vm_list, vm_list) {
        vma_unlink(mm, vma);
        vma_free(vma);
    }
    
    // Free page directory
    if (mm->pgd) {
        pmm_free_frame((uint32)mm->pgd);
    }
    
    // Decrement structure count and free if zero
    if (atomic_dec_and_test(&mm->mm_count)) {
        kmem_cache_free(mm_cache, mm);
        vma_state.total_mms--;
    }
}

// =============================================================================
// VMA LOOKUP AND MANAGEMENT
// =============================================================================

// Find VMA containing address
vm_area_struct_t *find_vma(mm_struct_t *mm, unsigned long addr)
{
    vm_area_struct_t *vma = NULL;
    
    if (!mm) {
        return NULL;
    }
    
    down_read(&mm->mmap_sem);
    
    // Simple linear search for now (could use RB-tree for efficiency)
    list_for_each_entry(vma, &mm->mmap->vm_list, vm_list) {
        if (addr >= vma->vm_start && addr < vma->vm_end) {
            up_read(&mm->mmap_sem);
            return vma;
        }
    }
    
    up_read(&mm->mmap_sem);
    return NULL;
}

// Find VMA that starts at or before addr
vm_area_struct_t *find_vma_prev(mm_struct_t *mm, unsigned long addr,
                                vm_area_struct_t **pprev)
{
    vm_area_struct_t *vma = NULL, *prev = NULL;
    
    if (!mm) {
        return NULL;
    }
    
    down_read(&mm->mmap_sem);
    
    list_for_each_entry(vma, &mm->mmap->vm_list, vm_list) {
        if (addr < vma->vm_end) {
            break;
        }
        prev = vma;
    }
    
    up_read(&mm->mmap_sem);
    
    if (pprev) {
        *pprev = prev;
    }
    
    if (vma && addr >= vma->vm_start) {
        return vma;
    }
    
    return NULL;
}

// Check if address range is free
static bool is_area_free(mm_struct_t *mm, unsigned long start, unsigned long end)
{
    vm_area_struct_t *vma;
    
    list_for_each_entry(vma, &mm->mmap->vm_list, vm_list) {
        if (!(end <= vma->vm_start || start >= vma->vm_end)) {
            return false; // Overlap found
        }
    }
    
    return true;
}

// Find free area of specified size
static unsigned long find_free_area(mm_struct_t *mm, unsigned long len,
                                   unsigned long start, unsigned long end)
{
    unsigned long addr = start;
    
    while (addr + len <= end) {
        if (is_area_free(mm, addr, addr + len)) {
            return addr;
        }
        addr += PAGE_SIZE;
    }
    
    return 0; // No free area found
}

// =============================================================================
// MMAP IMPLEMENTATION
// =============================================================================

// Main mmap implementation
unsigned long do_mmap(struct file *file, unsigned long addr, 
                      unsigned long len, unsigned long prot,
                      unsigned long flags, unsigned long offset)
{
    mm_struct_t *mm = current_mm; // Would be current->mm in real kernel
    vm_area_struct_t *vma;
    unsigned long vm_flags = 0;
    
    if (!mm) {
        return -1;
    }
    
    // Validate parameters
    if (len == 0) {
        return -1;
    }
    
    // Align to page boundaries
    len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    // Convert protection flags to VM flags
    if (prot & PROT_READ) vm_flags |= VM_READ;
    if (prot & PROT_WRITE) vm_flags |= VM_WRITE;
    if (prot & PROT_EXEC) vm_flags |= VM_EXEC;
    
    // Convert mapping flags
    if (flags & MAP_SHARED) vm_flags |= VM_SHARED;
    if (flags & MAP_GROWSDOWN) vm_flags |= VM_GROWSDOWN;
    
    // Find suitable address if not specified
    if (addr == 0) {
        addr = find_free_area(mm, len, mm->start_brk, mm->start_stack);
        if (addr == 0) {
            return -1; // No space available
        }
    } else {
        // Validate specified address
        if (!is_area_free(mm, addr, addr + len)) {
            return -1; // Address range not available
        }
    }
    
    // Create new VMA
    vma = vma_alloc();
    if (!vma) {
        return -1;
    }
    
    // Initialize VMA
    vma->vm_start = addr;
    vma->vm_end = addr + len;
    vma->vm_flags = vm_flags;
    vma->vm_mm = mm;
    vma->vm_file = file;
    vma->vm_pgoff = offset >> PAGE_SHIFT;
    
    // Set page protection
    if (vm_flags & VM_WRITE) {
        vma->vm_page_prot = PAGE_SHARED;
    } else {
        vma->vm_page_prot = PAGE_READONLY;
    }
    
    // Set operations
    if (file) {
        // File-backed mapping (simplified)
        vma->vm_ops = &default_vm_ops;
    } else {
        // Anonymous mapping
        vma->vm_ops = &anonymous_vm_ops;
    }
    
    // Link VMA into mm_struct
    vma_link(mm, vma);
    
    print("[VMA] Mapped region 0x"); print_hex(addr);
    print("-0x"); print_hex(addr + len);
    print(" flags=0x"); print_hex(vm_flags); print("\n");
    
    return addr;
}

// Unmap memory region
int do_munmap(mm_struct_t *mm, unsigned long start, size_t len)
{
    vm_area_struct_t *vma, *next;
    unsigned long end = start + len;
    
    if (!mm) {
        return -1;
    }
    
    // Align to page boundaries
    start &= ~(PAGE_SIZE - 1);
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    print("[VMA] Unmapping region 0x"); print_hex(start);
    print("-0x"); print_hex(end); print("\n");
    
    down_write(&mm->mmap_sem);
    
    // Find all VMAs in the range and remove them
    list_for_each_entry_safe(vma, next, &mm->mmap->vm_list, vm_list) {
        if (vma->vm_start >= end) {
            break; // Past our range
        }
        
        if (vma->vm_end <= start) {
            continue; // Before our range
        }
        
        // This VMA overlaps with the range to unmap
        // For simplicity, remove the entire VMA
        // A full implementation would handle partial unmapping
        
        // Unmap pages (simplified - would need proper TLB invalidation)
        for (unsigned long addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
            // Would call vmm_unmap_page here
        }
        
        // Remove VMA
        list_del(&vma->vm_list);
        rb_erase_vma(&mm->mm_rb, vma);
        vma_free(vma);
    }
    
    up_write(&mm->mmap_sem);
    
    return 0;
}

// =============================================================================
// PROCESS MEMORY LAYOUT HELPERS
// =============================================================================

// Setup initial process memory layout
int setup_process_memory(mm_struct_t *mm, unsigned long code_start,
                        unsigned long code_end, unsigned long data_end,
                        unsigned long brk_start)
{
    if (!mm) {
        return -1;
    }
    
    // Set memory layout parameters
    mm->start_code = code_start;
    mm->end_code = code_end;
    mm->start_data = code_end;
    mm->end_data = data_end;
    mm->start_brk = brk_start;
    mm->brk = brk_start;
    mm->start_stack = USER_STACK_TOP - PAGE_SIZE; // Stack grows down from user space top
    
    // Create code segment VMA
    unsigned long code_addr = do_mmap(NULL, code_start, code_end - code_start,
                                     PROT_READ | PROT_EXEC, MAP_PRIVATE, 0);
    if (code_addr != code_start) {
        print("[VMA] Failed to map code segment\n");
        return -1;
    }
    
    // Create data segment VMA
    unsigned long data_addr = do_mmap(NULL, code_end, data_end - code_end,
                                     PROT_READ | PROT_WRITE, MAP_PRIVATE, 0);
    if (data_addr != code_end) {
        print("[VMA] Failed to map data segment\n");
        return -1;
    }
    
    // Create stack VMA
    unsigned long stack_addr = do_mmap(NULL, mm->start_stack, PAGE_SIZE,
                                      PROT_READ | PROT_WRITE, 
                                      MAP_PRIVATE | MAP_GROWSDOWN, 0);
    if (stack_addr != mm->start_stack) {
        print("[VMA] Failed to map stack\n");
        return -1;
    }
    
    print("[VMA] Process memory layout created:\n");
    print("  Code: 0x"); print_hex(code_start); 
    print("-0x"); print_hex(code_end); print("\n");
    print("  Data: 0x"); print_hex(code_end);
    print("-0x"); print_hex(data_end); print("\n");
    print("  Heap: 0x"); print_hex(brk_start); print("\n");
    print("  Stack: 0x"); print_hex(mm->start_stack); print("\n");
    
    return 0;
}

// =============================================================================
// INITIALIZATION
// =============================================================================

// Initialize VMA subsystem
int vma_init(void)
{
    print("[VMA] Initializing Virtual Memory Areas subsystem...\n");
    
    if (vma_state.initialized) {
        return 0;
    }
    
    // Create VMA cache
    vm_area_cache = kmem_cache_create("vm_area_struct", 
                                     sizeof(vm_area_struct_t),
                                     0, SLAB_HWCACHE_ALIGN, NULL);
    if (!vm_area_cache) {
        print("[VMA] Failed to create VMA cache\n");
        return -1;
    }
    
    // Create mm_struct cache
    mm_cache = kmem_cache_create("mm_struct",
                                sizeof(mm_struct_t),
                                0, SLAB_HWCACHE_ALIGN, NULL);
    if (!mm_cache) {
        print("[VMA] Failed to create mm_struct cache\n");
        return -1;
    }
    
    vma_state.initialized = true;
    vma_state.total_vmas = 0;
    vma_state.total_mms = 0;
    
    print("[VMA] VMA subsystem initialized\n");

    return 0;
}