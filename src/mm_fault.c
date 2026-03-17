// =============================================================================
// PAGE FAULT HANDLER - FOREST OS v3.0
// =============================================================================
// Linux-inspired page fault handling with demand paging support
// Implements on-demand allocation, VMA validation, and page table management
// Integrates with existing Forest OS interrupt system
// =============================================================================

#include "include/mm.h"
#include "include/atomic_mm.h"
#include "include/list.h"
#include "include/interrupt.h"
#include "include/system.h"
#include "include/memory.h"  // For x86 page table definitions
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

static int __handle_mm_fault(mm_struct_t *mm, vm_area_struct_t *vma, 
                             unsigned long address, unsigned int flags);
static int do_anonymous_page(mm_struct_t *mm, vm_area_struct_t *vma,
                            unsigned long address, pte_t *ptep, 
                            unsigned int flags);
static int do_linear_fault(mm_struct_t *mm, vm_area_struct_t *vma,
                          unsigned long address, pte_t *ptep, 
                          unsigned int flags);
static inline bool access_error(unsigned long error_code, vm_area_struct_t *vma);
static inline unsigned long get_unmapped_area(mm_struct_t *mm, 
                                              unsigned long len);

// External functions that need to be implemented by arch-specific code
extern mm_struct_t *get_current_mm(void);
extern pte_t *get_pte_from_address(mm_struct_t *mm, unsigned long address);
extern void set_pte_at(mm_struct_t *mm, unsigned long address,
                      pte_t *ptep, pte_t entry);
extern void flush_tlb_page(vm_area_struct_t *vma, unsigned long address);
extern void update_mmu_cache(vm_area_struct_t *vma, unsigned long address,
                            pte_t *ptep);
extern void page_fault_handler_minimal(uint32 fault_addr, uint32 error_code,
                                       struct interrupt_frame* frame);

// Page fault statistics structure
struct fault_stats {
    unsigned long total_faults;
    unsigned long minor_faults;
    unsigned long major_faults;
    unsigned long cow_faults;
    unsigned long oom_faults;
    unsigned long sigbus_faults;
};

// Global statistics for the enhanced page fault handler
static struct fault_stats enhanced_fault_stats = {0};

// =============================================================================
// PAGE FAULT ERROR CODE PARSING (x86 specific)
// =============================================================================

#define PF_PROT         0x1    // Protection violation (vs page not present)
#define PF_WRITE        0x2    // Write access (vs read access)  
#define PF_USER         0x4    // User mode access (vs kernel mode)
#define PF_RSVD         0x8    // Reserved bits violation
#define PF_INSTR        0x10   // Instruction fetch

// =============================================================================
// MAIN PAGE FAULT HANDLER
// =============================================================================

/**
 * mm_handle_page_fault - Main page fault handler entry point
 * @frame: CPU interrupt frame at time of fault
 * @address: Virtual address that caused the fault
 * @error_code: Page fault error code from CPU
 *
 * This is the main entry point for all page faults. It determines the cause
 * of the fault and dispatches to appropriate handlers for resolution.
 *
 * Returns: 0 on success, negative error code on failure
 */
int mm_handle_page_fault(struct interrupt_frame *frame, unsigned long address,
                        unsigned long error_code)
{
    mm_struct_t *mm;
    vm_area_struct_t *vma;
    unsigned int flags = 0;
    int ret = 0;
    
    // Get current process memory descriptor
    mm = get_current_mm();
    if (!mm) {
        return -1;  // Kernel fault - should be handled differently
    }
    
    // Take read lock on VMA semaphore
    down_read(&mm->mmap_sem);
    
    // Find VMA containing the fault address
    vma = find_vma(mm, address);
    if (!vma) {
        ret = VM_FAULT_SIGBUS;  // Address not mapped
        goto unlock_out;
    }
    
    // Check if address is within VMA bounds
    if (address < vma->vm_start) {
        // Check if VMA can grow down (stack)
        if (!(vma->vm_flags & VM_GROWSDOWN)) {
            ret = VM_FAULT_SIGBUS;
            goto unlock_out;
        }
        
        // Expand stack VMA downward (for stack growth)
        unsigned long new_start = address & ~(PAGE_SIZE - 1);
        unsigned long expand_size = vma->vm_start - new_start;
        
        // Limit stack growth to reasonable size (prevent stack overflow attacks)
        if (expand_size > (8 * 1024 * 1024)) { // 8MB limit
            ret = VM_FAULT_SIGBUS;
            goto unlock_out;
        }
        
        // Expand the VMA
        vma->vm_start = new_start;
        
        // Allocate and map the new pages
        unsigned long cur_va = new_start;
        while (cur_va < vma->vm_end) {
            pte_t *ptep = get_pte_from_address(mm, cur_va);
            if (!ptep) {
                ret = VM_FAULT_SIGBUS;
                goto unlock_out;
            }
            
            if (!pte_present(*ptep)) {
                page_t *page = alloc_page(GFP_USER);
                if (!page) {
                    ret = VM_FAULT_OOM;
                    goto unlock_out;
                }
                
                memset(page_address(page), 0, PAGE_SIZE);
                page->flags |= PG_UPTODATE;
                atomic_set(&page->refcount, 1);
                
                pte_t entry = pfn_pte(page_to_pfn(page), vma->vm_page_prot);
                entry = pte_mkwrite(entry);
                entry = pte_mkdirty(entry);
                
                spin_lock(&mm->page_table_lock);
                if (!pte_none(*ptep)) {
                    spin_unlock(&mm->page_table_lock);
                    free_page(page);
                } else {
                    set_pte_at(mm, cur_va, ptep, entry);
                    mm->total_vm++;
                    if (vma->vm_flags & VM_LOCKED) {
                        mm->locked_vm++;
                    }
                    spin_unlock(&mm->page_table_lock);
                    update_mmu_cache(vma, cur_va, ptep);
                }
            }
            
            cur_va += PAGE_SIZE;
        }
        
        return VM_FAULT_MINOR;
    }
    
    // Validate access permissions
    if (access_error(error_code, vma)) {
        ret = VM_FAULT_SIGBUS;
        goto unlock_out;
    }
    
    // Set fault flags based on error code
    if (error_code & PF_WRITE) {
        flags |= FAULT_FLAG_WRITE;
    }
    
    // Handle the memory management fault
    ret = __handle_mm_fault(mm, vma, address, flags);
    
unlock_out:
    up_read(&mm->mmap_sem);
    return ret;
}

// =============================================================================
// MEMORY MANAGEMENT FAULT HANDLER
// =============================================================================

/**
 * __handle_mm_fault - Handle memory management faults
 * @mm: Memory descriptor
 * @vma: VMA containing the fault
 * @address: Fault address
 * @flags: Fault flags
 *
 * This function handles the actual memory management aspects of page faults,
 * including page table walking and page allocation.
 */
static int __handle_mm_fault(mm_struct_t *mm, vm_area_struct_t *vma, 
                             unsigned long address, unsigned int flags)
{
    pte_t *ptep;
    pte_t entry;
    
    // Get page table entry for this address
    ptep = get_pte_from_address(mm, address);
    if (!ptep) {
        // Page table not allocated - this is a major fault
        return do_linear_fault(mm, vma, address, ptep, flags);
    }
    
    entry = *ptep;
    
    // Check if page is present
    if (!pte_present(entry)) {
        // Page not present - handle demand paging
        if (pte_none(entry)) {
            // Anonymous page or file-backed page
            if (vma->vm_ops && vma->vm_ops->fault) {
                // VMA has custom fault handler
                struct vm_fault vmf = {
                    .address = address,
                    .flags = flags,
                    .ptep = ptep,
                    .orig_pte = entry
                };
                return vma->vm_ops->fault(vma, &vmf);
            } else {
                // Anonymous memory
                return do_anonymous_page(mm, vma, address, ptep, flags);
            }
        } else {
            // Swapped out page - handle swap-in
            // TODO: Implement swap handling
            return VM_FAULT_MAJOR;
        }
    }
    
    // Page is present - check for write protection fault
    if ((flags & FAULT_FLAG_WRITE) && !pte_write(entry)) {
        // COW fault or write protection violation
        return do_wp_page(mm, vma, address, ptep, entry);
    }
    
    return VM_FAULT_MINOR;
}

// =============================================================================
// ANONYMOUS PAGE FAULT HANDLER
// =============================================================================

/**
 * do_anonymous_page - Handle anonymous page faults (demand paging)
 * @mm: Memory descriptor
 * @vma: VMA containing the fault
 * @address: Fault address
 * @ptep: Page table entry pointer
 * @flags: Fault flags
 *
 * This implements demand paging for anonymous memory. When a process
 * accesses unmapped anonymous memory, we allocate a page on-demand.
 */
static int do_anonymous_page(mm_struct_t *mm, vm_area_struct_t *vma,
                            unsigned long address, pte_t *ptep, 
                            unsigned int flags)
{
    page_t *page;
    pte_t entry;
    unsigned long page_addr;
    
    // Align address to page boundary
    page_addr = address & ~PAGE_MASK;
    
    // Allocate a new page
    page = alloc_page(GFP_USER);
    if (!page) {
        return VM_FAULT_OOM;
    }
    
    // Clear the page for security (zero-fill)
    void *page_virt = page_address(page);
    if (page_virt) {
        memset(page_virt, 0, PAGE_SIZE);
    }
    
    // Set page flags
    page->flags |= PG_UPTODATE;
    atomic_set(&page->refcount, 1);
    
    // Create page table entry
    entry = pfn_pte(page_to_pfn(page), vma->vm_page_prot);
    
    // Make page writable if VMA allows it and this is a write fault
    if ((vma->vm_flags & VM_WRITE) && (flags & FAULT_FLAG_WRITE)) {
        entry = pte_mkwrite(entry);
        entry = pte_mkdirty(entry);
    }
    
    // Set the PTE and update MMU
    spin_lock(&mm->page_table_lock);
    
    // Double-check that PTE is still empty
    if (!pte_none(*ptep)) {
        spin_unlock(&mm->page_table_lock);
        free_page(page);
        return VM_FAULT_MINOR;  // Someone else handled it
    }
    
    set_pte_at(mm, page_addr, ptep, entry);
    
    // Update statistics
    mm->total_vm++;
    if (vma->vm_flags & VM_LOCKED) {
        mm->locked_vm++;
    }
    
    spin_unlock(&mm->page_table_lock);
    
    // Update TLB and MMU cache
    update_mmu_cache(vma, page_addr, ptep);
    
    return VM_FAULT_MINOR;
}

// =============================================================================
// LINEAR FAULT HANDLER (Page table allocation)
// =============================================================================

/**
 * do_linear_fault - Handle faults requiring page table allocation
 * @mm: Memory descriptor  
 * @vma: VMA containing the fault
 * @address: Fault address
 * @ptep: Page table entry pointer (may be NULL)
 * @flags: Fault flags
 *
 * This handles cases where we need to allocate page table pages themselves.
 */
static int do_linear_fault(mm_struct_t *mm, vm_area_struct_t *vma,
                          unsigned long address, pte_t *ptep, 
                          unsigned int flags)
{
    // Page table not allocated - allocate it
    // For x86 32-bit, we need to allocate page tables
    unsigned long page_dir = (unsigned long)mm->pgd;
    unsigned long dir_index = (address >> 22) & 0x3FF;
    unsigned long table_index = (address >> 12) & 0x3FF;
    
    // Check if page directory entry exists and is present
    page_entry_t *page_dir_entry = &((page_directory_t*)page_dir)[dir_index];
    if (!page_dir_entry->present) {
        // Allocate new page table
        uint32_t table_frame = pmm_alloc_frame();
        if (!table_frame) {
            return VM_FAULT_OOM;
        }
        
        // Zero the page table
        void *table_vaddr = (void*)(0xFFC00000); // Temporary mapping
        page_directory_t* cur_dir = vmm_get_current_page_directory();
        vmm_unmap_page(cur_dir, (uint32_t)table_vaddr);
        vmm_map_page(cur_dir, (uint32_t)table_vaddr, table_frame, PAGE_PRESENT | PAGE_WRITABLE);
        memset(table_vaddr, 0, PAGE_SIZE);
        vmm_unmap_page(cur_dir, (uint32_t)table_vaddr);
        
        // Set page directory entry
        page_dir_entry->frame = table_frame >> 12;
        page_dir_entry->present = 1;
        page_dir_entry->writable = 1;
        page_dir_entry->user = 0; // Kernel-only access to page tables
    }
    
    // Now try to get PTE again
    ptep = get_pte_from_address(mm, address);
    if (!ptep) {
        return VM_FAULT_SIGBUS;
    }
    
    // If PTE is now available, handle the fault
    if (pte_none(*ptep)) {
        return do_anonymous_page(mm, vma, address, ptep, flags);
    } else if (!pte_present(*ptep)) {
        // Handle swapped out page
        return VM_FAULT_MAJOR;
    }
    
    return VM_FAULT_MINOR;
}

// =============================================================================
// ACCESS VALIDATION
// =============================================================================

/**
 * access_error - Check if fault violates VMA permissions
 * @error_code: Page fault error code
 * @vma: VMA being accessed
 *
 * Returns true if the access violates permissions, false otherwise.
 */
static inline bool access_error(unsigned long error_code, vm_area_struct_t *vma)
{
    // Write to non-writable VMA
    if ((error_code & PF_WRITE) && !(vma->vm_flags & VM_WRITE)) {
        return true;
    }
    
    // Execute from non-executable VMA  
    if ((error_code & PF_INSTR) && !(vma->vm_flags & VM_EXEC)) {
        return true;
    }
    
    // User access to kernel-only VMA
    if ((error_code & PF_USER) && !(vma->vm_flags & (VM_READ | VM_EXEC))) {
        return true;
    }
    
    // Protection violation (present page, but access not allowed)
    if (error_code & PF_PROT) {
        return true;
    }
    
    return false;
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

/**
 * get_unmapped_area - Find unmapped area in address space
 * @mm: Memory descriptor
 * @len: Length of area needed
 *
 * Returns virtual address of suitable unmapped area, or 0 on failure.
 */
static inline unsigned long get_unmapped_area(mm_struct_t *mm, 
                                              unsigned long len)
{
    // Simple linear search for unmapped area
    // TODO: Implement more sophisticated allocation strategy
    
    unsigned long addr = 0x40000000;  // Start at 1GB
    vm_area_struct_t *vma;
    
    while (addr < 0xC0000000) {  // End at 3GB (userspace limit)
        vma = find_vma(mm, addr);
        if (!vma || addr + len <= vma->vm_start) {
            return addr;
        }
        addr = vma->vm_end;
    }
    
    return 0;  // No suitable area found
}

// =============================================================================
// COPY-ON-WRITE INTEGRATION
// =============================================================================
// COW functions are now implemented in mm_cow.c
// The declarations remain in mm.h for cross-module access

// =============================================================================
// MEMORY DEBUGGING AND STATISTICS  
// =============================================================================

/**
 * print_fault_info - Print debugging information about page fault
 * @address: Fault address
 * @error_code: Page fault error code
 * @vma: VMA containing fault (may be NULL)
 */
void print_fault_info(unsigned long address, unsigned long error_code,
                     vm_area_struct_t *vma)
{
    // TODO: Implement fault debugging output
    // This would print useful information for debugging page faults
}

/**
 * get_fault_stats - Get page fault statistics
 */
struct fault_stats *get_fault_stats(void)
{
    return &enhanced_fault_stats;
}

// =============================================================================
// FOREST OS INTEGRATION - COMPATIBILITY LAYER
// =============================================================================

/**
 * enhanced_page_fault_handler - Forest OS compatible entry point
 * @vector: Interrupt vector number (should be EXCEPTION_PAGE_FAULT)
 * @ctx: Interrupt context containing frame and registers
 *
 * This function provides compatibility with Forest OS's existing interrupt
 * system while enabling the advanced Linux-inspired memory management.
 * It can be registered as the page fault handler in the interrupt system.
 *
 * Returns: IRQ_HANDLED if handled, IRQ_NONE otherwise
 */
irq_return_t enhanced_page_fault_handler(int vector, struct interrupt_context *ctx)
{
    unsigned long fault_addr = 0;
    unsigned long error_code;
    int result;
    (void)vector;  // Unused

    if (!ctx) {
        return IRQ_NONE;
    }

    // Get fault address from CR2 register
#if !ARCH_64BIT
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(fault_addr));
#endif

    // Get error code from interrupt frame
    error_code = ctx->error_code;

    // Update statistics
    enhanced_fault_stats.total_faults++;

    // Try the enhanced Linux-inspired handler first
    result = mm_handle_page_fault(&ctx->frame, fault_addr, error_code);

    switch (result) {
        case VM_FAULT_MINOR:
            enhanced_fault_stats.minor_faults++;
            return IRQ_HANDLED;

        case VM_FAULT_MAJOR:
            enhanced_fault_stats.major_faults++;
            return IRQ_HANDLED;

        case VM_FAULT_OOM:
            enhanced_fault_stats.oom_faults++;
            break;

        case VM_FAULT_SIGBUS:
        default:
            enhanced_fault_stats.sigbus_faults++;
            break;
    }

    // If our enhanced handler can't handle it, fall back to the original
    // This maintains compatibility with existing Forest OS behavior
    page_fault_handler_minimal(fault_addr, error_code, &ctx->frame);
    return IRQ_HANDLED;
}

/**
 * install_enhanced_page_fault_handler - Install the enhanced handler
 *
 * Call this function after the memory management system is initialized
 * to replace the default page fault handler with the enhanced version.
 */
void install_enhanced_page_fault_handler(void)
{
    interrupt_set_handler(EXCEPTION_PAGE_FAULT, (interrupt_handler_t)enhanced_page_fault_handler);
}

/**
 * get_enhanced_fault_stats - Get statistics for enhanced page fault handler
 */
struct fault_stats *get_enhanced_fault_stats(void)
{
    return &enhanced_fault_stats;
}
