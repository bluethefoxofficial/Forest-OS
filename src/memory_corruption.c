#include "include/memory_corruption.h"
#include "include/screen.h"
#include "include/memory.h"
#include "include/ssp.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// COMPREHENSIVE MEMORY CORRUPTION DETECTION IMPLEMENTATION
// =============================================================================
// Multi-layered detection system for heap corruption, use-after-free, etc.
// =============================================================================

// Global state and tracking
static alloc_tracker_t allocation_table[CORRUPTION_MAX_ALLOCS];
static corruption_stats_t corruption_stats = {0};
static bool corruption_detection_enabled = false;
static uint32_t allocation_counter = 0;
static uint32_t current_time_counter = 0;

// Emergency reporting function (direct VGA to avoid heap allocation)
static void corruption_emergency_report(const char *msg, int row) {
    volatile uint16_t *vga = (volatile uint16_t*)0xB8000;
    int pos = row * 80;
    int col = 0;
    
    while (*msg && col < 80) {
        vga[pos + col] = (0x4F << 8) | *msg;  // Red background, white text
        msg++;
        col++;
    }
}

// Calculate simple checksum for allocation tracker integrity
static uint32_t calculate_tracker_checksum(const alloc_tracker_t *tracker) {
    uint32_t checksum = 0;
    checksum ^= tracker->magic;
    checksum ^= (uint32_t)tracker->ptr;
    checksum ^= tracker->size;
    checksum ^= tracker->state;
    checksum ^= tracker->alloc_time;
    checksum ^= tracker->canary_start;
    checksum ^= tracker->canary_end;
    return checksum;
}

// Find allocation tracker entry for a pointer
static alloc_tracker_t* find_allocation_tracker(void *ptr) {
    if (!ptr) {
        return NULL;
    }
    
    for (int i = 0; i < CORRUPTION_MAX_ALLOCS; i++) {
        alloc_tracker_t *tracker = &allocation_table[i];
        
        if (tracker->magic == CORRUPTION_MAGIC_ALLOC && tracker->ptr == ptr) {
            // Verify tracker integrity
            uint32_t expected_checksum = calculate_tracker_checksum(tracker);
            if (tracker->checksum != expected_checksum) {
                corruption_report_violation("Tracker corruption", ptr, "Checksum mismatch");
                return NULL;
            }
            return tracker;
        }
    }
    
    return NULL;
}

// Find free slot in allocation table
static alloc_tracker_t* find_free_tracker_slot(void) {
    for (int i = 0; i < CORRUPTION_MAX_ALLOCS; i++) {
        if (allocation_table[i].magic != CORRUPTION_MAGIC_ALLOC) {
            return &allocation_table[i];
        }
    }
    return NULL;
}

// Initialize memory corruption detection system
void memory_corruption_init(void) {
    print("[CORRUPTION] Initializing memory corruption detection...\n");
    
    // Clear allocation tracking table
    for (int i = 0; i < CORRUPTION_MAX_ALLOCS; i++) {
        allocation_table[i].magic = 0;
        allocation_table[i].ptr = NULL;
        allocation_table[i].size = 0;
        allocation_table[i].state = ALLOC_STATE_FREE;
        allocation_table[i].alloc_time = 0;
        allocation_table[i].free_time = 0;
        allocation_table[i].canary_start = 0;
        allocation_table[i].canary_end = 0;
        allocation_table[i].alloc_function = NULL;
        allocation_table[i].free_function = NULL;
        allocation_table[i].checksum = 0;
    }
    
    // Reset statistics
    corruption_stats.total_allocations = 0;
    corruption_stats.total_frees = 0;
    corruption_stats.current_allocations = 0;
    corruption_stats.double_free_detected = 0;
    corruption_stats.use_after_free_detected = 0;
    corruption_stats.buffer_overflow_detected = 0;
    corruption_stats.buffer_underflow_detected = 0;
    corruption_stats.corruption_detected = 0;
    corruption_stats.leak_warnings = 0;
    
    allocation_counter = 0;
    current_time_counter = 1;
    
    print("[CORRUPTION] Detection system initialized\n");
}

// Enable corruption detection
void memory_corruption_enable(void) {
    corruption_detection_enabled = true;
    print("[CORRUPTION] Detection enabled\n");
}

// Disable corruption detection
void memory_corruption_disable(void) {
    corruption_detection_enabled = false;
    print("[CORRUPTION] Detection disabled\n");
}

// Check if corruption detection is enabled
bool memory_corruption_is_enabled(void) {
    return corruption_detection_enabled;
}

// Safe malloc with corruption tracking
void* corruption_safe_malloc(size_t size, const char *caller) {
    if (!corruption_detection_enabled || size == 0) {
        // Fall back to regular malloc if not enabled
        extern void* kmalloc(size_t size);
        return kmalloc(size);
    }
    
    // Find free tracker slot
    alloc_tracker_t *tracker = find_free_tracker_slot();
    if (!tracker) {
        corruption_report_violation("Allocation table full", NULL, caller);
        return NULL;
    }
    
    // Calculate total size with canaries and redzones
    size_t total_size = size + (2 * CORRUPTION_CANARY_SIZE) + (2 * CORRUPTION_REDZONE_SIZE);
    
    // Allocate memory
    extern void* kmalloc(size_t size);
    void *raw_ptr = kmalloc(total_size);
    if (!raw_ptr) {
        return NULL;
    }
    
    // Calculate user pointer (after front canary and redzone)
    uint8_t *buffer = (uint8_t*)raw_ptr;
    void *user_ptr = buffer + CORRUPTION_CANARY_SIZE + CORRUPTION_REDZONE_SIZE;
    
    // Set up canaries
    uint32_t canary_value = 0xDEADC0DE ^ (uint32_t)user_ptr ^ size;
    
    // Front canary
    *((uint32_t*)(buffer)) = canary_value;
    *((uint32_t*)(buffer + 4)) = ~canary_value;
    
    // Back canary
    uint8_t *back_canary = buffer + CORRUPTION_CANARY_SIZE + CORRUPTION_REDZONE_SIZE + size;
    *((uint32_t*)(back_canary)) = canary_value;
    *((uint32_t*)(back_canary + 4)) = ~canary_value;
    
    // Set up redzones
    corruption_poison_memory(buffer + CORRUPTION_CANARY_SIZE, CORRUPTION_REDZONE_SIZE, POISON_REDZONE);
    corruption_poison_memory(back_canary + CORRUPTION_CANARY_SIZE, CORRUPTION_REDZONE_SIZE, POISON_REDZONE);
    
    // Initialize user memory with pattern for uninitialized access detection
    corruption_poison_memory(user_ptr, size, POISON_UNINITIALIZED);
    
    // Set up tracker
    tracker->magic = CORRUPTION_MAGIC_ALLOC;
    tracker->ptr = user_ptr;
    tracker->size = size;
    tracker->state = ALLOC_STATE_ALLOCATED;
    tracker->alloc_time = ++current_time_counter;
    tracker->free_time = 0;
    tracker->canary_start = canary_value;
    tracker->canary_end = canary_value;
    tracker->alloc_function = caller;
    tracker->free_function = NULL;
    tracker->checksum = calculate_tracker_checksum(tracker);
    
    // Update statistics
    corruption_stats.total_allocations++;
    corruption_stats.current_allocations++;
    
    return user_ptr;
}

// Safe free with corruption detection
void corruption_safe_free(void *ptr, const char *caller) {
    if (!ptr) {
        return;  // Free of NULL is valid
    }
    
    if (!corruption_detection_enabled) {
        // Fall back to regular free
        extern void kfree(void *ptr);
        kfree(ptr);
        return;
    }
    
    // Find allocation tracker
    alloc_tracker_t *tracker = find_allocation_tracker(ptr);
    if (!tracker) {
        corruption_report_violation("Free of untracked pointer", ptr, caller);
        return;
    }
    
    // Check for double-free
    if (tracker->state == ALLOC_STATE_FREED) {
        corruption_stats.double_free_detected++;
        corruption_report_violation("Double free detected", ptr, caller);
        return;
    }
    
    // Check canaries for buffer overflow/underflow
    if (!corruption_check_canaries(ptr)) {
        corruption_stats.buffer_overflow_detected++;
        corruption_report_violation("Buffer overflow/underflow", ptr, caller);
        return;
    }
    
    // Check redzones
    if (!corruption_check_redzone(ptr)) {
        corruption_stats.buffer_overflow_detected++;
        corruption_report_violation("Redzone corruption", ptr, caller);
        return;
    }
    
    // Poison freed memory
    corruption_poison_memory(ptr, tracker->size, POISON_FREED_MEMORY);
    
    // Update tracker accounting before releasing the slot
    tracker->state = ALLOC_STATE_FREED;
    tracker->free_time = ++current_time_counter;
    tracker->free_function = caller;
    tracker->checksum = calculate_tracker_checksum(tracker);
    
    // Update statistics
    corruption_stats.total_frees++;
    corruption_stats.current_allocations--;
    
    // Calculate raw pointer for actual free
    uint8_t *raw_ptr = (uint8_t*)ptr - CORRUPTION_CANARY_SIZE - CORRUPTION_REDZONE_SIZE;
    
    // Free the memory
    extern void kfree(void *ptr);
    kfree(raw_ptr);
    
    // Release tracker slot so future allocations don't exhaust the table.
    tracker->magic = CORRUPTION_MAGIC_FREE;
    tracker->ptr = NULL;
    tracker->size = 0;
    tracker->state = ALLOC_STATE_FREE;
    tracker->alloc_time = 0;
    tracker->free_time = 0;
    tracker->canary_start = 0;
    tracker->canary_end = 0;
    tracker->alloc_function = NULL;
    tracker->free_function = NULL;
    tracker->checksum = 0;
}

// Validate pointer integrity
bool corruption_validate_pointer(void *ptr) {
    if (!ptr) {
        return false;
    }
    
    if (!corruption_detection_enabled) {
        return true;  // Can't validate if not enabled
    }
    
    alloc_tracker_t *tracker = find_allocation_tracker(ptr);
    if (!tracker) {
        return false;
    }
    
    return tracker->state == ALLOC_STATE_ALLOCATED;
}

// Validate allocation integrity
bool corruption_validate_allocation(void *ptr) {
    if (!corruption_validate_pointer(ptr)) {
        return false;
    }
    
    return corruption_check_canaries(ptr) && corruption_check_redzone(ptr);
}

// Check canary values for buffer overflow detection
bool corruption_check_canaries(void *ptr) {
    alloc_tracker_t *tracker = find_allocation_tracker(ptr);
    if (!tracker) {
        return false;
    }
    
    uint8_t *buffer = (uint8_t*)ptr;
    uint8_t *front_canary = buffer - CORRUPTION_REDZONE_SIZE - CORRUPTION_CANARY_SIZE;
    uint8_t *back_canary = buffer + tracker->size;
    
    // Check front canary
    uint32_t front_val1 = *((uint32_t*)front_canary);
    uint32_t front_val2 = *((uint32_t*)(front_canary + 4));
    
    if (front_val1 != tracker->canary_start || front_val2 != ~tracker->canary_start) {
        return false;
    }
    
    // Check back canary
    uint32_t back_val1 = *((uint32_t*)back_canary);
    uint32_t back_val2 = *((uint32_t*)(back_canary + 4));
    
    if (back_val1 != tracker->canary_end || back_val2 != ~tracker->canary_end) {
        return false;
    }
    
    return true;
}

// Check redzone integrity
bool corruption_check_redzone(void *ptr) {
    alloc_tracker_t *tracker = find_allocation_tracker(ptr);
    if (!tracker) {
        return false;
    }
    
    uint8_t *buffer = (uint8_t*)ptr;
    uint8_t *front_redzone = buffer - CORRUPTION_REDZONE_SIZE;
    uint8_t *back_redzone = buffer + tracker->size + CORRUPTION_CANARY_SIZE;
    
    // Check front redzone
    if (!corruption_verify_poison(front_redzone, CORRUPTION_REDZONE_SIZE, POISON_REDZONE)) {
        return false;
    }
    
    // Check back redzone
    if (!corruption_verify_poison(back_redzone, CORRUPTION_REDZONE_SIZE, POISON_REDZONE)) {
        return false;
    }
    
    return true;
}

// Poison memory with specific pattern
void corruption_poison_memory(void *ptr, size_t size, uint8_t poison_value) {
    if (!ptr || size == 0) {
        return;
    }
    
    uint8_t *buffer = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        buffer[i] = poison_value;
    }
}

// Verify poison pattern
bool corruption_verify_poison(void *ptr, size_t size, uint8_t poison_value) {
    if (!ptr || size == 0) {
        return false;
    }
    
    uint8_t *buffer = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        if (buffer[i] != poison_value) {
            return false;
        }
    }
    
    return true;
}

// Report corruption violation
void corruption_report_violation(const char *type, void *ptr, const char *details) {
    corruption_stats.corruption_detected++;
    
    // Use emergency reporting to avoid recursion
    corruption_emergency_report("*** MEMORY CORRUPTION DETECTED ***", 0);
    
    char msg[80];
    int pos = 0;
    
    // Build message
    const char *prefix = "Type: ";
    while (*prefix && pos < 70) {
        msg[pos++] = *prefix++;
    }
    
    while (*type && pos < 70) {
        msg[pos++] = *type++;
    }
    
    msg[pos] = '\0';
    corruption_emergency_report(msg, 1);
    
    // Additional details
    if (details) {
        pos = 0;
        prefix = "Context: ";
        while (*prefix && pos < 70) {
            msg[pos++] = *prefix++;
        }
        
        while (*details && pos < 70) {
            msg[pos++] = *details++;
        }
        
        msg[pos] = '\0';
        corruption_emergency_report(msg, 2);
    }
    
    corruption_emergency_report("System integrity compromised!", 3);
}

// Get corruption statistics
void corruption_get_stats(corruption_stats_t *stats) {
    if (stats) {
        *stats = corruption_stats;
    }
}

// Scan entire heap for corruption
bool corruption_scan_heap_corruption(void) {
    bool corruption_found = false;
    
    if (!corruption_detection_enabled) {
        return false;
    }
    
    for (int i = 0; i < CORRUPTION_MAX_ALLOCS; i++) {
        alloc_tracker_t *tracker = &allocation_table[i];
        
        if (tracker->magic == CORRUPTION_MAGIC_ALLOC && 
            tracker->state == ALLOC_STATE_ALLOCATED) {
            
            if (!corruption_validate_allocation(tracker->ptr)) {
                corruption_found = true;
                corruption_report_violation("Heap scan corruption", tracker->ptr, tracker->alloc_function);
            }
        }
    }
    
    return corruption_found;
}

// Dump all current allocations
void corruption_dump_all_allocations(void) {
    print("\n=== Memory Allocation Dump ===\n");
    
    int active_allocs = 0;
    for (int i = 0; i < CORRUPTION_MAX_ALLOCS; i++) {
        alloc_tracker_t *tracker = &allocation_table[i];
        
        if (tracker->magic == CORRUPTION_MAGIC_ALLOC && 
            tracker->state == ALLOC_STATE_ALLOCATED) {
            
            active_allocs++;
            print("Alloc #");
            print_hex(active_allocs);
            print(": ptr=0x");
            print_hex((uint32_t)tracker->ptr);
            print(", size=");
            print_hex(tracker->size);
            print(", function=");
            if (tracker->alloc_function) {
                print(tracker->alloc_function);
            } else {
                print("unknown");
            }
            print("\n");
        }
    }
    
    print("Total active allocations: ");
    print_hex(active_allocs);
    print("\n");
}
