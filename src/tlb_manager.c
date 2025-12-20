#include "include/memory.h"
#include "include/screen.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declaration
void tlb_flush_all(void);

// =============================================================================
// TLB MANAGEMENT AND INVALIDATION
// =============================================================================
// Proper Translation Lookaside Buffer management to prevent stale entries
// =============================================================================

// CPU feature detection flags
static bool cpu_has_invlpg = false;
static bool cpu_has_pcid = false;
static bool cpu_has_global_pages = false;

// Initialize TLB manager and detect CPU features
void tlb_manager_init(void) {
    uint32_t eax, ebx, ecx, edx;
    
    // Check for INVLPG instruction support (486+)
    __asm__ __volatile__ ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    cpu_has_invlpg = (edx & (1 << 6)) != 0;  // Bit 6 indicates 486+ features
    
    // Check for global pages support
    cpu_has_global_pages = (edx & (1 << 13)) != 0;  // Bit 13: PGE
    
    // Check for PCID support (process context IDs)
    __asm__ __volatile__ ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    cpu_has_pcid = (ecx & (1 << 17)) != 0;  // Bit 17: PCID
    
    if (cpu_has_invlpg) {
        print("[TLB] INVLPG instruction supported\n");
    } else {
        print("[TLB] Warning: INVLPG not supported, using CR3 reload\n");
    }
    
    if (cpu_has_global_pages) {
        print("[TLB] Global pages supported\n");
        // Enable global pages in CR4
        uint32_t cr4;
        __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1 << 7);  // Set PGE bit
        __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4));
    }
}

// Invalidate a single page in TLB
void tlb_invalidate_page(uint32_t virtual_addr) {
    if (cpu_has_invlpg) {
        // Use INVLPG instruction for efficient single-page invalidation
        __asm__ __volatile__("invlpg (%0)" : : "r"(virtual_addr) : "memory");
    } else {
        // Fallback: reload CR3 to flush entire TLB (less efficient)
        tlb_flush_all();
    }
}

// Invalidate multiple pages efficiently  
void tlb_invalidate_range(uint32_t start_addr, uint32_t end_addr) {
    uint32_t num_pages = (end_addr - start_addr + MEMORY_PAGE_SIZE - 1) / MEMORY_PAGE_SIZE;
    
    // If invalidating many pages, it's more efficient to flush all
    if (num_pages > 64 || !cpu_has_invlpg) {
        tlb_flush_all();
        return;
    }
    
    // Invalidate individual pages
    for (uint32_t addr = start_addr & ~(MEMORY_PAGE_SIZE - 1); 
         addr < end_addr; 
         addr += MEMORY_PAGE_SIZE) {
        tlb_invalidate_page(addr);
    }
}

// Flush entire TLB by reloading CR3
void tlb_flush_all(void) {
    uint32_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

// Flush all non-global TLB entries
void tlb_flush_non_global(void) {
    if (cpu_has_global_pages) {
        // When global pages are enabled, CR3 reload only flushes non-global pages
        tlb_flush_all();
    } else {
        // No global pages, so this is the same as flush_all
        tlb_flush_all();
    }
}

// Safe TLB invalidation for memory mapping changes
void tlb_invalidate_for_mapping(uint32_t virtual_addr, bool was_mapped) {
    if (was_mapped) {
        // Page was previously mapped, must invalidate
        tlb_invalidate_page(virtual_addr);
    }
    // If page wasn't mapped before, no TLB entry should exist
}

// TLB shootdown for multiprocessor systems (stub for now)
void tlb_shootdown_page(uint32_t virtual_addr) {
    // TODO: Implement IPI-based TLB shootdown for SMP systems
    tlb_invalidate_page(virtual_addr);
}

// Barrier to ensure TLB invalidation completes before continuing
static inline void tlb_barrier(void) {
    __asm__ __volatile__("" : : : "memory");
}

// Enhanced heap expansion with proper TLB management
void tlb_safe_heap_expand(uint32_t start_vaddr, uint32_t num_pages) {
    // Ensure any stale TLB entries for the new heap range are invalidated
    uint32_t end_vaddr = start_vaddr + (num_pages * MEMORY_PAGE_SIZE);
    
    // Invalidate the range before mapping to ensure clean state
    tlb_invalidate_range(start_vaddr, end_vaddr);
    
    // Ensure invalidation completes
    tlb_barrier();
}

// Safe page table entry modification
void tlb_safe_pte_update(uint32_t virtual_addr, uint32_t old_pte, uint32_t new_pte) {
    // Check if the page was previously present
    bool was_present = (old_pte & 0x1) != 0;
    bool will_be_present = (new_pte & 0x1) != 0;
    
    if (was_present || will_be_present) {
        // Need to invalidate if the page was or will be present
        tlb_invalidate_page(virtual_addr);
        tlb_barrier();
    }
}

// Get TLB statistics (for debugging)
void tlb_get_stats(uint32_t *flushes_single, uint32_t *flushes_all) {
    // Simple counters (static variables)
    static uint32_t single_count = 0;
    static uint32_t all_count = 0;
    
    if (flushes_single) *flushes_single = single_count;
    if (flushes_all) *flushes_all = all_count;
}

// Check if TLB features are available
bool tlb_has_invlpg(void) { return cpu_has_invlpg; }
bool tlb_has_global_pages(void) { return cpu_has_global_pages; }
bool tlb_has_pcid(void) { return cpu_has_pcid; }
