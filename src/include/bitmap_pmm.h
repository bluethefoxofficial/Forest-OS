#ifndef __BITMAP_PMM_H__
#define __BITMAP_PMM_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "memory.h"

// =============================================================================
// BITMAP-BASED PHYSICAL MEMORY MANAGER HEADER
// =============================================================================
// Efficient page frame allocator using bitmap tracking with corruption detection
// =============================================================================

// Configuration constants
#define PMM_PAGE_SIZE           4096
#define PMM_PAGES_PER_BITMAP    32          // 32 pages per uint32_t bitmap entry
#define PMM_MAX_MEMORY_GB       4           // Support up to 4GB physical memory
#define PMM_MAX_PAGES           ((uint32_t)((PMM_MAX_MEMORY_GB * 1024ULL * 1024ULL * 1024ULL) / PMM_PAGE_SIZE))
#define PMM_BITMAP_SIZE         (PMM_MAX_PAGES / PMM_PAGES_PER_BITMAP)

// Backwards-compatible aliases for region type values
#define MEMORY_TYPE_AVAILABLE   MEMORY_REGION_AVAILABLE
#define MEMORY_TYPE_RESERVED    MEMORY_REGION_RESERVED
#define MEMORY_TYPE_ACPI_RECLAIM MEMORY_REGION_ACPI_RECLAIM
#define MEMORY_TYPE_ACPI_NVS    MEMORY_REGION_ACPI_NVS
#define MEMORY_TYPE_BAD         MEMORY_REGION_BADRAM
#define MEMORY_TYPE_KERNEL      MEMORY_REGION_KERNEL
#define MEMORY_TYPE_INITRD      MEMORY_REGION_INITRD

// Page frame states
typedef enum {
    PAGE_FREE = 0,
    PAGE_USED = 1,
    PAGE_RESERVED = 2,
    PAGE_DMA = 3,
    PAGE_CORRUPTED = 4
} page_state_t;

// Physical memory statistics
typedef struct pmm_stats {
    uint32_t total_pages;
    uint32_t free_pages;
    uint32_t used_pages;
    uint32_t reserved_pages;
    uint32_t dma_pages;
    uint32_t corrupted_pages;
    
    uint32_t total_allocations;
    uint32_t total_frees;
    uint32_t allocation_failures;
    uint32_t fragmentation_level;
    uint32_t fragmentation_ratio;
    
    uint32_t largest_free_block;
    uint32_t smallest_free_block;
    uint32_t free_block_count;
    
    // Corruption detection statistics
    uint32_t bitmap_corruption_detected;
    uint32_t metadata_corruption_detected;
    uint32_t checksum_failures;
} pmm_stats_t;

// Allocation preferences
typedef enum {
    PMM_ALLOC_LOW_MEMORY,    // Prefer low memory (DMA-capable)
    PMM_ALLOC_HIGH_MEMORY,   // Prefer high memory
    PMM_ALLOC_ANY_MEMORY     // No preference
} pmm_alloc_preference_t;

// Page frame allocator configuration
typedef struct pmm_config {
    bool corruption_detection_enabled;
    bool defragmentation_enabled;
    bool statistics_tracking_enabled;
    bool debug_mode_enabled;
    uint32_t reserved_pages_low;     // Pages to keep free in low memory
    uint32_t reserved_pages_high;    // Pages to keep free in high memory
} pmm_config_t;

// Bitmap metadata with corruption protection
typedef struct bitmap_metadata {
    uint32_t magic_header;           // Header magic number
    uint32_t checksum;               // Bitmap checksum
    uint32_t total_pages;            // Total number of pages tracked
    uint32_t free_pages;             // Number of free pages
    uint32_t last_alloc_hint;        // Hint for next allocation
    uint32_t magic_footer;           // Footer magic number
} bitmap_metadata_t;

// Initialization and configuration
void bitmap_pmm_init(const pmm_config_t* config);
void bitmap_pmm_add_memory_region(uint64_t base, uint64_t length, memory_region_type_t type);
void bitmap_pmm_finalize_initialization(void);

// Core allocation functions
uint32_t bitmap_pmm_alloc_page(pmm_alloc_preference_t preference);
uint32_t bitmap_pmm_alloc_pages(uint32_t count, pmm_alloc_preference_t preference);
uint32_t bitmap_pmm_alloc_contiguous_pages(uint32_t count, uint32_t alignment);
void bitmap_pmm_free_page(uint32_t page_frame);
void bitmap_pmm_free_pages(uint32_t page_frame, uint32_t count);

// Page state management
page_state_t bitmap_pmm_get_page_state(uint32_t page_frame);
void bitmap_pmm_set_page_state(uint32_t page_frame, page_state_t state);
void bitmap_pmm_mark_pages_reserved(uint32_t start_page, uint32_t count);
void bitmap_pmm_mark_pages_dma(uint32_t start_page, uint32_t count);

// Validation and integrity checking
bool bitmap_pmm_validate_page_frame(uint32_t page_frame);
bool bitmap_pmm_validate_bitmap_integrity(void);
bool bitmap_pmm_check_corruption(void);
void bitmap_pmm_scan_for_corruption(void);

// Statistics and analysis
void bitmap_pmm_get_stats(pmm_stats_t* stats);
void bitmap_pmm_dump_stats(void);
void bitmap_pmm_analyze_fragmentation(void);
uint32_t bitmap_pmm_find_largest_free_block(void);

// Memory region management
void bitmap_pmm_dump_memory_map(void);
uint32_t bitmap_pmm_get_total_memory(void);
uint32_t bitmap_pmm_get_usable_memory(void);

// Advanced features
void bitmap_pmm_defragment(void);
void bitmap_pmm_compact_free_pages(void);
uint32_t bitmap_pmm_allocate_at_address(uint32_t page_frame);

// Debugging and diagnostics
void bitmap_pmm_dump_bitmap(uint32_t start_page, uint32_t count);
void bitmap_pmm_dump_free_regions(void);
void bitmap_pmm_verify_consistency(void);

// Utility functions
static inline uint32_t bitmap_pmm_addr_to_page(uint32_t addr) {
    return addr / PMM_PAGE_SIZE;
}

static inline uint32_t bitmap_pmm_page_to_addr(uint32_t page_frame) {
    return page_frame * PMM_PAGE_SIZE;
}

static inline bool bitmap_pmm_is_page_aligned(uint32_t addr) {
    return (addr & (PMM_PAGE_SIZE - 1)) == 0;
}

// Bitmap manipulation helpers
static inline uint32_t bitmap_pmm_get_bitmap_index(uint32_t page_frame) {
    return page_frame / PMM_PAGES_PER_BITMAP;
}

static inline uint32_t bitmap_pmm_get_bit_offset(uint32_t page_frame) {
    return page_frame % PMM_PAGES_PER_BITMAP;
}

// Memory zone definitions for different allocation preferences
#define PMM_LOW_MEMORY_LIMIT    (16 * 1024 * 1024)  // 16MB limit for DMA
#define PMM_DMA_ZONE_PAGES      (PMM_LOW_MEMORY_LIMIT / PMM_PAGE_SIZE)

// Configuration macros
#define PMM_MAGIC_HEADER        0x504D4D48  // "PMMH"
#define PMM_MAGIC_FOOTER        0x504D4D46  // "PMMF"
#define PMM_CORRUPTION_MARKER   0xDEADC0DE

#endif /* __BITMAP_PMM_H__ */
