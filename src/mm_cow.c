// =============================================================================
// COPY-ON-WRITE SUPPORT - FOREST OS v3.0
// =============================================================================
// Linux-inspired copy-on-write implementation for efficient memory sharing
// Enables memory sharing between processes with lazy copying on write access
// =============================================================================

#include "include/mm.h"
#include "include/memory.h"
#include "include/atomic_mm.h"
#include "include/list.h"
#include "include/interrupt.h"
#include "include/system.h"
#include "include/spinlock.h"
#include <stddef.h>
#include <stdbool.h>

/* Stub macros for missing functions - to be implemented in VMM */
#ifndef pte_same
#define pte_same(a, b)  ((a) == (b))
#endif

#ifndef set_pte_at
#define set_pte_at(mm, addr, ptep, entry)  do { *(ptep) = (entry); } while(0)
#endif

#ifndef flush_tlb_page
#define flush_tlb_page(vma, addr) do { \
    __asm__ volatile ("invlpg (%0)" :: "r" (addr) : "memory"); \
} while(0)
#endif

#ifndef get_pte_from_address
static inline pte_t *get_pte_from_address(mm_struct_t *mm, unsigned long addr) {
    (void)mm; (void)addr;
    return NULL;  /* Stub - needs VMM implementation */
}
#endif

/* Stub semaphore operations - to be replaced with real implementation */
#ifndef down_read
#define down_read(sem)  do { (void)(sem); } while(0)
#endif

#ifndef up_read
#define up_read(sem)    do { (void)(sem); } while(0)
#endif

/* Helper macro for VMA list traversal */
#define for_each_vma(mm, vma) \
    for ((vma) = (mm)->mmap; (vma) != NULL; (vma) = list_entry((vma)->vm_list.next, vm_area_struct_t, vm_list))

// =============================================================================
// COPY-ON-WRITE CONSTANTS AND FLAGS
// =============================================================================

// Special PTE flags for COW pages
#define _PAGE_COW           0x200  // Custom flag for COW pages  
#define _PAGE_SHARED_REF    0x400  // Page has multiple references

// COW statistics (struct cow_stats is defined in mm.h)
static struct cow_stats cow_stats = {0};

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

static page_t *cow_copy_page(page_t *old_page, vm_area_struct_t *vma, 
                            unsigned long address);
static int cow_unshare_page(mm_struct_t *mm, vm_area_struct_t *vma,
                           unsigned long address, pte_t *ptep);
static bool is_cow_page(pte_t pte);
static void cow_make_readonly(pte_t *ptep);
static void cow_make_writable(pte_t *ptep);

// =============================================================================
// MAIN COW FAULT HANDLER
// =============================================================================

/**
 * do_wp_page - Handle write protection faults (main COW entry point)
 * @mm: Memory descriptor
 * @vma: VMA containing the fault
 * @address: Fault address
 * @ptep: Page table entry pointer
 * @entry: Current page table entry
 *
 * This function handles write faults on read-only pages. It determines
 * whether this is a COW fault or a genuine protection violation.
 */
int do_wp_page(mm_struct_t *mm, vm_area_struct_t *vma,
               unsigned long address, pte_t *ptep, pte_t entry)
{
    page_t *old_page;
    unsigned long page_addr;
    
    if (!mm || !vma || !ptep) {
        return VM_FAULT_SIGBUS;
    }
    
    // Align address to page boundary
    page_addr = address & ~PAGE_MASK;
    
    // Check if VMA allows writing
    if (!(vma->vm_flags & VM_WRITE)) {
        // Genuine write protection violation
        return VM_FAULT_SIGBUS;
    }
    
    // Get the page from the PTE
    if (!pte_present(entry)) {
        return VM_FAULT_SIGBUS;
    }
    
    old_page = pte_page(entry);
    if (!old_page) {
        return VM_FAULT_SIGBUS;
    }
    
    // Update COW statistics
    cow_stats.cow_faults++;
    
    // Check if this is a COW page
    if (is_cow_page(entry)) {
        // Handle COW fault
        return do_cow_fault(mm, vma, address, ptep, entry);
    }
    
    // Check if page is shared (reference count > 1)
    if (atomic_read(&old_page->refcount) > 1) {
        // This is a shared page - need to copy
        return cow_unshare_page(mm, vma, page_addr, ptep);
    }
    
    // Page is not shared - just make it writable
    spinlock_acquire(&mm->page_table_lock);
    
    // Double-check the PTE hasn't changed
    if (!pte_same(*ptep, entry)) {
        spin_unlock(&mm->page_table_lock);
        return VM_FAULT_MINOR; // Someone else handled it
    }
    
    // Make the page writable
    entry = pte_mkwrite(entry);
    entry = pte_mkdirty(entry);
    set_pte_at(mm, page_addr, ptep, entry);
    
    spin_unlock(&mm->page_table_lock);
    
    // Update TLB
    flush_tlb_page(vma, page_addr);
    
    return VM_FAULT_MINOR;
}

// =============================================================================
// COW FAULT HANDLING
// =============================================================================

/**
 * do_cow_fault - Handle COW page faults
 * @mm: Memory descriptor
 * @vma: VMA containing the fault
 * @address: Fault address
 * @ptep: Page table entry pointer
 * @entry: Current page table entry
 *
 * This function handles the actual copy-on-write operation when a process
 * tries to write to a COW page.
 */
int do_cow_fault(mm_struct_t *mm, vm_area_struct_t *vma, 
                 unsigned long address, pte_t *ptep, pte_t entry)
{
    page_t *old_page, *new_page;
    pte_t new_entry;
    unsigned long page_addr;
    
    page_addr = address & ~PAGE_MASK;
    old_page = pte_page(entry);
    
    if (!old_page) {
        return VM_FAULT_SIGBUS;
    }
    
    // Allocate a new page for the copy
    new_page = alloc_page(GFP_USER);
    if (!new_page) {
        cow_stats.cow_failures++;
        return VM_FAULT_OOM;
    }
    
    // Copy the page content
    if (!cow_copy_page(old_page, vma, page_addr)) {
        free_page(new_page);
        cow_stats.cow_failures++;
        return VM_FAULT_OOM;
    }
    
    // Copy data from old page to new page
    void *old_kaddr = page_address(old_page);
    void *new_kaddr = page_address(new_page);
    
    if (old_kaddr && new_kaddr) {
        memcpy(new_kaddr, old_kaddr, PAGE_SIZE);
    } else {
        // Handle high memory or unmapped pages
        free_page(new_page);
        cow_stats.cow_failures++;
        return VM_FAULT_OOM;
    }
    
    // Set up the new page
    atomic_set(&new_page->refcount, 1);
    new_page->flags |= PG_UPTODATE;
    
    // Create new PTE for the copied page
    new_entry = pfn_pte(page_to_pfn(new_page), vma->vm_page_prot);
    new_entry = pte_mkwrite(new_entry);
    new_entry = pte_mkdirty(new_entry);
    new_entry = pte_mkyoung(new_entry);
    
    // Update page table
    spinlock_acquire(&mm->page_table_lock);
    
    // Double-check PTE hasn't changed
    if (!pte_same(*ptep, entry)) {
        spin_unlock(&mm->page_table_lock);
        free_page(new_page);
        return VM_FAULT_MINOR; // Someone else handled it
    }
    
    // Install new PTE
    set_pte_at(mm, page_addr, ptep, new_entry);
    
    // Decrease reference count on old page
    if (atomic_dec_and_test(&old_page->refcount)) {
        free_page(old_page);
    }
    
    spin_unlock(&mm->page_table_lock);
    
    // Update TLB and statistics
    flush_tlb_page(vma, page_addr);
    cow_stats.pages_copied++;
    
    return VM_FAULT_MINOR;
}

// =============================================================================
// PAGE SHARING FUNCTIONS
// =============================================================================

/**
 * cow_share_page - Set up page for copy-on-write sharing
 * @mm: Memory descriptor
 * @vma: VMA containing the page
 * @address: Page address
 * @page: Page to share
 *
 * This function sets up a page for COW sharing between processes.
 * The page is marked read-only and the COW flag is set.
 */
int cow_share_page(mm_struct_t *mm, vm_area_struct_t *vma,
                   unsigned long address, page_t *page)
{
    pte_t *ptep;
    pte_t entry;
    unsigned long page_addr;
    
    if (!mm || !vma || !page) {
        return -1;
    }
    
    page_addr = address & ~PAGE_MASK;
    ptep = get_pte_from_address(mm, page_addr);
    if (!ptep) {
        return -1;
    }
    
    // Increment page reference count
    atomic_inc(&page->refcount);
    
    // Create COW PTE
    entry = pfn_pte(page_to_pfn(page), vma->vm_page_prot);
    entry = pte_wrprotect(entry);  // Make read-only
    entry |= _PAGE_COW;            // Mark as COW
    
    // Install the PTE
    spinlock_acquire(&mm->page_table_lock);
    set_pte_at(mm, page_addr, ptep, entry);
    spin_unlock(&mm->page_table_lock);
    
    // Update statistics
    cow_stats.shared_pages++;
    
    // Flush TLB to ensure read-only protection is active
    flush_tlb_page(vma, page_addr);
    
    return 0;
}

/**
 * cow_unshare_page - Unshare a COW page
 * @mm: Memory descriptor
 * @vma: VMA containing the page
 * @address: Page address
 * @ptep: Page table entry pointer
 *
 * This function creates a private copy of a shared page.
 */
static int cow_unshare_page(mm_struct_t *mm, vm_area_struct_t *vma,
                           unsigned long address, pte_t *ptep)
{
    pte_t entry = *ptep;
    page_t *old_page, *new_page;
    pte_t new_entry;
    
    old_page = pte_page(entry);
    if (!old_page) {
        return VM_FAULT_SIGBUS;
    }
    
    // Allocate new page
    new_page = alloc_page(GFP_USER);
    if (!new_page) {
        return VM_FAULT_OOM;
    }
    
    // Copy page content
    void *old_kaddr = page_address(old_page);
    void *new_kaddr = page_address(new_page);
    
    if (old_kaddr && new_kaddr) {
        memcpy(new_kaddr, old_kaddr, PAGE_SIZE);
    }
    
    // Set up new page
    atomic_set(&new_page->refcount, 1);
    new_page->flags |= PG_UPTODATE;
    
    // Create writable PTE
    new_entry = pfn_pte(page_to_pfn(new_page), vma->vm_page_prot);
    if (vma->vm_flags & VM_WRITE) {
        new_entry = pte_mkwrite(new_entry);
        new_entry = pte_mkdirty(new_entry);
    }
    new_entry = pte_mkyoung(new_entry);
    
    // Update page table
    spinlock_acquire(&mm->page_table_lock);
    set_pte_at(mm, address, ptep, new_entry);
    spin_unlock(&mm->page_table_lock);
    
    // Release old page reference
    if (atomic_dec_and_test(&old_page->refcount)) {
        free_page(old_page);
        cow_stats.shared_pages--;
    }
    
    // Update statistics and flush TLB
    cow_stats.pages_copied++;
    flush_tlb_page(vma, address);
    
    return VM_FAULT_MINOR;
}

// =============================================================================
// COPY PROCESS MEMORY (for fork())
// =============================================================================

/**
 * cow_copy_mm - Copy memory descriptor for COW
 * @mm: Source memory descriptor
 *
 * This function creates a copy of a memory descriptor for a new process,
 * setting up COW sharing for all writable pages.
 */
mm_struct_t *cow_copy_mm(mm_struct_t *mm)
{
    mm_struct_t *new_mm;
    vm_area_struct_t *vma, *new_vma;
    pte_t *ptep, *new_ptep;
    page_t *page;
    
    if (!mm) {
        return NULL;
    }
    
    // Allocate new memory descriptor
    new_mm = mm_alloc();
    if (!new_mm) {
        return NULL;
    }
    
    // Copy VMA list
    down_read(&mm->mmap_sem);

    // Use mmap pointer directly - VMAs are linked via vm_list but mmap gives us the first one
    // For simplicity, we'll iterate using the mmap pointer chain
    // Note: This is a simplified traversal - full implementation would use list_for_each_entry
    vma = mm->mmap;
    while (vma) {
        vm_area_struct_t *next_vma = NULL;
        // Check if there's a next VMA in the list
        if (vma->vm_list.next && vma->vm_list.next != &mm->mmap->vm_list) {
            next_vma = list_entry(vma->vm_list.next, vm_area_struct_t, vm_list);
        }

        {  // Scope block for the original loop body
        // Create new VMA
        new_vma = kmalloc(sizeof(vm_area_struct_t));
        if (!new_vma) {
            up_read(&mm->mmap_sem);
            mm_free(new_mm);
            return NULL;
        }
        
        // Copy VMA content
        memcpy(new_vma, vma, sizeof(vm_area_struct_t));
        new_vma->vm_mm = new_mm;
        
        // Add to new mm's VMA list
        list_add_tail(&new_vma->vm_list, &new_mm->mmap->vm_list);
        
        // Set up COW for writable pages in this VMA
        if (vma->vm_flags & VM_WRITE) {
            for (unsigned long addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
                ptep = get_pte_from_address(mm, addr);
                if (ptep && pte_present(*ptep)) {
                    page = pte_page(*ptep);
                    if (page) {
                        // Set up COW sharing
                        cow_make_readonly(ptep);
                        cow_share_page(new_mm, new_vma, addr, page);
                    }
                }
            }
        }
        }  // End scope block

        // Move to next VMA
        vma = next_vma;
    }

    up_read(&mm->mmap_sem);
    return new_mm;
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

/**
 * cow_copy_page - Copy a page with proper COW handling
 * @old_page: Source page
 * @vma: VMA containing the page
 * @address: Virtual address of the page
 */
static page_t *cow_copy_page(page_t *old_page, vm_area_struct_t *vma, 
                            unsigned long address)
{
    page_t *new_page;
    void *old_kaddr, *new_kaddr;
    
    // Check for special zero page
    if (old_page->flags & PG_RESERVED) {
        cow_stats.zero_page_cows++;
        // For zero pages, just allocate and zero-fill
        new_page = alloc_page(GFP_USER | __GFP_ZERO);
        return new_page;
    }
    
    // Allocate new page
    new_page = alloc_page(GFP_USER);
    if (!new_page) {
        return NULL;
    }
    
    // Get kernel addresses for copying
    old_kaddr = page_address(old_page);
    new_kaddr = page_address(new_page);
    
    if (old_kaddr && new_kaddr) {
        memcpy(new_kaddr, old_kaddr, PAGE_SIZE);
        return new_page;
    }
    
    // Failed to map pages
    free_page(new_page);
    return NULL;
}

/**
 * is_cow_page - Check if PTE represents a COW page
 * @pte: Page table entry
 */
static bool is_cow_page(pte_t pte)
{
    return (pte & _PAGE_COW) != 0;
}

/**
 * cow_make_readonly - Mark page as read-only for COW
 * @ptep: Page table entry pointer
 */
static void cow_make_readonly(pte_t *ptep)
{
    pte_t pte = *ptep;
    pte = pte_wrprotect(pte);
    pte |= _PAGE_COW;
    *ptep = pte;
}

/**
 * cow_make_writable - Remove COW protection and make writable
 * @ptep: Page table entry pointer
 */
static void cow_make_writable(pte_t *ptep)
{
    pte_t pte = *ptep;
    pte = pte_mkwrite(pte);
    pte &= ~_PAGE_COW;
    *ptep = pte;
}

// =============================================================================
// DEBUGGING AND STATISTICS
// =============================================================================

/**
 * cow_get_stats - Get COW statistics
 */
struct cow_stats *cow_get_stats(void)
{
    return &cow_stats;
}

/**
 * cow_print_stats - Print COW statistics
 */
void cow_print_stats(void)
{
    // TODO: Implement COW statistics printing
    // This would be useful for debugging and monitoring
}

/**
 * cow_init - Initialize COW subsystem
 */
int cow_init(void)
{
    // Initialize COW statistics
    memset(&cow_stats, 0, sizeof(cow_stats));
    
    // Any additional COW initialization
    return 0;
}