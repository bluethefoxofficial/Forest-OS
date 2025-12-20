#ifndef __ENHANCED_HEAP_H__
#define __ENHANCED_HEAP_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// ENHANCED HEAP ALLOCATOR HEADER
// =============================================================================
// Redesigned heap with corruption detection integration and better performance
// =============================================================================

// Enhanced heap configuration
#define ENHANCED_HEAP_MAGIC         0x45484541  // "EHEA"
#define ENHANCED_BLOCK_MAGIC        0x45424C4B  // "EBLK"
#define ENHANCED_GUARD_MAGIC        0x47554152  // "GUAR"

// Size classes for segregated free lists (powers of 2)
#define HEAP_SIZE_CLASSES           12
#define HEAP_MIN_BLOCK_SIZE         16
#define HEAP_MAX_SMALL_BLOCK        2048

// Block states
typedef enum {
    EBLOCK_FREE = 0x46524545,          // "FREE"
    EBLOCK_USED = 0x55534544,          // "USED"
    EBLOCK_GUARD = 0x47554152,         // "GUAR"
    EBLOCK_CORRUPTED = 0x43525054      // "CRPT"
} enhanced_block_state_t;

// Enhanced block header with corruption detection
typedef struct enhanced_heap_block {
    uint32_t header_magic;             // Header magic number
    uint32_t size;                     // Block size including header
    enhanced_block_state_t state;      // Block state
    uint32_t checksum;                 // Header checksum for integrity
    
    // Free list pointers (only valid when free)
    struct enhanced_heap_block* next_free;
    struct enhanced_heap_block* prev_free;
    
    // Allocation tracking
    uint32_t alloc_sequence;           // Allocation sequence number
    uint32_t alloc_time;               // Timestamp of allocation
    const char* alloc_function;        // Function that allocated this
    
    // Corruption detection integration
    uint32_t corruption_canary;        // Canary for detecting overwrites
    uint32_t footer_magic;             // Footer magic (at end of block)
} enhanced_heap_block_t;

// Size class information
typedef struct size_class {
    uint32_t block_size;               // Block size for this class
    enhanced_heap_block_t* free_list;  // Free list head
    uint32_t free_count;               // Number of free blocks
    uint32_t total_count;              // Total blocks in this class
} size_class_t;

// Enhanced heap statistics
typedef struct enhanced_heap_stats {
    uint32_t total_allocations;
    uint32_t total_frees;
    uint32_t current_allocations;
    uint32_t bytes_allocated;
    uint32_t bytes_free;
    uint32_t fragmentation_ratio;
    uint32_t corruption_detected;
    uint32_t guard_violations;
    uint32_t metadata_corruptions;
    uint32_t coalesce_operations;
    uint32_t heap_expansions;
    
    // Per-size-class statistics
    struct {
        uint32_t allocations;
        uint32_t current_free;
        uint32_t max_used;
    } size_class_stats[HEAP_SIZE_CLASSES];
} enhanced_heap_stats_t;

// Enhanced heap configuration
typedef struct enhanced_heap_config {
    bool corruption_detection_enabled;
    bool guard_pages_enabled;
    bool metadata_protection_enabled;
    bool fragmentation_mitigation_enabled;
    bool debug_mode_enabled;
    uint32_t max_heap_size;
    uint32_t expansion_increment;
} enhanced_heap_config_t;

// Initialization and configuration
void enhanced_heap_init(const enhanced_heap_config_t* config);
void enhanced_heap_enable_corruption_detection(bool enabled);
void enhanced_heap_enable_debug_mode(bool enabled);

// Core allocation functions
void* enhanced_heap_alloc(size_t size, const char* caller);
void enhanced_heap_free(void* ptr, const char* caller);
void* enhanced_heap_realloc(void* ptr, size_t new_size, const char* caller);

// Validation and integrity checking
bool enhanced_heap_validate_pointer(void* ptr);
bool enhanced_heap_validate_block(enhanced_heap_block_t* block);
bool enhanced_heap_validate_all_blocks(void);
bool enhanced_heap_check_integrity(void);

// Corruption detection integration
bool enhanced_heap_detect_corruption(void* ptr);
void enhanced_heap_scan_for_corruption(void);
void enhanced_heap_report_corruption(void* ptr, const char* type, const char* details);

// Statistics and analysis
void enhanced_heap_get_stats(enhanced_heap_stats_t* stats);
void enhanced_heap_dump_stats(void);
void enhanced_heap_analyze_fragmentation(void);
void enhanced_heap_dump_free_lists(void);

// Advanced features
void enhanced_heap_compact(void);
void enhanced_heap_defragment(void);
size_t enhanced_heap_get_largest_free_block(void);
uint32_t enhanced_heap_get_fragmentation_ratio(void);

// Guard page protection
void enhanced_heap_enable_guard_pages(void);
void enhanced_heap_check_guard_pages(void);

// Debugging and diagnostics
void enhanced_heap_dump_block_info(void* ptr);
void enhanced_heap_dump_allocation_history(void);
void enhanced_heap_verify_free_lists(void);

// Convenience macros that integrate with corruption detection
#define ENHANCED_ALLOC(size) enhanced_heap_alloc(size, __FUNCTION__)
#define ENHANCED_FREE(ptr) do { \
    enhanced_heap_free(ptr, __FUNCTION__); \
    ptr = NULL; \
} while(0)
#define ENHANCED_REALLOC(ptr, size) enhanced_heap_realloc(ptr, size, __FUNCTION__)

// Size class calculation
static inline int enhanced_heap_size_to_class(size_t size) {
    if (size <= HEAP_MIN_BLOCK_SIZE) return 0;
    if (size > HEAP_MAX_SMALL_BLOCK) return HEAP_SIZE_CLASSES - 1;
    
    // Find the appropriate size class (powers of 2)
    int class = 0;
    size_t class_size = HEAP_MIN_BLOCK_SIZE;
    
    while (class_size < size && class < HEAP_SIZE_CLASSES - 1) {
        class_size *= 2;
        class++;
    }
    
    return class;
}

#endif /* __ENHANCED_HEAP_H__ */