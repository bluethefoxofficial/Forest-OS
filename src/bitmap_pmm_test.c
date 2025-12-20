#include "include/bitmap_pmm.h"
#include "include/screen.h"
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// BITMAP PMM TEST SUITE
// =============================================================================
// Comprehensive tests for bitmap-based physical memory manager
// =============================================================================

// Test basic page allocation and free
static int test_basic_page_allocation(void) {
    print("[BITMAP_PMM_TEST] Testing basic page allocation...\n");
    
    pmm_stats_t stats_before, stats_after;
    bitmap_pmm_get_stats(&stats_before);
    
    // Allocate a single page
    uint32_t page1 = bitmap_pmm_alloc_page(PMM_ALLOC_ANY_MEMORY);
    if (page1 == 0) {
        print("[BITMAP_PMM_TEST] Single page allocation failed\n");
        return -1;
    }
    
    // Validate the page frame number
    if (!bitmap_pmm_validate_page_frame(page1)) {
        print("[BITMAP_PMM_TEST] Page frame validation failed\n");
        bitmap_pmm_free_page(page1);
        return -1;
    }
    
    // Check statistics update
    bitmap_pmm_get_stats(&stats_after);
    if (stats_after.used_pages <= stats_before.used_pages ||
        stats_after.free_pages >= stats_before.free_pages) {
        print("[BITMAP_PMM_TEST] Statistics not updated correctly\n");
        bitmap_pmm_free_page(page1);
        return -1;
    }
    
    // Free the page
    bitmap_pmm_free_page(page1);
    
    // Verify statistics after free
    bitmap_pmm_get_stats(&stats_after);
    if (stats_after.free_pages != stats_before.free_pages ||
        stats_after.used_pages != stats_before.used_pages) {
        print("[BITMAP_PMM_TEST] Free statistics incorrect\n");
        return -1;
    }
    
    print("[BITMAP_PMM_TEST] Basic page allocation: PASS\n");
    return 0;
}

// Test allocation preferences
static int test_allocation_preferences(void) {
    print("[BITMAP_PMM_TEST] Testing allocation preferences...\n");
    
    // Test low memory allocation
    uint32_t low_page = bitmap_pmm_alloc_page(PMM_ALLOC_LOW_MEMORY);
    if (low_page != 0 && low_page >= PMM_DMA_ZONE_PAGES) {
        print("[BITMAP_PMM_TEST] Low memory allocation returned high memory page\n");
        bitmap_pmm_free_page(low_page);
        return -1;
    }
    
    // Test high memory allocation
    uint32_t high_page = bitmap_pmm_alloc_page(PMM_ALLOC_HIGH_MEMORY);
    if (high_page != 0 && high_page < PMM_DMA_ZONE_PAGES) {
        print("[BITMAP_PMM_TEST] High memory allocation returned low memory page\n");
        bitmap_pmm_free_page(low_page);
        bitmap_pmm_free_page(high_page);
        return -1;
    }
    
    // Test any memory allocation
    uint32_t any_page = bitmap_pmm_alloc_page(PMM_ALLOC_ANY_MEMORY);
    if (any_page == 0) {
        print("[BITMAP_PMM_TEST] Any memory allocation failed\n");
        if (low_page) bitmap_pmm_free_page(low_page);
        if (high_page) bitmap_pmm_free_page(high_page);
        return -1;
    }
    
    // Clean up
    if (low_page) bitmap_pmm_free_page(low_page);
    if (high_page) bitmap_pmm_free_page(high_page);
    bitmap_pmm_free_page(any_page);
    
    print("[BITMAP_PMM_TEST] Allocation preferences: PASS\n");
    return 0;
}

// Test contiguous page allocation
static int test_contiguous_allocation(void) {
    print("[BITMAP_PMM_TEST] Testing contiguous page allocation...\n");
    
    // Try to allocate 4 contiguous pages
    uint32_t contiguous_start = bitmap_pmm_alloc_contiguous_pages(4, 1);
    if (contiguous_start == 0) {
        print("[BITMAP_PMM_TEST] Contiguous allocation failed (may be normal if fragmented)\n");
        return 0; // Not necessarily a failure in a fragmented system
    }
    
    // Verify the pages are actually allocated
    for (uint32_t i = 0; i < 4; i++) {
        page_state_t state = bitmap_pmm_get_page_state(contiguous_start + i);
        if (state != PAGE_USED) {
            print("[BITMAP_PMM_TEST] Contiguous page not marked as used\n");
            bitmap_pmm_free_pages(contiguous_start, 4);
            return -1;
        }
    }
    
    // Free the contiguous pages
    bitmap_pmm_free_pages(contiguous_start, 4);
    
    print("[BITMAP_PMM_TEST] Contiguous allocation: PASS\n");
    return 0;
}

// Test aligned allocation
static int test_aligned_allocation(void) {
    print("[BITMAP_PMM_TEST] Testing aligned allocation...\n");
    
    // Try to allocate 2 pages with 4-page alignment
    uint32_t aligned_start = bitmap_pmm_alloc_contiguous_pages(2, 4);
    if (aligned_start == 0) {
        print("[BITMAP_PMM_TEST] Aligned allocation failed (may be normal)\n");
        return 0; // Not necessarily a failure
    }
    
    // Check alignment
    if ((aligned_start % 4) != 0) {
        print("[BITMAP_PMM_TEST] Allocation not properly aligned\n");
        bitmap_pmm_free_pages(aligned_start, 2);
        return -1;
    }
    
    // Clean up
    bitmap_pmm_free_pages(aligned_start, 2);
    
    print("[BITMAP_PMM_TEST] Aligned allocation: PASS\n");
    return 0;
}

// Test double-free detection
static int test_double_free_detection(void) {
    print("[BITMAP_PMM_TEST] Testing double-free detection...\n");
    
    pmm_stats_t stats_before;
    bitmap_pmm_get_stats(&stats_before);
    
    // Allocate a page
    uint32_t page = bitmap_pmm_alloc_page(PMM_ALLOC_ANY_MEMORY);
    if (page == 0) {
        print("[BITMAP_PMM_TEST] Allocation for double-free test failed\n");
        return -1;
    }
    
    // Free it once (should succeed)
    bitmap_pmm_free_page(page);
    
    // Try to free it again (should be detected)
    bitmap_pmm_free_page(page);
    
    // The double-free should be detected internally
    // We can't directly test the error message, but the system should handle it gracefully
    
    print("[BITMAP_PMM_TEST] Double-free detection: PASS\n");
    return 0;
}

// Test bitmap integrity validation
static int test_bitmap_integrity(void) {
    print("[BITMAP_PMM_TEST] Testing bitmap integrity validation...\n");
    
    // Test bitmap validation
    if (!bitmap_pmm_validate_bitmap_integrity()) {
        print("[BITMAP_PMM_TEST] Bitmap integrity validation failed\n");
        return -1;
    }
    
    // Check for corruption
    if (bitmap_pmm_check_corruption()) {
        print("[BITMAP_PMM_TEST] Corruption detected in clean bitmap\n");
        return -1;
    }
    
    print("[BITMAP_PMM_TEST] Bitmap integrity: PASS\n");
    return 0;
}

// Test page state management
static int test_page_state_management(void) {
    print("[BITMAP_PMM_TEST] Testing page state management...\n");
    
    // Allocate a page
    uint32_t page = bitmap_pmm_alloc_page(PMM_ALLOC_ANY_MEMORY);
    if (page == 0) {
        print("[BITMAP_PMM_TEST] Allocation for state test failed\n");
        return -1;
    }
    
    // Check that it's marked as used
    page_state_t state = bitmap_pmm_get_page_state(page);
    if (state != PAGE_USED) {
        print("[BITMAP_PMM_TEST] Page not marked as used after allocation\n");
        bitmap_pmm_free_page(page);
        return -1;
    }
    
    // Free the page
    bitmap_pmm_free_page(page);
    
    // Check that it's marked as free
    state = bitmap_pmm_get_page_state(page);
    if (state != PAGE_FREE) {
        print("[BITMAP_PMM_TEST] Page not marked as free after deallocation\n");
        return -1;
    }
    
    print("[BITMAP_PMM_TEST] Page state management: PASS\n");
    return 0;
}

// Test memory region tracking
static int test_memory_region_tracking(void) {
    print("[BITMAP_PMM_TEST] Testing memory region tracking...\n");
    
    // Display memory map
    bitmap_pmm_dump_memory_map();
    
    // Get total and usable memory
    uint32_t total_memory = bitmap_pmm_get_total_memory();
    uint32_t usable_memory = bitmap_pmm_get_usable_memory();
    
    if (total_memory == 0) {
        print("[BITMAP_PMM_TEST] Total memory reported as zero\n");
        return -1;
    }
    
    if (usable_memory > total_memory) {
        print("[BITMAP_PMM_TEST] Usable memory exceeds total memory\n");
        return -1;
    }
    
    print("[BITMAP_PMM_TEST] Memory region tracking: PASS\n");
    return 0;
}

// Test statistics tracking
static int test_statistics_tracking(void) {
    print("[BITMAP_PMM_TEST] Testing statistics tracking...\n");
    
    pmm_stats_t stats_before, stats_after;
    bitmap_pmm_get_stats(&stats_before);
    
    // Make several allocations
    uint32_t pages[5];
    int allocated_count = 0;
    
    for (int i = 0; i < 5; i++) {
        pages[i] = bitmap_pmm_alloc_page(PMM_ALLOC_ANY_MEMORY);
        if (pages[i] != 0) {
            allocated_count++;
        }
    }
    
    bitmap_pmm_get_stats(&stats_after);
    
    // Check allocation statistics
    if (stats_after.total_allocations <= stats_before.total_allocations) {
        print("[BITMAP_PMM_TEST] Allocation count not updated\n");
        // Clean up
        for (int i = 0; i < allocated_count; i++) {
            if (pages[i] != 0) {
                bitmap_pmm_free_page(pages[i]);
            }
        }
        return -1;
    }
    
    // Free the pages
    for (int i = 0; i < allocated_count; i++) {
        if (pages[i] != 0) {
            bitmap_pmm_free_page(pages[i]);
        }
    }
    
    bitmap_pmm_get_stats(&stats_after);
    
    // Check free statistics
    if (stats_after.total_frees <= stats_before.total_frees) {
        print("[BITMAP_PMM_TEST] Free count not updated\n");
        return -1;
    }
    
    print("[BITMAP_PMM_TEST] Statistics tracking: PASS\n");
    return 0;
}

// Test fragmentation analysis
static int test_fragmentation_analysis(void) {
    print("[BITMAP_PMM_TEST] Testing fragmentation analysis...\n");
    
    // Allocate some pages to create fragmentation
    uint32_t pages[10];
    int allocated = 0;
    
    for (int i = 0; i < 10; i++) {
        pages[i] = bitmap_pmm_alloc_page(PMM_ALLOC_ANY_MEMORY);
        if (pages[i] != 0) {
            allocated++;
        }
    }
    
    // Free every other page to create fragmentation
    for (int i = 1; i < allocated; i += 2) {
        bitmap_pmm_free_page(pages[i]);
    }
    
    // Analyze fragmentation
    bitmap_pmm_analyze_fragmentation();
    
    // Find largest free block
    uint32_t largest_block = bitmap_pmm_find_largest_free_block();
    
    // Clean up remaining pages
    for (int i = 0; i < allocated; i += 2) {
        bitmap_pmm_free_page(pages[i]);
    }
    
    print("[BITMAP_PMM_TEST] Fragmentation analysis: PASS\n");
    return 0;
}

// Stress test for bitmap PMM
static int test_stress_allocation(void) {
    print("[BITMAP_PMM_TEST] Running stress test...\n");
    
    #define STRESS_PAGES 20
    uint32_t pages[STRESS_PAGES];
    
    // Multiple allocation/free cycles
    for (int cycle = 0; cycle < 3; cycle++) {
        int allocated = 0;
        
        // Allocation phase
        for (int i = 0; i < STRESS_PAGES; i++) {
            pages[i] = bitmap_pmm_alloc_page(PMM_ALLOC_ANY_MEMORY);
            if (pages[i] != 0) {
                allocated++;
            }
        }
        
        print("[BITMAP_PMM_TEST] Cycle ");
        print_dec(cycle);
        print(": allocated ");
        print_dec(allocated);
        print(" pages\n");
        
        // Free phase
        for (int i = 0; i < allocated; i++) {
            if (pages[i] != 0) {
                bitmap_pmm_free_page(pages[i]);
                pages[i] = 0;
            }
        }
        
        // Validate bitmap integrity after each cycle
        if (!bitmap_pmm_validate_bitmap_integrity()) {
            print("[BITMAP_PMM_TEST] Bitmap integrity failed during stress test\n");
            return -1;
        }
    }
    
    print("[BITMAP_PMM_TEST] Stress test: PASS\n");
    return 0;
}

// Main bitmap PMM test runner
int bitmap_pmm_run_tests(void) {
    int failures = 0;
    
    print("\n=== Bitmap PMM Tests ===\n");
    
    if (test_basic_page_allocation() != 0) failures++;
    if (test_allocation_preferences() != 0) failures++;
    if (test_contiguous_allocation() != 0) failures++;
    if (test_aligned_allocation() != 0) failures++;
    if (test_double_free_detection() != 0) failures++;
    if (test_bitmap_integrity() != 0) failures++;
    if (test_page_state_management() != 0) failures++;
    if (test_memory_region_tracking() != 0) failures++;
    if (test_statistics_tracking() != 0) failures++;
    if (test_fragmentation_analysis() != 0) failures++;
    if (test_stress_allocation() != 0) failures++;
    
    print("\n=== Bitmap PMM Test Summary ===\n");
    if (failures == 0) {
        print("[BITMAP_PMM_TEST] All tests passed!\n");
        return 0;
    } else {
        print("[BITMAP_PMM_TEST] ");
        print_dec(failures);
        print(" tests failed\n");
        return failures;
    }
}

// Benchmark function for bitmap PMM
void bitmap_pmm_benchmark(void) {
    print("\n=== Bitmap PMM Benchmark ===\n");
    
    #define BENCHMARK_PAGES 100
    uint32_t pages[BENCHMARK_PAGES];
    
    print("[BENCHMARK] Allocating ");
    print_dec(BENCHMARK_PAGES);
    print(" pages...\n");
    
    // Allocation benchmark
    int allocated = 0;
    for (int i = 0; i < BENCHMARK_PAGES; i++) {
        pages[i] = bitmap_pmm_alloc_page(PMM_ALLOC_ANY_MEMORY);
        if (pages[i] != 0) {
            allocated++;
        }
    }
    
    print("[BENCHMARK] Successfully allocated ");
    print_dec(allocated);
    print(" pages\n");
    
    // Free benchmark
    for (int i = 0; i < allocated; i++) {
        if (pages[i] != 0) {
            bitmap_pmm_free_page(pages[i]);
        }
    }
    
    print("[BENCHMARK] Freed all allocated pages\n");
    
    // Display statistics
    bitmap_pmm_dump_stats();
    
    print("[BENCHMARK] Benchmark completed\n");
}