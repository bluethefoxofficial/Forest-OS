#include "include/bitmap_pmm.h"
#include "include/screen.h"
#include "include/memory.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// BITMAP-BASED PHYSICAL MEMORY MANAGER IMPLEMENTATION
// =============================================================================
// Efficient and corruption-resistant physical page frame allocator
// =============================================================================

// Global state
static struct {
    bool initialized;
    pmm_config_t config;
    
    // Main bitmap for page tracking
    uint32_t bitmap[PMM_BITMAP_SIZE];
    bitmap_metadata_t metadata;
    
    // Memory region tracking
    memory_region_t regions[32];
    uint32_t region_count;
    
    // Statistics
    pmm_stats_t stats;
    
    // Allocation hints for efficiency
    uint32_t last_alloc_low;
    uint32_t last_alloc_high;
    
} pmm_state = {0};

// Emergency reporting for critical errors
static void pmm_emergency_report(const char* msg, uint32_t data) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    int row = 6; // Use row 6 for PMM errors
    int col = 0;
    
    while (*msg && col < 80) {
        vga[row * 80 + col] = (0x4C << 8) | *msg; // Red on light red
        msg++;
        col++;
    }
}

// Calculate bitmap checksum for corruption detection
static uint32_t calculate_bitmap_checksum(void) {
    uint32_t checksum = 0;
    
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        checksum ^= pmm_state.bitmap[i];
        checksum = (checksum << 1) | (checksum >> 31); // Rotate left
    }
    
    checksum ^= pmm_state.metadata.total_pages;
    checksum ^= pmm_state.metadata.free_pages;
    
    return checksum;
}

// Set bit in bitmap (mark page as used)
static void bitmap_set_bit(uint32_t page_frame) {
    uint32_t bitmap_index = bitmap_pmm_get_bitmap_index(page_frame);
    uint32_t bit_offset = bitmap_pmm_get_bit_offset(page_frame);
    
    if (bitmap_index < PMM_BITMAP_SIZE) {
        pmm_state.bitmap[bitmap_index] |= (1U << bit_offset);
    }
}

// Clear bit in bitmap (mark page as free)
static void bitmap_clear_bit(uint32_t page_frame) {
    uint32_t bitmap_index = bitmap_pmm_get_bitmap_index(page_frame);
    uint32_t bit_offset = bitmap_pmm_get_bit_offset(page_frame);
    
    if (bitmap_index < PMM_BITMAP_SIZE) {
        pmm_state.bitmap[bitmap_index] &= ~(1U << bit_offset);
    }
}

// Test bit in bitmap (check if page is used)
static bool bitmap_test_bit(uint32_t page_frame) {
    uint32_t bitmap_index = bitmap_pmm_get_bitmap_index(page_frame);
    uint32_t bit_offset = bitmap_pmm_get_bit_offset(page_frame);
    
    if (bitmap_index >= PMM_BITMAP_SIZE) {
        return true; // Treat out-of-range as used
    }
    
    return (pmm_state.bitmap[bitmap_index] & (1U << bit_offset)) != 0;
}

// Find first free page starting from hint
static uint32_t find_free_page(uint32_t start_hint, uint32_t max_pages) {
    uint32_t start_bitmap = bitmap_pmm_get_bitmap_index(start_hint);
    
    // Search bitmap entries
    for (uint32_t i = start_bitmap; i < PMM_BITMAP_SIZE; i++) {
        if (pmm_state.bitmap[i] != 0xFFFFFFFF) { // Has at least one free bit
            // Find the specific free bit
            for (uint32_t bit = 0; bit < PMM_PAGES_PER_BITMAP; bit++) {
                uint32_t page_frame = i * PMM_PAGES_PER_BITMAP + bit;
                
                if (page_frame >= max_pages) {
                    return 0; // No more pages to search
                }
                
                if (!bitmap_test_bit(page_frame)) {
                    return page_frame;
                }
            }
        }
    }
    
    return 0; // No free page found
}

// Find contiguous free pages
static uint32_t find_contiguous_pages(uint32_t count, uint32_t alignment, uint32_t max_pages) {
    uint32_t current_run = 0;
    uint32_t start_page = 0;
    
    for (uint32_t page = 0; page < max_pages; page++) {
        if (!bitmap_test_bit(page)) {
            if (current_run == 0) {
                // Check alignment
                if (alignment > 1 && (page % alignment) != 0) {
                    continue;
                }
                start_page = page;
            }
            
            current_run++;
            
            if (current_run >= count) {
                return start_page;
            }
        } else {
            current_run = 0;
        }
    }
    
    return 0; // Not enough contiguous pages found
}

// Initialize bitmap PMM
void bitmap_pmm_init(const pmm_config_t* config) {
    print("[BITMAP_PMM] Initializing bitmap-based physical memory manager...\n");
    
    // Set configuration
    if (config) {
        pmm_state.config = *config;
    } else {
        // Default configuration
        pmm_state.config.corruption_detection_enabled = true;
        pmm_state.config.defragmentation_enabled = true;
        pmm_state.config.statistics_tracking_enabled = true;
        pmm_state.config.debug_mode_enabled = false;
        pmm_state.config.reserved_pages_low = 256;   // Reserve 1MB in low memory
        pmm_state.config.reserved_pages_high = 256;  // Reserve 1MB in high memory
    }
    
    // Initialize bitmap to all used (safe default)
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        pmm_state.bitmap[i] = 0xFFFFFFFF;
    }
    
    // Initialize metadata
    pmm_state.metadata.magic_header = PMM_MAGIC_HEADER;
    pmm_state.metadata.magic_footer = PMM_MAGIC_FOOTER;
    pmm_state.metadata.total_pages = 0;
    pmm_state.metadata.free_pages = 0;
    pmm_state.metadata.last_alloc_hint = 0;
    
    // Clear statistics
    pmm_state.stats.total_pages = 0;
    pmm_state.stats.free_pages = 0;
    pmm_state.stats.used_pages = 0;
    pmm_state.stats.reserved_pages = 0;
    pmm_state.stats.dma_pages = 0;
    pmm_state.stats.corrupted_pages = 0;
    pmm_state.stats.total_allocations = 0;
    pmm_state.stats.total_frees = 0;
    pmm_state.stats.allocation_failures = 0;
    
    pmm_state.region_count = 0;
    pmm_state.last_alloc_low = 0;
    pmm_state.last_alloc_high = PMM_DMA_ZONE_PAGES;
    
    print("[BITMAP_PMM] Initialized with corruption detection ");
    print(pmm_state.config.corruption_detection_enabled ? "enabled" : "disabled");
    print("\n");
}

// Add memory region to the allocator
void bitmap_pmm_add_memory_region(uint64_t base, uint64_t length, memory_region_type_t type) {
    if (pmm_state.region_count >= 32) {
        pmm_emergency_report("Too many memory regions", pmm_state.region_count);
        return;
    }
    
    memory_region_t* region = &pmm_state.regions[pmm_state.region_count++];
    region->base_addr = base;
    region->length = length;
    region->type = type;
    
    // Set description based on type
    switch (type) {
        case MEMORY_TYPE_AVAILABLE:
            region->description = "Available RAM";
            break;
        case MEMORY_TYPE_RESERVED:
            region->description = "Reserved";
            break;
        case MEMORY_TYPE_ACPI_RECLAIM:
            region->description = "ACPI Reclaimable";
            break;
        case MEMORY_TYPE_ACPI_NVS:
            region->description = "ACPI NVS";
            break;
        case MEMORY_TYPE_BAD:
            region->description = "Bad Memory";
            break;
        case MEMORY_TYPE_KERNEL:
            region->description = "Kernel Code/Data";
            break;
        case MEMORY_TYPE_INITRD:
            region->description = "Initial Ramdisk";
            break;
        default:
            region->description = "Unknown";
            break;
    }
    
    uint32_t start_page = (uint32_t)(base / PMM_PAGE_SIZE);
    uint32_t page_count = (uint32_t)(length / PMM_PAGE_SIZE);
    
    print("[BITMAP_PMM] Added region: 0x");
    print_hex((uint32_t)base);
    print(" - 0x");
    print_hex((uint32_t)(base + length));
    print(" (");
    print(region->description);
    print(", ");
    print_dec(page_count);
    print(" pages)\n");
    
    // Mark pages based on type
    if (type == MEMORY_TYPE_AVAILABLE) {
        // Mark as free
        for (uint32_t page = start_page; page < start_page + page_count; page++) {
            if (page < PMM_MAX_PAGES) {
                bitmap_clear_bit(page);
                pmm_state.stats.free_pages++;
                pmm_state.metadata.free_pages++;
            }
        }
        pmm_state.stats.total_pages += page_count;
        pmm_state.metadata.total_pages += page_count;
    } else {
        // Mark as reserved/used
        for (uint32_t page = start_page; page < start_page + page_count; page++) {
            if (page < PMM_MAX_PAGES) {
                bitmap_set_bit(page);
                if (type == MEMORY_TYPE_RESERVED) {
                    pmm_state.stats.reserved_pages++;
                } else {
                    pmm_state.stats.used_pages++;
                }
            }
        }
        pmm_state.stats.total_pages += page_count;
        pmm_state.metadata.total_pages += page_count;
    }
}

// Finalize initialization and update checksums
void bitmap_pmm_finalize_initialization(void) {
    print("[BITMAP_PMM] Finalizing initialization...\n");
    
    // Calculate initial checksum
    pmm_state.metadata.checksum = calculate_bitmap_checksum();
    
    pmm_state.initialized = true;
    
    print("[BITMAP_PMM] Physical memory manager ready\n");
    print("  Total pages: ");
    print_dec(pmm_state.stats.total_pages);
    print("\n");
    print("  Free pages: ");
    print_dec(pmm_state.stats.free_pages);
    print("\n");
    print("  Reserved pages: ");
    print_dec(pmm_state.stats.reserved_pages);
    print("\n");
}

// Allocate a single page
uint32_t bitmap_pmm_alloc_page(pmm_alloc_preference_t preference) {
    if (!pmm_state.initialized) {
        return 0;
    }
    
    uint32_t page_frame = 0;
    uint32_t search_start = 0;
    uint32_t search_limit = pmm_state.metadata.total_pages;
    
    // Set search parameters based on preference
    switch (preference) {
        case PMM_ALLOC_LOW_MEMORY:
            search_start = pmm_state.last_alloc_low;
            search_limit = PMM_DMA_ZONE_PAGES;
            break;
            
        case PMM_ALLOC_HIGH_MEMORY:
            search_start = pmm_state.last_alloc_high;
            if (search_start < PMM_DMA_ZONE_PAGES) {
                search_start = PMM_DMA_ZONE_PAGES;
            }
            break;
            
        case PMM_ALLOC_ANY_MEMORY:
            search_start = pmm_state.metadata.last_alloc_hint;
            break;
    }
    
    // Find free page
    page_frame = find_free_page(search_start, search_limit);
    
    // If not found and searching in limited range, try full search
    if (page_frame == 0 && preference != PMM_ALLOC_ANY_MEMORY) {
        page_frame = find_free_page(0, pmm_state.metadata.total_pages);
    }
    
    if (page_frame == 0) {
        pmm_state.stats.allocation_failures++;
        return 0; // No free page available
    }
    
    // Mark page as used
    bitmap_set_bit(page_frame);
    
    // Update statistics
    pmm_state.stats.free_pages--;
    pmm_state.stats.used_pages++;
    pmm_state.stats.total_allocations++;
    pmm_state.metadata.free_pages--;
    
    // Update allocation hints
    pmm_state.metadata.last_alloc_hint = page_frame + 1;
    if (preference == PMM_ALLOC_LOW_MEMORY) {
        pmm_state.last_alloc_low = page_frame + 1;
    } else if (preference == PMM_ALLOC_HIGH_MEMORY) {
        pmm_state.last_alloc_high = page_frame + 1;
    }
    
    // Update checksum if corruption detection enabled
    if (pmm_state.config.corruption_detection_enabled) {
        pmm_state.metadata.checksum = calculate_bitmap_checksum();
    }
    
    return page_frame;
}

// Allocate multiple pages (not necessarily contiguous)
uint32_t bitmap_pmm_alloc_pages(uint32_t count, pmm_alloc_preference_t preference) {
    if (count == 0 || !pmm_state.initialized) {
        return 0;
    }
    
    if (count == 1) {
        return bitmap_pmm_alloc_page(preference);
    }
    
    // For multiple pages, try to find them contiguously first
    uint32_t contiguous_start = find_contiguous_pages(count, 1, pmm_state.metadata.total_pages);
    
    if (contiguous_start != 0) {
        // Mark all pages as used
        for (uint32_t i = 0; i < count; i++) {
            bitmap_set_bit(contiguous_start + i);
        }
        
        // Update statistics
        pmm_state.stats.free_pages -= count;
        pmm_state.stats.used_pages += count;
        pmm_state.stats.total_allocations++;
        pmm_state.metadata.free_pages -= count;
        
        // Update checksum
        if (pmm_state.config.corruption_detection_enabled) {
            pmm_state.metadata.checksum = calculate_bitmap_checksum();
        }
        
        return contiguous_start;
    }
    
    // If contiguous allocation failed, fall back to individual allocations
    // This is a simplified approach - a real implementation might return
    // an array of page frames or use a different allocation strategy
    pmm_state.stats.allocation_failures++;
    return 0;
}

// Allocate contiguous pages with alignment
uint32_t bitmap_pmm_alloc_contiguous_pages(uint32_t count, uint32_t alignment) {
    if (count == 0 || !pmm_state.initialized) {
        return 0;
    }
    
    uint32_t start_page = find_contiguous_pages(count, alignment, pmm_state.metadata.total_pages);
    
    if (start_page == 0) {
        pmm_state.stats.allocation_failures++;
        return 0;
    }
    
    // Mark all pages as used
    for (uint32_t i = 0; i < count; i++) {
        bitmap_set_bit(start_page + i);
    }
    
    // Update statistics
    pmm_state.stats.free_pages -= count;
    pmm_state.stats.used_pages += count;
    pmm_state.stats.total_allocations++;
    pmm_state.metadata.free_pages -= count;
    
    // Update checksum
    if (pmm_state.config.corruption_detection_enabled) {
        pmm_state.metadata.checksum = calculate_bitmap_checksum();
    }
    
    return start_page;
}

// Free a single page
void bitmap_pmm_free_page(uint32_t page_frame) {
    if (!pmm_state.initialized || page_frame >= pmm_state.metadata.total_pages) {
        return;
    }
    
    // Check if page was actually allocated
    if (!bitmap_test_bit(page_frame)) {
        pmm_emergency_report("Double free detected", page_frame);
        return;
    }
    
    // Mark page as free
    bitmap_clear_bit(page_frame);
    
    // Update statistics
    pmm_state.stats.free_pages++;
    pmm_state.stats.used_pages--;
    pmm_state.stats.total_frees++;
    pmm_state.metadata.free_pages++;
    
    // Update checksum
    if (pmm_state.config.corruption_detection_enabled) {
        pmm_state.metadata.checksum = calculate_bitmap_checksum();
    }
}

// Free multiple pages
void bitmap_pmm_free_pages(uint32_t page_frame, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        bitmap_pmm_free_page(page_frame + i);
    }
}

// Validate bitmap integrity
bool bitmap_pmm_validate_bitmap_integrity(void) {
    if (!pmm_state.initialized) {
        return false;
    }
    
    // Check metadata magic numbers
    if (pmm_state.metadata.magic_header != PMM_MAGIC_HEADER ||
        pmm_state.metadata.magic_footer != PMM_MAGIC_FOOTER) {
        return false;
    }
    
    // Check checksum if corruption detection enabled
    if (pmm_state.config.corruption_detection_enabled) {
        uint32_t calculated_checksum = calculate_bitmap_checksum();
        if (calculated_checksum != pmm_state.metadata.checksum) {
            pmm_state.stats.checksum_failures++;
            return false;
        }
    }
    
    return true;
}

// Get PMM statistics
void bitmap_pmm_get_stats(pmm_stats_t* stats) {
    if (stats) {
        *stats = pmm_state.stats;
    }
}

// Get page state
page_state_t bitmap_pmm_get_page_state(uint32_t page_frame) {
    if (!pmm_state.initialized || page_frame >= pmm_state.metadata.total_pages) {
        return PAGE_CORRUPTED;
    }
    
    return bitmap_test_bit(page_frame) ? PAGE_USED : PAGE_FREE;
}

// Validate page frame number
bool bitmap_pmm_validate_page_frame(uint32_t page_frame) {
    return pmm_state.initialized && page_frame < pmm_state.metadata.total_pages;
}

// Check for corruption
bool bitmap_pmm_check_corruption(void) {
    return !bitmap_pmm_validate_bitmap_integrity();
}

// Scan for corruption
void bitmap_pmm_scan_for_corruption(void) {
    if (bitmap_pmm_check_corruption()) {
        pmm_emergency_report("Bitmap corruption detected", 0);
        pmm_state.stats.bitmap_corruption_detected++;
    }
}

// Dump memory map
void bitmap_pmm_dump_memory_map(void) {
    print("\n=== Physical Memory Map ===\n");
    
    for (uint32_t i = 0; i < pmm_state.region_count; i++) {
        memory_region_t* region = &pmm_state.regions[i];
        
        print("Region ");
        print_dec(i);
        print(": 0x");
        print_hex((uint32_t)region->base_addr);
        print(" - 0x");
        print_hex((uint32_t)(region->base_addr + region->length));
        print(" (");
        print(region->description);
        print(")\n");
    }
}

// Get total memory in bytes
uint32_t bitmap_pmm_get_total_memory(void) {
    return pmm_state.stats.total_pages * PMM_PAGE_SIZE;
}

// Get usable memory in bytes
uint32_t bitmap_pmm_get_usable_memory(void) {
    return (pmm_state.stats.free_pages + pmm_state.stats.used_pages) * PMM_PAGE_SIZE;
}

// Analyze fragmentation
void bitmap_pmm_analyze_fragmentation(void) {
    uint32_t free_blocks = 0;
    uint32_t largest_block = 0;
    uint32_t current_block = 0;
    
    for (uint32_t page = 0; page < pmm_state.metadata.total_pages; page++) {
        if (!bitmap_test_bit(page)) {
            current_block++;
        } else {
            if (current_block > 0) {
                free_blocks++;
                if (current_block > largest_block) {
                    largest_block = current_block;
                }
                current_block = 0;
            }
        }
    }
    
    // Handle case where memory ends with free pages
    if (current_block > 0) {
        free_blocks++;
        if (current_block > largest_block) {
            largest_block = current_block;
        }
    }
    
    pmm_state.stats.free_block_count = free_blocks;
    pmm_state.stats.largest_free_block = largest_block;
    
    // Calculate fragmentation ratio (higher = more fragmented)
    if (pmm_state.stats.free_pages > 0) {
        pmm_state.stats.fragmentation_ratio = (free_blocks * 100) / (pmm_state.stats.free_pages / 10 + 1);
    }
    
    print("[BITMAP_PMM] Fragmentation analysis:\n");
    print("  Free blocks: ");
    print_dec(free_blocks);
    print("\n");
    print("  Largest free block: ");
    print_dec(largest_block);
    print(" pages\n");
    print("  Fragmentation ratio: ");
    print_dec(pmm_state.stats.fragmentation_ratio);
    print("%\n");
}

// Find largest free block
uint32_t bitmap_pmm_find_largest_free_block(void) {
    uint32_t largest_block = 0;
    uint32_t current_block = 0;
    
    for (uint32_t page = 0; page < pmm_state.metadata.total_pages; page++) {
        if (!bitmap_test_bit(page)) {
            current_block++;
        } else {
            if (current_block > largest_block) {
                largest_block = current_block;
            }
            current_block = 0;
        }
    }
    
    // Handle case where memory ends with free pages
    if (current_block > largest_block) {
        largest_block = current_block;
    }
    
    return largest_block;
}

// Dump PMM statistics
void bitmap_pmm_dump_stats(void) {
    print("\n=== Bitmap PMM Statistics ===\n");
    print("Total pages: ");
    print_dec(pmm_state.stats.total_pages);
    print("\n");
    
    print("Free pages: ");
    print_dec(pmm_state.stats.free_pages);
    print("\n");
    
    print("Used pages: ");
    print_dec(pmm_state.stats.used_pages);
    print("\n");
    
    print("Reserved pages: ");
    print_dec(pmm_state.stats.reserved_pages);
    print("\n");
    
    print("Total allocations: ");
    print_dec(pmm_state.stats.total_allocations);
    print("\n");
    
    print("Total frees: ");
    print_dec(pmm_state.stats.total_frees);
    print("\n");
    
    print("Allocation failures: ");
    print_dec(pmm_state.stats.allocation_failures);
    print("\n");
    
    if (pmm_state.stats.bitmap_corruption_detected > 0) {
        print("Corruption detected: ");
        print_dec(pmm_state.stats.bitmap_corruption_detected);
        print(" instances\n");
    }
}