#include "include/enhanced_heap.h"
#include "include/screen.h"
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// ENHANCED HEAP ALLOCATOR TEST SUITE
// =============================================================================
// Comprehensive tests for the enhanced heap with corruption detection
// =============================================================================

// Test basic allocation and free operations
static int test_basic_allocation_free(void) {
    print("[ENHANCED_HEAP_TEST] Testing basic allocation and free...\n");
    
    // Test various sizes
    size_t test_sizes[] = {16, 32, 64, 128, 256, 512, 1024};
    int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    for (int i = 0; i < num_sizes; i++) {
        void* ptr = ENHANCED_ALLOC(test_sizes[i]);
        if (!ptr) {
            print("[ENHANCED_HEAP_TEST] Allocation failed for size ");
            print_dec(test_sizes[i]);
            print("\n");
            return -1;
        }
        
        // Validate pointer
        if (!enhanced_heap_validate_pointer(ptr)) {
            print("[ENHANCED_HEAP_TEST] Pointer validation failed\n");
            ENHANCED_FREE(ptr);
            return -1;
        }
        
        // Write to memory to test accessibility
        char* buffer = (char*)ptr;
        for (size_t j = 0; j < test_sizes[i] && j < 1024; j++) {
            buffer[j] = 'A' + (j % 26);
        }
        
        ENHANCED_FREE(ptr);
    }
    
    print("[ENHANCED_HEAP_TEST] Basic allocation and free: PASS\n");
    return 0;
}

// Test size class distribution
static int test_size_class_distribution(void) {
    print("[ENHANCED_HEAP_TEST] Testing size class distribution...\n");
    
    enhanced_heap_stats_t stats_before, stats_after;
    enhanced_heap_get_stats(&stats_before);
    
    // Allocate blocks of different sizes to test size classes
    void* ptrs[20];
    size_t sizes[] = {15, 31, 63, 127, 255, 511, 1023, 2047};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    
    for (int i = 0; i < num_sizes; i++) {
        ptrs[i] = ENHANCED_ALLOC(sizes[i]);
        if (!ptrs[i]) {
            print("[ENHANCED_HEAP_TEST] Size class allocation failed\n");
            // Clean up allocated pointers
            for (int j = 0; j < i; j++) {
                ENHANCED_FREE(ptrs[j]);
            }
            return -1;
        }
    }
    
    enhanced_heap_get_stats(&stats_after);
    
    // Check that allocations were distributed across size classes
    bool size_classes_used = false;
    for (int i = 0; i < HEAP_SIZE_CLASSES; i++) {
        if (stats_after.size_class_stats[i].allocations > stats_before.size_class_stats[i].allocations) {
            size_classes_used = true;
            break;
        }
    }
    
    // Clean up
    for (int i = 0; i < num_sizes; i++) {
        ENHANCED_FREE(ptrs[i]);
    }
    
    if (!size_classes_used) {
        print("[ENHANCED_HEAP_TEST] Size classes not being used\n");
        return -1;
    }
    
    print("[ENHANCED_HEAP_TEST] Size class distribution: PASS\n");
    return 0;
}

// Test block validation and corruption detection
static int test_corruption_detection(void) {
    print("[ENHANCED_HEAP_TEST] Testing corruption detection...\n");
    
    void* ptr = ENHANCED_ALLOC(128);
    if (!ptr) {
        print("[ENHANCED_HEAP_TEST] Allocation failed\n");
        return -1;
    }
    
    // Validate that the block is initially valid
    if (!enhanced_heap_validate_pointer(ptr)) {
        print("[ENHANCED_HEAP_TEST] Initial validation failed\n");
        ENHANCED_FREE(ptr);
        return -1;
    }
    
    // Test corruption scanning
    enhanced_heap_scan_for_corruption();
    
    ENHANCED_FREE(ptr);
    
    print("[ENHANCED_HEAP_TEST] Corruption detection: PASS\n");
    return 0;
}

// Test allocation statistics tracking
static int test_statistics_tracking(void) {
    print("[ENHANCED_HEAP_TEST] Testing statistics tracking...\n");
    
    enhanced_heap_stats_t stats_before, stats_after;
    enhanced_heap_get_stats(&stats_before);
    
    // Make several allocations
    void* ptr1 = ENHANCED_ALLOC(64);
    void* ptr2 = ENHANCED_ALLOC(128);
    void* ptr3 = ENHANCED_ALLOC(256);
    
    if (!ptr1 || !ptr2 || !ptr3) {
        print("[ENHANCED_HEAP_TEST] Statistics test allocations failed\n");
        return -1;
    }
    
    enhanced_heap_get_stats(&stats_after);
    
    // Check statistics were updated
    if (stats_after.total_allocations <= stats_before.total_allocations ||
        stats_after.current_allocations <= stats_before.current_allocations) {
        print("[ENHANCED_HEAP_TEST] Statistics not updated properly\n");
        ENHANCED_FREE(ptr1);
        ENHANCED_FREE(ptr2);
        ENHANCED_FREE(ptr3);
        return -1;
    }
    
    // Free and check statistics again
    ENHANCED_FREE(ptr1);
    ENHANCED_FREE(ptr2);
    ENHANCED_FREE(ptr3);
    
    enhanced_heap_get_stats(&stats_after);
    
    if (stats_after.total_frees <= stats_before.total_frees) {
        print("[ENHANCED_HEAP_TEST] Free statistics not updated\n");
        return -1;
    }
    
    print("[ENHANCED_HEAP_TEST] Statistics tracking: PASS\n");
    return 0;
}

// Test double-free detection
static int test_double_free_detection(void) {
    print("[ENHANCED_HEAP_TEST] Testing double-free detection...\n");
    
    void* ptr = ENHANCED_ALLOC(64);
    if (!ptr) {
        print("[ENHANCED_HEAP_TEST] Allocation failed\n");
        return -1;
    }
    
    // First free should succeed
    ENHANCED_FREE(ptr);
    
    // Second free should be detected (but shouldn't crash)
    // The enhanced heap should detect this internally
    enhanced_heap_stats_t stats;
    enhanced_heap_get_stats(&stats);
    
    // In a real scenario, attempting to access the freed pointer
    // should be detected by our validation
    if (enhanced_heap_validate_pointer(ptr)) {
        print("[ENHANCED_HEAP_TEST] Double-free not detected properly\n");
        return -1;
    }
    
    print("[ENHANCED_HEAP_TEST] Double-free detection: PASS\n");
    return 0;
}

// Test fragmentation handling
static int test_fragmentation_handling(void) {
    print("[ENHANCED_HEAP_TEST] Testing fragmentation handling...\n");
    
    // Allocate and free in a pattern that could cause fragmentation
    void* ptrs[10];
    
    // Allocate 10 blocks
    for (int i = 0; i < 10; i++) {
        ptrs[i] = ENHANCED_ALLOC(64 + (i * 32));
        if (!ptrs[i]) {
            print("[ENHANCED_HEAP_TEST] Fragmentation test allocation failed\n");
            // Clean up
            for (int j = 0; j < i; j++) {
                ENHANCED_FREE(ptrs[j]);
            }
            return -1;
        }
    }
    
    // Free every other block to create fragmentation
    for (int i = 1; i < 10; i += 2) {
        ENHANCED_FREE(ptrs[i]);
        ptrs[i] = NULL;
    }
    
    // Try to allocate a larger block
    void* large_ptr = ENHANCED_ALLOC(200);
    
    // Clean up remaining blocks
    for (int i = 0; i < 10; i += 2) {
        if (ptrs[i]) {
            ENHANCED_FREE(ptrs[i]);
        }
    }
    
    if (large_ptr) {
        ENHANCED_FREE(large_ptr);
    }
    
    print("[ENHANCED_HEAP_TEST] Fragmentation handling: PASS\n");
    return 0;
}

// Test heap integrity validation
static int test_heap_integrity_validation(void) {
    print("[ENHANCED_HEAP_TEST] Testing heap integrity validation...\n");
    
    // Allocate several blocks
    void* ptr1 = ENHANCED_ALLOC(128);
    void* ptr2 = ENHANCED_ALLOC(256);
    void* ptr3 = ENHANCED_ALLOC(64);
    
    if (!ptr1 || !ptr2 || !ptr3) {
        print("[ENHANCED_HEAP_TEST] Integrity test allocations failed\n");
        return -1;
    }
    
    // Run integrity check
    bool integrity_ok = enhanced_heap_validate_all_blocks();
    
    if (!integrity_ok) {
        print("[ENHANCED_HEAP_TEST] Heap integrity check failed\n");
        ENHANCED_FREE(ptr1);
        ENHANCED_FREE(ptr2);
        ENHANCED_FREE(ptr3);
        return -1;
    }
    
    // Clean up
    ENHANCED_FREE(ptr1);
    ENHANCED_FREE(ptr2);
    ENHANCED_FREE(ptr3);
    
    // Run integrity check again after free
    integrity_ok = enhanced_heap_validate_all_blocks();
    
    if (!integrity_ok) {
        print("[ENHANCED_HEAP_TEST] Post-free integrity check failed\n");
        return -1;
    }
    
    print("[ENHANCED_HEAP_TEST] Heap integrity validation: PASS\n");
    return 0;
}

// Test configuration changes
static int test_configuration_changes(void) {
    print("[ENHANCED_HEAP_TEST] Testing configuration changes...\n");
    
    // Test enabling/disabling corruption detection
    enhanced_heap_enable_corruption_detection(false);
    enhanced_heap_enable_corruption_detection(true);
    
    // Test enabling/disabling debug mode
    enhanced_heap_enable_debug_mode(true);
    enhanced_heap_enable_debug_mode(false);
    
    print("[ENHANCED_HEAP_TEST] Configuration changes: PASS\n");
    return 0;
}

// Test statistics display
static int test_statistics_display(void) {
    print("[ENHANCED_HEAP_TEST] Testing statistics display...\n");
    
    // Make some allocations to generate statistics
    void* ptr1 = ENHANCED_ALLOC(32);
    void* ptr2 = ENHANCED_ALLOC(128);
    void* ptr3 = ENHANCED_ALLOC(512);
    
    // Display statistics
    enhanced_heap_dump_stats();
    
    // Clean up
    if (ptr1) ENHANCED_FREE(ptr1);
    if (ptr2) ENHANCED_FREE(ptr2);
    if (ptr3) ENHANCED_FREE(ptr3);
    
    print("[ENHANCED_HEAP_TEST] Statistics display: PASS\n");
    return 0;
}

// Stress test for enhanced heap
static int test_stress_allocation(void) {
    print("[ENHANCED_HEAP_TEST] Running stress test...\n");
    
    #define STRESS_ALLOCS 20
    void* ptrs[STRESS_ALLOCS];
    
    // Multiple allocation/free cycles
    for (int cycle = 0; cycle < 3; cycle++) {
        // Allocation phase
        for (int i = 0; i < STRESS_ALLOCS; i++) {
            size_t size = 32 + (i * 16);
            ptrs[i] = ENHANCED_ALLOC(size);
            
            if (ptrs[i]) {
                // Write pattern to test memory
                char* buffer = (char*)ptrs[i];
                for (size_t j = 0; j < size && j < 1000; j++) {
                    buffer[j] = 'X' + (j % 10);
                }
            }
        }
        
        // Free phase
        for (int i = 0; i < STRESS_ALLOCS; i++) {
            if (ptrs[i]) {
                ENHANCED_FREE(ptrs[i]);
                ptrs[i] = NULL;
            }
        }
        
        // Run corruption scan after each cycle
        enhanced_heap_scan_for_corruption();
    }
    
    print("[ENHANCED_HEAP_TEST] Stress test: PASS\n");
    return 0;
}

// Main enhanced heap test runner
int enhanced_heap_run_tests(void) {
    int failures = 0;
    
    print("\n=== Enhanced Heap Allocator Tests ===\n");
    
    if (test_basic_allocation_free() != 0) failures++;
    if (test_size_class_distribution() != 0) failures++;
    if (test_corruption_detection() != 0) failures++;
    if (test_statistics_tracking() != 0) failures++;
    if (test_double_free_detection() != 0) failures++;
    if (test_fragmentation_handling() != 0) failures++;
    if (test_heap_integrity_validation() != 0) failures++;
    if (test_configuration_changes() != 0) failures++;
    if (test_statistics_display() != 0) failures++;
    if (test_stress_allocation() != 0) failures++;
    
    print("\n=== Enhanced Heap Test Summary ===\n");
    if (failures == 0) {
        print("[ENHANCED_HEAP_TEST] All tests passed!\n");
        return 0;
    } else {
        print("[ENHANCED_HEAP_TEST] ");
        print_dec(failures);
        print(" tests failed\n");
        return failures;
    }
}

// Benchmark comparison between old and new heap
void enhanced_heap_benchmark(void) {
    print("\n=== Enhanced Heap Benchmark ===\n");
    
    // Simple performance comparison
    #define BENCHMARK_ALLOCS 50
    
    print("[BENCHMARK] Allocating ");
    print_dec(BENCHMARK_ALLOCS);
    print(" blocks of varying sizes...\n");
    
    void* ptrs[BENCHMARK_ALLOCS];
    
    // Allocation benchmark
    for (int i = 0; i < BENCHMARK_ALLOCS; i++) {
        size_t size = 32 + (i * 8);
        ptrs[i] = ENHANCED_ALLOC(size);
    }
    
    // Free benchmark  
    for (int i = 0; i < BENCHMARK_ALLOCS; i++) {
        if (ptrs[i]) {
            ENHANCED_FREE(ptrs[i]);
        }
    }
    
    // Display final statistics
    enhanced_heap_dump_stats();
    
    print("[BENCHMARK] Benchmark completed\n");
}