// =============================================================================
// MEMORY DEBUGGING AND ROBUSTNESS FEATURES - FOREST OS v3.0
// =============================================================================
// Comprehensive memory debugging, guard pages, poisoning, and corruption detection
// Provides tools for detecting memory bugs and improving system robustness
// =============================================================================

#include "include/mm.h"
#include "include/atomic_mm.h"
#include "include/list.h"
#include "include/interrupt.h"
#include "include/system.h"
#include "include/memory.h" // For kmalloc declaration
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// DEBUG CONFIGURATION AND CONSTANTS
// =============================================================================

// Debug feature enable flags (compile-time configuration)
#define MM_DEBUG_GUARD_PAGES    1       // Enable guard pages
#define MM_DEBUG_POISONING      1       // Enable memory poisoning
#define MM_DEBUG_DOUBLE_FREE    1       // Enable double-free detection
#define MM_DEBUG_LEAK_TRACKING  1       // Enable leak tracking
#define MM_DEBUG_STACK_GUARD    1       // Enable stack guard pages

// Poison patterns (easily recognizable in memory dumps)
#define POISON_FREE             0xDEADBEEF  // Freed memory
#define POISON_UNINITIALIZED    0xAAAAAAAA  // Uninitialized memory
#define POISON_RED_ZONE         0xCC        // Red zone bytes
#define POISON_GUARD_PAGE       0xBBADBEEF  // Guard pages

// Debug allocation metadata
#define ALLOC_MAGIC             0x12345678  // Magic number for valid allocations
#define FREE_MAGIC              0x87654321  // Magic number for freed allocations

// Guard page configuration
#define GUARD_PAGE_SIZE         PAGE_SIZE   // Size of guard pages
#define STACK_GUARD_PAGES       2           // Number of guard pages for stacks

// =============================================================================
// DATA STRUCTURES FOR DEBUGGING
// =============================================================================

// Allocation tracking metadata
struct alloc_header {
    uint32_t magic;                 // Magic number for validation
    size_t size;                    // Size of allocation
    const char *file;               // Source file name
    int line;                       // Source line number
    const char *func;               // Function name
    uint32_t timestamp;             // Allocation timestamp
    struct list_head list;          // List linkage for tracking
    uint32_t checksum;              // Header checksum
};

// Red zone footer for overflow detection
struct alloc_footer {
    uint32_t magic;                 // Magic number
    uint32_t pattern[4];            // Red zone pattern
};

// Guard page descriptor
struct guard_page {
    unsigned long address;          // Guard page virtual address
    size_t size;                    // Size of guarded region
    const char *purpose;            // Purpose description
    struct list_head list;          // List linkage
};

// Leak tracking entry
struct leak_entry {
    void *address;                  // Allocation address
    size_t size;                    // Allocation size
    const char *file;               // Source file
    int line;                       // Source line
    uint32_t timestamp;             // Allocation time
    struct list_head list;          // List linkage
};

// =============================================================================
// GLOBAL DEBUG STATE
// =============================================================================

// Debug configuration
static struct debug_config {
    bool guard_pages_enabled;       // Guard page protection
    bool poisoning_enabled;         // Memory poisoning
    bool double_free_check;         // Double-free detection
    bool leak_tracking_enabled;     // Memory leak tracking
    bool stack_guard_enabled;       // Stack overflow protection
    
    unsigned long poison_on_alloc;  // Poison pattern for new allocations
    unsigned long poison_on_free;   // Poison pattern for freed memory
} debug_config = {
    .guard_pages_enabled = MM_DEBUG_GUARD_PAGES,
    .poisoning_enabled = MM_DEBUG_POISONING,
    .double_free_check = MM_DEBUG_DOUBLE_FREE,
    .leak_tracking_enabled = MM_DEBUG_LEAK_TRACKING,
    .stack_guard_enabled = MM_DEBUG_STACK_GUARD,
    .poison_on_alloc = POISON_UNINITIALIZED,
    .poison_on_free = POISON_FREE
};

// Debug statistics (struct debug_stats defined in mm.h)
static struct debug_stats debug_stats = {0};

// Tracking lists
static struct debug_lists {
    struct list_head alloc_list;    // Active allocations
    struct list_head guard_pages;   // Guard pages
    struct list_head leak_list;     // Potential leaks
    spinlock_t alloc_lock;         // Allocation list lock
    spinlock_t guard_lock;         // Guard page list lock
    spinlock_t leak_lock;          // Leak list lock
} debug_lists;

// =============================================================================
// INITIALIZATION
// =============================================================================

/**
 * mm_debug_init - Initialize memory debugging subsystem
 */
int mm_debug_init(void)
{
    // Initialize debug lists
    INIT_LIST_HEAD(&debug_lists.alloc_list);
    INIT_LIST_HEAD(&debug_lists.guard_pages);
    INIT_LIST_HEAD(&debug_lists.leak_list);
    
    spin_lock_init(&debug_lists.alloc_lock);
    spin_lock_init(&debug_lists.guard_lock);
    spin_lock_init(&debug_lists.leak_lock);
    
    // Initialize statistics
    memset(&debug_stats, 0, sizeof(debug_stats));
    
    return 0;
}

// =============================================================================
// MEMORY POISONING
// =============================================================================

/**
 * mm_poison_memory - Fill memory with poison pattern
 * @addr: Memory address to poison
 * @size: Size of memory to poison
 * @pattern: Poison pattern to use
 */
void mm_poison_memory(void *addr, size_t size, unsigned long pattern)
{
    uint32_t *ptr;
    size_t words;
    size_t remaining;
    uint8_t *byte_ptr;
    
    if (!addr || size == 0 || !debug_config.poisoning_enabled) {
        return;
    }
    
    // Poison word-aligned portion
    ptr = (uint32_t *)addr;
    words = size / sizeof(uint32_t);
    
    for (size_t i = 0; i < words; i++) {
        ptr[i] = pattern;
    }
    
    // Poison remaining bytes
    remaining = size % sizeof(uint32_t);
    if (remaining > 0) {
        byte_ptr = (uint8_t *)(ptr + words);
        for (size_t i = 0; i < remaining; i++) {
            byte_ptr[i] = (uint8_t)(pattern >> (i * 8));
        }
    }
}

/**
 * mm_check_poison - Check if memory contains expected poison pattern
 * @addr: Memory address to check
 * @size: Size of memory to check
 * @pattern: Expected poison pattern
 * Returns: true if memory is properly poisoned, false if corrupted
 */
bool mm_check_poison(const void *addr, size_t size, unsigned long pattern)
{
    const uint32_t *ptr;
    size_t words;
    size_t remaining;
    const uint8_t *byte_ptr;
    
    if (!addr || size == 0 || !debug_config.poisoning_enabled) {
        return true; // Skip check if poisoning disabled
    }
    
    // Check word-aligned portion
    ptr = (const uint32_t *)addr;
    words = size / sizeof(uint32_t);
    
    for (size_t i = 0; i < words; i++) {
        if (ptr[i] != pattern) {
            debug_stats.corruption_detected++;
            return false;
        }
    }
    
    // Check remaining bytes
    remaining = size % sizeof(uint32_t);
    if (remaining > 0) {
        byte_ptr = (const uint8_t *)(ptr + words);
        for (size_t i = 0; i < remaining; i++) {
            uint8_t expected = (uint8_t)(pattern >> (i * 8));
            if (byte_ptr[i] != expected) {
                debug_stats.corruption_detected++;
                return false;
            }
        }
    }
    
    return true;
}

// =============================================================================
// ALLOCATION TRACKING
// =============================================================================

/**
 * mm_track_allocation - Track a memory allocation for debugging
 * @addr: Allocated memory address
 * @size: Size of allocation
 * @file: Source file name
 * @line: Source line number
 * @func: Function name
 */
void mm_track_allocation(void *addr, size_t size, const char *file, 
                        int line, const char *func)
{
    struct alloc_header *header;
    struct alloc_footer *footer;
    void *user_ptr;
    uint32_t checksum;
    
    if (!addr || size == 0 || !debug_config.leak_tracking_enabled) {
        return;
    }
    
    // Calculate addresses
    header = (struct alloc_header *)addr;
    user_ptr = (char *)addr + sizeof(struct alloc_header);
    footer = (struct alloc_footer *)((char *)user_ptr + size);
    
    // Initialize header
    header->magic = ALLOC_MAGIC;
    header->size = size;
    header->file = file;
    header->line = line;
    header->func = func;
    header->timestamp = 0; // TODO: Get actual timestamp
    
    // Calculate header checksum
    checksum = header->magic ^ header->size ^ header->line;
    header->checksum = checksum;
    
    // Initialize footer with red zone
    footer->magic = ALLOC_MAGIC;
    for (int i = 0; i < 4; i++) {
        footer->pattern[i] = POISON_RED_ZONE | (POISON_RED_ZONE << 8) |
                           (POISON_RED_ZONE << 16) | (POISON_RED_ZONE << 24);
    }
    
    // Poison user memory if requested
    if (debug_config.poisoning_enabled) {
        mm_poison_memory(user_ptr, size, debug_config.poison_on_alloc);
    }
    
    // Add to tracking list
    spin_lock(&debug_lists.alloc_lock);
    list_add(&header->list, &debug_lists.alloc_list);
    debug_stats.total_allocations++;
    debug_stats.active_allocations++;
    spin_unlock(&debug_lists.alloc_lock);
}

/**
 * mm_untrack_allocation - Remove allocation from tracking
 * @addr: Address to untrack (user pointer)
 * Returns: true if allocation was valid, false if double-free or corruption
 */
bool mm_untrack_allocation(void *addr)
{
    struct alloc_header *header;
    struct alloc_footer *footer;
    uint32_t expected_checksum;
    bool valid = true;
    
    if (!addr || !debug_config.leak_tracking_enabled) {
        return true; // Skip if tracking disabled
    }
    
    // Calculate header address
    header = (struct alloc_header *)((char *)addr - sizeof(struct alloc_header));
    
    // Validate magic number
    if (header->magic != ALLOC_MAGIC) {
        if (header->magic == FREE_MAGIC && debug_config.double_free_check) {
            debug_stats.double_frees++;
            return false; // Double-free detected
        }
        debug_stats.corruption_detected++;
        return false; // Corruption or invalid pointer
    }
    
    // Validate header checksum
    expected_checksum = ALLOC_MAGIC ^ header->size ^ header->line;
    if (header->checksum != expected_checksum) {
        debug_stats.corruption_detected++;
        valid = false;
    }
    
    // Validate footer red zone
    footer = (struct alloc_footer *)((char *)addr + header->size);
    if (footer->magic != ALLOC_MAGIC) {
        debug_stats.corruption_detected++;
        valid = false;
    }
    
    for (int i = 0; i < 4; i++) {
        uint32_t expected = POISON_RED_ZONE | (POISON_RED_ZONE << 8) |
                          (POISON_RED_ZONE << 16) | (POISON_RED_ZONE << 24);
        if (footer->pattern[i] != expected) {
            debug_stats.corruption_detected++;
            valid = false;
        }
    }
    
    // Remove from tracking list
    spin_lock(&debug_lists.alloc_lock);
    list_del(&header->list);
    debug_stats.total_frees++;
    debug_stats.active_allocations--;
    spin_unlock(&debug_lists.alloc_lock);
    
    // Poison freed memory
    if (debug_config.poisoning_enabled && valid) {
        mm_poison_memory(addr, header->size, debug_config.poison_on_free);
    }
    
    // Mark as freed
    header->magic = FREE_MAGIC;
    footer->magic = FREE_MAGIC;
    
    return valid;
}

// =============================================================================
// GUARD PAGES
// =============================================================================

/**
 * mm_create_guard_pages - Create guard pages around a memory region
 * @start: Start of region to protect
 * @size: Size of region to protect
 * @purpose: Description of what we're protecting
 * Returns: 0 on success, negative error on failure
 */
int mm_create_guard_pages(unsigned long start, size_t size, const char *purpose)
{
    struct guard_page *guard;
    unsigned long guard_start, guard_end;
    
    if (!debug_config.guard_pages_enabled || size == 0) {
        return 0;
    }
    
    guard_start = start - GUARD_PAGE_SIZE;
    guard_end = start + size;
    
    // Allocate guard page descriptor
    guard = kmalloc(sizeof(struct guard_page));
    if (!guard) {
        return -1;
    }
    
    // Initialize guard page
    guard->address = start;
    guard->size = size;
    guard->purpose = purpose;
    
    // TODO: Map guard pages as non-accessible
    // This would involve setting up page table entries that cause
    // page faults when accessed
    
    // Add to guard page list
    spin_lock(&debug_lists.guard_lock);
    list_add(&guard->list, &debug_lists.guard_pages);
    spin_unlock(&debug_lists.guard_lock);
    
    return 0;
}

/**
 * mm_remove_guard_pages - Remove guard pages for a memory region
 * @start: Start address of protected region
 */
void mm_remove_guard_pages(unsigned long start)
{
    struct guard_page *guard, *tmp;
    
    if (!debug_config.guard_pages_enabled) {
        return;
    }
    
    spin_lock(&debug_lists.guard_lock);
    
    list_for_each_entry_safe(guard, tmp, &debug_lists.guard_pages, list) {
        if (guard->address == start) {
            list_del(&guard->list);
            kfree(guard);
            break;
        }
    }
    
    spin_unlock(&debug_lists.guard_lock);
}

/**
 * mm_handle_guard_page_fault - Handle page fault on guard page
 * @address: Fault address
 * Returns: true if this was a guard page fault, false otherwise
 */
bool mm_handle_guard_page_fault(unsigned long address)
{
    struct guard_page *guard;
    bool found = false;
    
    if (!debug_config.guard_pages_enabled) {
        return false;
    }
    
    spin_lock(&debug_lists.guard_lock);
    
    list_for_each_entry(guard, &debug_lists.guard_pages, list) {
        unsigned long guard_start = guard->address - GUARD_PAGE_SIZE;
        unsigned long guard_end = guard->address + guard->size + GUARD_PAGE_SIZE;
        
        if (address >= guard_start && address < guard_end) {
            debug_stats.guard_page_hits++;
            found = true;
            
            // TODO: Log guard page violation
            // This would typically print debug information about
            // the guard page violation including the purpose and
            // the faulting address
            
            break;
        }
    }
    
    spin_unlock(&debug_lists.guard_lock);
    return found;
}

// =============================================================================
// STACK PROTECTION
// =============================================================================

/**
 * mm_setup_stack_guard - Set up stack guard pages
 * @stack_base: Base of stack
 * @stack_size: Size of stack
 * Returns: 0 on success, negative error on failure
 */
int mm_setup_stack_guard(unsigned long stack_base, size_t stack_size)
{
    if (!debug_config.stack_guard_enabled) {
        return 0;
    }
    
    // Create guard pages at both ends of stack
    return mm_create_guard_pages(stack_base, stack_size, "stack overflow protection");
}

// =============================================================================
// LEAK DETECTION
// =============================================================================

/**
 * mm_scan_for_leaks - Scan for memory leaks
 * Returns: Number of leaks detected
 */
unsigned long mm_scan_for_leaks(void)
{
    struct alloc_header *alloc;
    unsigned long leaks = 0;
    
    if (!debug_config.leak_tracking_enabled) {
        return 0;
    }
    
    spin_lock(&debug_lists.alloc_lock);
    
    list_for_each_entry(alloc, &debug_lists.alloc_list, list) {
        // TODO: Implement more sophisticated leak detection
        // This could involve scanning for reachable pointers,
        // checking allocation age, etc.
        
        leaks++;
    }
    
    spin_unlock(&debug_lists.alloc_lock);
    
    debug_stats.leaks_detected = leaks;
    return leaks;
}

// =============================================================================
// STATISTICS AND REPORTING
// =============================================================================

/**
 * mm_debug_get_stats - Get debug statistics
 */
struct debug_stats *mm_debug_get_stats(void)
{
    return &debug_stats;
}

/**
 * mm_debug_print_stats - Print debug statistics
 */
void mm_debug_print_stats(void)
{
    // TODO: Implement debug statistics printing
    // This would print allocation counts, corruption events,
    // guard page hits, etc.
}

/**
 * mm_debug_dump_allocations - Dump all tracked allocations
 */
void mm_debug_dump_allocations(void)
{
    struct alloc_header *alloc;
    
    if (!debug_config.leak_tracking_enabled) {
        return;
    }
    
    spin_lock(&debug_lists.alloc_lock);
    
    list_for_each_entry(alloc, &debug_lists.alloc_list, list) {
        // TODO: Print allocation information
        // This would show file, line, function, size, timestamp
    }
    
    spin_unlock(&debug_lists.alloc_lock);
}

// =============================================================================
// CONFIGURATION FUNCTIONS
// =============================================================================

/**
 * mm_debug_enable_feature - Enable a debug feature
 * @feature: Feature to enable
 */
void mm_debug_enable_feature(const char *feature)
{
    // TODO: Implement feature enable/disable
    // This would allow runtime configuration of debug features
}

/**
 * mm_debug_set_poison_pattern - Set poison pattern
 * @type: Type of poison (alloc/free)
 * @pattern: New pattern to use
 */
void mm_debug_set_poison_pattern(int type, unsigned long pattern)
{
    if (type == 0) {
        debug_config.poison_on_alloc = pattern;
    } else {
        debug_config.poison_on_free = pattern;
    }
}