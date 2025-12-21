#include "include/enhanced_heap.h"
#include "include/memory_corruption.h"
#include "include/tlb_manager.h"
#include "include/screen.h"
#include "include/memory.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// ENHANCED HEAP ALLOCATOR IMPLEMENTATION
// =============================================================================
// High-performance, corruption-resistant heap with segregated free lists
// =============================================================================

// Global heap state
static struct {
    bool initialized;
    enhanced_heap_config_t config;
    uint32_t heap_size;
    uint32_t allocation_sequence;
    uint32_t time_counter;
    struct {
        uint32_t start;
        uint32_t end;
    } regions[8];
    uint32_t region_count;
    
    // Size class management
    size_class_t size_classes[HEAP_SIZE_CLASSES];
    
    // Statistics
    enhanced_heap_stats_t stats;
    
    // Large block list (for blocks > HEAP_MAX_SMALL_BLOCK)
    enhanced_heap_block_t* large_block_list;
    
} enhanced_heap_state = {0};

static void add_to_free_list(enhanced_heap_block_t* block, int size_class);

// Emergency reporting function
static void enhanced_heap_emergency_report(const char* msg, void* ptr) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    int row = 5; // Use row 5 for heap errors
    int col = 0;
    
    while (*msg && col < 80) {
        vga[row * 80 + col] = (0x4E << 8) | *msg; // Yellow on red
        msg++;
        col++;
    }
}

// Calculate checksum for block header integrity
static uint32_t calculate_block_checksum(enhanced_heap_block_t* block) {
    uint32_t checksum = 0;
    checksum ^= block->header_magic;
    checksum ^= block->size;
    checksum ^= (uint32_t)block->state;
    checksum ^= block->alloc_sequence;
    checksum ^= block->alloc_time;
    checksum ^= block->corruption_canary;
    checksum ^= (uint32_t)block->alloc_function;
    return checksum;
}

// Allocate a new heap region backed by the legacy kernel heap.
static bool enhanced_heap_add_region(size_t min_size) {
    if (enhanced_heap_state.region_count >= sizeof(enhanced_heap_state.regions) / sizeof(enhanced_heap_state.regions[0])) {
        print("[ENHANCED_HEAP] Region limit reached\n");
        return false;
    }
    
    size_t region_size = enhanced_heap_state.config.expansion_increment;
    size_t overhead = sizeof(enhanced_heap_block_t) + sizeof(uint32_t);
    if (region_size < min_size + overhead) {
        region_size = min_size + overhead;
    }
    region_size = memory_align_up(region_size, MEMORY_PAGE_SIZE);
    
    uint8_t* region = (uint8_t*)kmalloc_aligned(region_size, MEMORY_PAGE_SIZE);
    if (!region) {
        print("[ENHANCED_HEAP] Failed to allocate heap region of size ");
        print_dec(region_size / 1024);
        print(" KB\n");
        return false;
    }
    
    enhanced_heap_block_t* block = (enhanced_heap_block_t*)region;
    block->header_magic = ENHANCED_BLOCK_MAGIC;
    block->size = region_size;
    block->state = EBLOCK_FREE;
    block->checksum = 0;
    block->next_free = NULL;
    block->prev_free = NULL;
    block->alloc_sequence = 0;
    block->alloc_time = 0;
    block->alloc_function = NULL;
    block->corruption_canary = 0xCAFEBABE ^ (uint32_t)block;
    block->footer_magic = ENHANCED_GUARD_MAGIC;
    
    uint32_t* footer = (uint32_t*)(region + region_size - sizeof(uint32_t));
    *footer = block->footer_magic;
    block->checksum = calculate_block_checksum(block);
    
    enhanced_heap_state.regions[enhanced_heap_state.region_count].start = (uint32_t)region;
    enhanced_heap_state.regions[enhanced_heap_state.region_count].end = (uint32_t)region + region_size;
    enhanced_heap_state.region_count++;
    enhanced_heap_state.heap_size += region_size;
    enhanced_heap_state.stats.bytes_free += region_size - overhead;
    enhanced_heap_state.stats.heap_expansions++;
    
    int size_class = enhanced_heap_size_to_class(block->size);
    add_to_free_list(block, size_class);
    
    print("[ENHANCED_HEAP] Added region @0x");
    print_hex((uint32_t)region);
    print(" size ");
    print_dec(region_size / 1024);
    print(" KB\n");
    
    return true;
}

// Initialize size classes
static void initialize_size_classes(void) {
    uint32_t class_size = HEAP_MIN_BLOCK_SIZE;
    
    for (int i = 0; i < HEAP_SIZE_CLASSES; i++) {
        enhanced_heap_state.size_classes[i].block_size = class_size;
        enhanced_heap_state.size_classes[i].free_list = NULL;
        enhanced_heap_state.size_classes[i].free_count = 0;
        enhanced_heap_state.size_classes[i].total_count = 0;
        
        if (i < HEAP_SIZE_CLASSES - 1) {
            class_size *= 2;
        } else {
            class_size = HEAP_MAX_SMALL_BLOCK; // Last class handles up to max small block
        }
    }
}

// Initialize enhanced heap
void enhanced_heap_init(const enhanced_heap_config_t* config) {
    print("[ENHANCED_HEAP] Initializing enhanced heap allocator...\n");
    
    if (config) {
        enhanced_heap_state.config = *config;
    } else {
        // Default configuration
        enhanced_heap_state.config.corruption_detection_enabled = true;
        enhanced_heap_state.config.guard_pages_enabled = false; // Disabled for now
        enhanced_heap_state.config.metadata_protection_enabled = true;
        enhanced_heap_state.config.fragmentation_mitigation_enabled = true;
        enhanced_heap_state.config.debug_mode_enabled = false;
        enhanced_heap_state.config.max_heap_size = 16 * 1024 * 1024; // 16MB
        enhanced_heap_state.config.expansion_increment = 64 * 1024; // 64KB
    }
    
    // Initialize heap boundaries (use same approach as original heap)
    enhanced_heap_state.heap_size = 0;
    
    // Initialize counters
    enhanced_heap_state.allocation_sequence = 0;
    enhanced_heap_state.time_counter = 1;
    
    // Initialize size classes
    initialize_size_classes();
    
    // Clear statistics
    for (int i = 0; i < HEAP_SIZE_CLASSES; i++) {
        enhanced_heap_state.stats.size_class_stats[i].allocations = 0;
        enhanced_heap_state.stats.size_class_stats[i].current_free = 0;
        enhanced_heap_state.stats.size_class_stats[i].max_used = 0;
    }
    
    enhanced_heap_state.large_block_list = NULL;
    enhanced_heap_state.region_count = 0;
    
    // Provision the first heap region so allocations can succeed.
    if (!enhanced_heap_add_region(enhanced_heap_state.config.expansion_increment)) {
        print("[ENHANCED_HEAP] Failed to reserve initial region\n");
        return;
    }
    
    enhanced_heap_state.initialized = true;
    
    print("[ENHANCED_HEAP] Initialized with ");
    if (enhanced_heap_state.config.corruption_detection_enabled) {
        print("corruption detection ");
    }
    if (enhanced_heap_state.config.fragmentation_mitigation_enabled) {
        print("fragmentation mitigation ");
    }
    print("\n");
}

// Validate block structure and integrity
bool enhanced_heap_validate_block(enhanced_heap_block_t* block) {
    if (!block) {
        return false;
    }
    
    // Check if block is within one of the managed regions
    bool in_region = false;
    for (uint32_t i = 0; i < enhanced_heap_state.region_count; i++) {
        if ((uint32_t)block >= enhanced_heap_state.regions[i].start &&
            (uint32_t)block < enhanced_heap_state.regions[i].end) {
            in_region = true;
            break;
        }
    }
    if (!in_region) {
        return false;
    }
    
    // Check header magic
    if (block->header_magic != ENHANCED_BLOCK_MAGIC) {
        return false;
    }
    
    // Validate state
    if (block->state != EBLOCK_FREE && block->state != EBLOCK_USED && 
        block->state != EBLOCK_GUARD && block->state != EBLOCK_CORRUPTED) {
        return false;
    }
    
    // Check minimum size
    if (block->size < sizeof(enhanced_heap_block_t)) {
        return false;
    }
    
    // Validate checksum if corruption detection is enabled
    if (enhanced_heap_state.config.corruption_detection_enabled) {
        uint32_t expected_checksum = calculate_block_checksum(block);
        if (block->checksum != expected_checksum) {
            return false;
        }
        
        // Check footer magic
        uint32_t* footer = (uint32_t*)((uint8_t*)block + block->size - sizeof(uint32_t));
        if (*footer != block->footer_magic) {
            return false;
        }
    }
    
    return true;
}

// Find best fit block in size class
static enhanced_heap_block_t* find_best_fit_in_class(int size_class, size_t required_size) {
    enhanced_heap_block_t* best_fit = NULL;
    enhanced_heap_block_t* current = enhanced_heap_state.size_classes[size_class].free_list;
    
    while (current) {
        if (!enhanced_heap_validate_block(current)) {
            enhanced_heap_emergency_report("Corrupted block in free list", current);
            return NULL;
        }
        
        if (current->size >= required_size) {
            if (!best_fit || current->size < best_fit->size) {
                best_fit = current;
            }
            
            // Perfect fit
            if (current->size == required_size) {
                break;
            }
        }
        
        current = current->next_free;
    }
    
    return best_fit;
}

// Remove block from free list
static void remove_from_free_list(enhanced_heap_block_t* block, int size_class) {
    if (block->prev_free) {
        block->prev_free->next_free = block->next_free;
    } else {
        enhanced_heap_state.size_classes[size_class].free_list = block->next_free;
    }
    
    if (block->next_free) {
        block->next_free->prev_free = block->prev_free;
    }
    
    block->next_free = NULL;
    block->prev_free = NULL;
    enhanced_heap_state.size_classes[size_class].free_count--;
}

// Add block to free list
static void add_to_free_list(enhanced_heap_block_t* block, int size_class) {
    block->state = EBLOCK_FREE;
    block->next_free = enhanced_heap_state.size_classes[size_class].free_list;
    block->prev_free = NULL;
    block->checksum = calculate_block_checksum(block);
    
    if (enhanced_heap_state.size_classes[size_class].free_list) {
        enhanced_heap_state.size_classes[size_class].free_list->prev_free = block;
    }
    
    enhanced_heap_state.size_classes[size_class].free_list = block;
    enhanced_heap_state.size_classes[size_class].free_count++;
}

// Split block if it's significantly larger than needed
static enhanced_heap_block_t* split_block(enhanced_heap_block_t* block, size_t required_size) {
    if (block->size < required_size + sizeof(enhanced_heap_block_t) + 32) {
        // Not enough space to split efficiently
        return NULL;
    }
    
    size_t remaining_size = block->size - required_size;
    
    // Create new block for remaining space
    enhanced_heap_block_t* new_block = (enhanced_heap_block_t*)((uint8_t*)block + required_size);
    new_block->header_magic = ENHANCED_BLOCK_MAGIC;
    new_block->size = remaining_size;
    new_block->state = EBLOCK_FREE;
    new_block->next_free = NULL;
    new_block->prev_free = NULL;
    new_block->alloc_sequence = 0;
    new_block->alloc_time = 0;
    new_block->alloc_function = NULL;
    new_block->corruption_canary = 0xCAFEBABE ^ (uint32_t)new_block;
    new_block->footer_magic = ENHANCED_GUARD_MAGIC;
    
    // Update footer
    uint32_t* footer = (uint32_t*)((uint8_t*)new_block + remaining_size - sizeof(uint32_t));
    *footer = new_block->footer_magic;
    
    // Calculate checksum
    new_block->checksum = calculate_block_checksum(new_block);
    
    // Update original block size
    block->size = required_size;
    
    // Update original block footer
    footer = (uint32_t*)((uint8_t*)block + required_size - sizeof(uint32_t));
    *footer = block->footer_magic;
    block->checksum = calculate_block_checksum(block);
    
    return new_block;
}

// Enhanced heap allocation
void* enhanced_heap_alloc(size_t size, const char* caller) {
    if (!enhanced_heap_state.initialized || size == 0) {
        return NULL;
    }
    
    // Calculate total size including header and footer
    size_t total_size = size + sizeof(enhanced_heap_block_t) + sizeof(uint32_t);
    total_size = (total_size + 7) & ~7; // 8-byte alignment
    
    // Determine size class
    int size_class = enhanced_heap_size_to_class(total_size);
    
    enhanced_heap_block_t* block = NULL;
    
    // Try to find suitable block in size class and larger classes
    for (int i = size_class; i < HEAP_SIZE_CLASSES && !block; i++) {
        block = find_best_fit_in_class(i, total_size);
        if (block) {
            remove_from_free_list(block, i);
            
            // Try to split if block is much larger than needed
            enhanced_heap_block_t* split = split_block(block, total_size);
            if (split) {
                int split_class = enhanced_heap_size_to_class(split->size);
                add_to_free_list(split, split_class);
                enhanced_heap_state.stats.coalesce_operations++;
            }
            break;
        }
    }
    
    // If no suitable block found, attempt to grow the heap region and retry
    if (!block) {
        if (!enhanced_heap_add_region(total_size)) {
            print("[ENHANCED_HEAP] Unable to expand heap for allocation\n");
            return NULL;
        }
        
        for (int i = size_class; i < HEAP_SIZE_CLASSES && !block; i++) {
            block = find_best_fit_in_class(i, total_size);
            if (block) {
                remove_from_free_list(block, i);
                
                enhanced_heap_block_t* split = split_block(block, total_size);
                if (split) {
                    int split_class = enhanced_heap_size_to_class(split->size);
                    add_to_free_list(split, split_class);
                    enhanced_heap_state.stats.coalesce_operations++;
                }
                break;
            }
        }
        
        if (!block) {
            print("[ENHANCED_HEAP] Expansion succeeded but no block available\n");
            return NULL;
        }
    }
    
    // Configure allocated block
    block->state = EBLOCK_USED;
    block->alloc_sequence = ++enhanced_heap_state.allocation_sequence;
    block->alloc_time = ++enhanced_heap_state.time_counter;
    block->alloc_function = caller;
    block->corruption_canary = 0xDEADC0DE ^ (uint32_t)block ^ size;
    block->footer_magic = ENHANCED_GUARD_MAGIC;
    
    // Update footer
    uint32_t* footer = (uint32_t*)((uint8_t*)block + block->size - sizeof(uint32_t));
    *footer = block->footer_magic;
    
    // Recalculate checksum
    block->checksum = calculate_block_checksum(block);
    
    // Update statistics
    enhanced_heap_state.stats.total_allocations++;
    enhanced_heap_state.stats.current_allocations++;
    enhanced_heap_state.stats.bytes_allocated += size;
    if (enhanced_heap_state.stats.bytes_free >= block->size) {
        enhanced_heap_state.stats.bytes_free -= block->size;
    } else {
        enhanced_heap_state.stats.bytes_free = 0;
    }
    enhanced_heap_state.stats.size_class_stats[size_class].allocations++;
    
    // Integrate with corruption detection if enabled
    if (enhanced_heap_state.config.corruption_detection_enabled) {
        // We could register this allocation with the corruption detection system
        // For now, our enhanced heap provides its own corruption detection
    }
    
    // Return pointer to data area
    return (void*)((uint8_t*)block + sizeof(enhanced_heap_block_t));
}

// Enhanced heap free
void enhanced_heap_free(void* ptr, const char* caller) {
    if (!ptr || !enhanced_heap_state.initialized) {
        return;
    }
    
    // Get block from data pointer
    enhanced_heap_block_t* block = (enhanced_heap_block_t*)((uint8_t*)ptr - sizeof(enhanced_heap_block_t));
    
    // Validate block
    if (!enhanced_heap_validate_block(block)) {
        enhanced_heap_emergency_report("Free of corrupted/invalid block", ptr);
        enhanced_heap_state.stats.corruption_detected++;
        return;
    }
    
    // Check for double-free
    if (block->state != EBLOCK_USED) {
        enhanced_heap_emergency_report("Double free detected", ptr);
        enhanced_heap_state.stats.corruption_detected++;
        return;
    }
    
    // Calculate original allocation size for statistics
    size_t user_size = block->size - sizeof(enhanced_heap_block_t) - sizeof(uint32_t);
    
    // Determine size class
    int size_class = enhanced_heap_size_to_class(block->size);
    
    // Add to appropriate free list
    add_to_free_list(block, size_class);
    
    // Update statistics
    enhanced_heap_state.stats.total_frees++;
    enhanced_heap_state.stats.current_allocations--;
    enhanced_heap_state.stats.bytes_allocated -= user_size;
    enhanced_heap_state.stats.bytes_free += block->size;
    
    print("[ENHANCED_HEAP] Freed block of size ");
    print_dec(user_size);
    print(" in size class ");
    print_dec(size_class);
    print("\n");
}

// Validate pointer
bool enhanced_heap_validate_pointer(void* ptr) {
    if (!ptr || !enhanced_heap_state.initialized) {
        return false;
    }
    
    enhanced_heap_block_t* block = (enhanced_heap_block_t*)((uint8_t*)ptr - sizeof(enhanced_heap_block_t));
    
    return enhanced_heap_validate_block(block) && block->state == EBLOCK_USED;
}

// Get statistics
void enhanced_heap_get_stats(enhanced_heap_stats_t* stats) {
    if (stats) {
        *stats = enhanced_heap_state.stats;
    }
}

// Dump statistics
void enhanced_heap_dump_stats(void) {
    print("\n=== Enhanced Heap Statistics ===\n");
    print("Total allocations: ");
    print_dec(enhanced_heap_state.stats.total_allocations);
    print("\n");
    
    print("Total frees: ");
    print_dec(enhanced_heap_state.stats.total_frees);
    print("\n");
    
    print("Current allocations: ");
    print_dec(enhanced_heap_state.stats.current_allocations);
    print("\n");
    
    print("Bytes allocated: ");
    print_dec(enhanced_heap_state.stats.bytes_allocated);
    print("\n");
    
    print("Corruption detected: ");
    print_dec(enhanced_heap_state.stats.corruption_detected);
    print("\n");
    
    print("Size class breakdown:\n");
    for (int i = 0; i < HEAP_SIZE_CLASSES; i++) {
        if (enhanced_heap_state.stats.size_class_stats[i].allocations > 0) {
            print("  Class ");
            print_dec(i);
            print(" (");
            print_dec(enhanced_heap_state.size_classes[i].block_size);
            print(" bytes): ");
            print_dec(enhanced_heap_state.stats.size_class_stats[i].allocations);
            print(" allocs, ");
            print_dec(enhanced_heap_state.size_classes[i].free_count);
            print(" free\n");
        }
    }
}

// Scan for corruption
void enhanced_heap_scan_for_corruption(void) {
    print("[ENHANCED_HEAP] Scanning for corruption...\n");
    
    uint32_t corrupted_blocks = 0;
    
    // Scan all size classes
    for (int i = 0; i < HEAP_SIZE_CLASSES; i++) {
        enhanced_heap_block_t* current = enhanced_heap_state.size_classes[i].free_list;
        
        while (current) {
            if (!enhanced_heap_validate_block(current)) {
                enhanced_heap_emergency_report("Corruption found in free list", current);
                corrupted_blocks++;
            }
            current = current->next_free;
        }
    }
    
    if (corrupted_blocks == 0) {
        print("[ENHANCED_HEAP] No corruption detected\n");
    } else {
        print("[ENHANCED_HEAP] Found ");
        print_dec(corrupted_blocks);
        print(" corrupted blocks\n");
    }
}

// Enable/disable corruption detection
void enhanced_heap_enable_corruption_detection(bool enabled) {
    enhanced_heap_state.config.corruption_detection_enabled = enabled;
    print("[ENHANCED_HEAP] Corruption detection ");
    print(enabled ? "enabled" : "disabled");
    print("\n");
}

// Enable/disable debug mode
void enhanced_heap_enable_debug_mode(bool enabled) {
    enhanced_heap_state.config.debug_mode_enabled = enabled;
    print("[ENHANCED_HEAP] Debug mode ");
    print(enabled ? "enabled" : "disabled");
    print("\n");
}

// Validate all blocks in the heap
bool enhanced_heap_validate_all_blocks(void) {
    if (!enhanced_heap_state.initialized) {
        return false;
    }
    
    bool all_valid = true;
    
    // Check all free lists
    for (int i = 0; i < HEAP_SIZE_CLASSES; i++) {
        enhanced_heap_block_t* current = enhanced_heap_state.size_classes[i].free_list;
        
        while (current) {
            if (!enhanced_heap_validate_block(current)) {
                enhanced_heap_emergency_report("Invalid block in free list", current);
                all_valid = false;
            }
            current = current->next_free;
        }
    }
    
    // Check large block list if implemented
    enhanced_heap_block_t* large_current = enhanced_heap_state.large_block_list;
    while (large_current) {
        if (!enhanced_heap_validate_block(large_current)) {
            enhanced_heap_emergency_report("Invalid block in large list", large_current);
            all_valid = false;
        }
        // Note: This assumes large blocks have a next pointer - would need proper implementation
        break; // For now, just check the first one
    }
    
    return all_valid;
}

// Check heap integrity
bool enhanced_heap_check_integrity(void) {
    return enhanced_heap_validate_all_blocks();
}
