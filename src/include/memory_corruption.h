#ifndef __MEMORY_CORRUPTION_H__
#define __MEMORY_CORRUPTION_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// COMPREHENSIVE MEMORY CORRUPTION DETECTION HEADER
// =============================================================================
// Multi-layered memory corruption detection and prevention system
// =============================================================================

// Memory corruption detection configuration
#define CORRUPTION_MAGIC_ALLOC      0xABCDEF00
#define CORRUPTION_MAGIC_FREE       0xDEADBEEF
#define CORRUPTION_CANARY_SIZE      8
#define CORRUPTION_REDZONE_SIZE     16
#define CORRUPTION_MAX_ALLOCS       1024

// Memory allocation tracking states
typedef enum {
    ALLOC_STATE_FREE = 0,
    ALLOC_STATE_ALLOCATED = 1,
    ALLOC_STATE_FREED = 2,
    ALLOC_STATE_CORRUPTED = 3
} alloc_state_t;

// Allocation tracking structure
typedef struct alloc_tracker {
    uint32_t magic;
    void *ptr;
    size_t size;
    alloc_state_t state;
    uint32_t alloc_time;
    uint32_t free_time;
    uint32_t canary_start;
    uint32_t canary_end;
    const char *alloc_function;
    const char *free_function;
    uint32_t checksum;
} alloc_tracker_t;

// Memory corruption statistics
typedef struct corruption_stats {
    uint32_t total_allocations;
    uint32_t total_frees;
    uint32_t current_allocations;
    uint32_t double_free_detected;
    uint32_t use_after_free_detected;
    uint32_t buffer_overflow_detected;
    uint32_t buffer_underflow_detected;
    uint32_t corruption_detected;
    uint32_t leak_warnings;
} corruption_stats_t;

// Initialization and management
void memory_corruption_init(void);
void memory_corruption_enable(void);
void memory_corruption_disable(void);
bool memory_corruption_is_enabled(void);

// Core detection functions
void* corruption_safe_malloc(size_t size, const char *caller);
void corruption_safe_free(void *ptr, const char *caller);
void* corruption_safe_realloc(void *ptr, size_t new_size, const char *caller);

// Memory validation functions
bool corruption_validate_pointer(void *ptr);
bool corruption_validate_allocation(void *ptr);
bool corruption_check_canaries(void *ptr);
bool corruption_check_redzone(void *ptr);

// Detection routines
bool corruption_detect_double_free(void *ptr);
bool corruption_detect_use_after_free(void *ptr);
bool corruption_detect_buffer_overflow(void *ptr);
bool corruption_scan_heap_corruption(void);

// Statistics and reporting
void corruption_get_stats(corruption_stats_t *stats);
void corruption_report_violation(const char *type, void *ptr, const char *details);
void corruption_dump_allocation_info(void *ptr);

// Memory pattern analysis
bool corruption_check_pattern_integrity(void *ptr, size_t size, uint8_t expected_pattern);
void corruption_poison_memory(void *ptr, size_t size, uint8_t poison_value);
bool corruption_verify_poison(void *ptr, size_t size, uint8_t poison_value);

// Leak detection
void corruption_check_memory_leaks(void);
uint32_t corruption_get_leak_count(void);

// Debugging and analysis tools
void corruption_dump_all_allocations(void);
void corruption_analyze_heap_health(void);
bool corruption_validate_heap_integrity(void);

// Convenience macros for safe allocation with tracking
#define SAFE_MALLOC(size) corruption_safe_malloc(size, __FUNCTION__)
#define SAFE_FREE(ptr) do { corruption_safe_free(ptr, __FUNCTION__); ptr = NULL; } while(0)
#define SAFE_REALLOC(ptr, size) corruption_safe_realloc(ptr, size, __FUNCTION__)

// Memory validation macros
#define VALIDATE_PTR(ptr) do { \
    if (!corruption_validate_pointer(ptr)) { \
        corruption_report_violation("Invalid pointer", ptr, __FUNCTION__); \
    } \
} while(0)

#define CHECK_ALLOCATION(ptr) do { \
    if (!corruption_validate_allocation(ptr)) { \
        corruption_report_violation("Invalid allocation", ptr, __FUNCTION__); \
    } \
} while(0)

// Memory poisoning patterns
#define POISON_FREED_MEMORY     0xDE
#define POISON_UNINITIALIZED    0xAA
#define POISON_REDZONE          0xCC
#define POISON_CANARY           0x55

// Advanced corruption detection features
void corruption_enable_guard_pages(void);
void corruption_enable_address_sanitizer(void);
void corruption_periodic_check(void);

#endif /* __MEMORY_CORRUPTION_H__ */