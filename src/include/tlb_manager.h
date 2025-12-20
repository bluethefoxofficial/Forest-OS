#ifndef __TLB_MANAGER_H__
#define __TLB_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// TLB MANAGEMENT HEADER
// =============================================================================

// Initialization
void tlb_manager_init(void);

// Core TLB invalidation functions
void tlb_invalidate_page(uint32_t virtual_addr);
void tlb_invalidate_range(uint32_t start_addr, uint32_t end_addr);
void tlb_flush_all(void);
void tlb_flush_non_global(void);

// Safe mapping operations
void tlb_invalidate_for_mapping(uint32_t virtual_addr, bool was_mapped);
void tlb_safe_pte_update(uint32_t virtual_addr, uint32_t old_pte, uint32_t new_pte);
void tlb_safe_heap_expand(uint32_t start_vaddr, uint32_t num_pages);

// Multiprocessor support
void tlb_shootdown_page(uint32_t virtual_addr);

// Feature detection
bool tlb_has_invlpg(void);
bool tlb_has_global_pages(void);
bool tlb_has_pcid(void);

// Statistics
void tlb_get_stats(uint32_t *flushes_single, uint32_t *flushes_all);

#endif /* __TLB_MANAGER_H__ */