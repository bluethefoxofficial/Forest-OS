#include "include/memory.h"
#include "include/screen.h"
#include "include/string.h"
#include "include/panic.h"

// =============================================================================
// PHYSICAL MEMORY MANAGER IMPLEMENTATION
// =============================================================================
// Simple, reliable bitmap-based page frame allocator
// Uses one bit per page frame to track allocation status
// =============================================================================

// PMM internal state
static struct {
    bool initialized;
    uint32* bitmap;                 // Allocation bitmap
    uint32 bitmap_size;            // Size of bitmap in uint32s
    uint32 total_frames;           // Total page frames
    uint32 free_frames;            // Free page frames
    uint32 last_frame;             // Last allocated frame (for optimization)
    uint32 kernel_start_frame;     // First frame after kernel
    uint32 kernel_end_frame;       // Last frame used by kernel
} pmm_state = {0};

// =============================================================================
// INTERNAL HELPER FUNCTIONS
// =============================================================================

// Convert physical address to frame number
static inline uint32 addr_to_frame(uint32 addr) {
    return addr >> MEMORY_PAGE_SHIFT;
}

// Convert frame number to physical address
static inline uint32 frame_to_addr(uint32 frame) {
    return frame << MEMORY_PAGE_SHIFT;
}

// Get bitmap word and bit for a frame
static inline void frame_to_bitmap_pos(uint32 frame, uint32* word, uint32* bit) {
    *word = frame / 32;
    *bit = frame % 32;
}

// Check if frame is marked as used in bitmap
static inline bool is_frame_used(uint32 frame) {
    if (frame >= pmm_state.total_frames) return true;
    
    uint32 word, bit;
    frame_to_bitmap_pos(frame, &word, &bit);
    return (pmm_state.bitmap[word] & (1 << bit)) != 0;
}

// Mark frame as used in bitmap
static inline void mark_frame_used(uint32 frame) {
    if (frame >= pmm_state.total_frames) return;
    
    uint32 word, bit;
    frame_to_bitmap_pos(frame, &word, &bit);
    
    if (!(pmm_state.bitmap[word] & (1 << bit))) {
        pmm_state.bitmap[word] |= (1 << bit);
        pmm_state.free_frames--;
    }
}

// Mark frame as free in bitmap  
static inline void mark_frame_free(uint32 frame) {
    if (frame >= pmm_state.total_frames) return;
    
    uint32 word, bit;
    frame_to_bitmap_pos(frame, &word, &bit);
    
    if (pmm_state.bitmap[word] & (1 << bit)) {
        pmm_state.bitmap[word] &= ~(1 << bit);
        pmm_state.free_frames++;
    }
}

// Find first free frame starting from given frame
static uint32 find_free_frame_from(uint32 start_frame) {
    // Start from specified frame and wrap around if necessary
    for (uint32 i = 0; i < pmm_state.total_frames; i++) {
        uint32 frame = (start_frame + i) % pmm_state.total_frames;
        
        // Skip frames below kernel area
        if (frame < pmm_state.kernel_start_frame) {
            continue;
        }
        
        if (!is_frame_used(frame)) {
            return frame;
        }
    }
    
    return 0xFFFFFFFF; // No free frame found
}

// Find contiguous free frames
static uint32 find_contiguous_frames(uint32 count) {
    if (count == 0) return 0xFFFFFFFF;
    if (count == 1) return find_free_frame_from(pmm_state.last_frame);
    
    for (uint32 start = pmm_state.kernel_start_frame;
         start + count <= pmm_state.total_frames && start + count > start;
         start++) {
        
        bool found = true;
        for (uint32 i = 0; i < count; i++) {
            if (is_frame_used(start + i)) {
                found = false;
                start += i; // Skip ahead past this used frame
                break;
            }
        }
        
        if (found) {
            return start;
        }
    }
    
    return 0xFFFFFFFF; // Not found
}

// =============================================================================
// PUBLIC INTERFACE IMPLEMENTATION  
// =============================================================================

memory_result_t pmm_init(memory_region_t* regions, uint32 region_count) {
    print("[PMM] pmm_init: Entry, regions: 0x");
    print_hex((uint32)regions);
    print(", count: ");
    print_dec(region_count);
    print("\n");
    if (!regions || region_count == 0) {
        return MEMORY_ERROR_NULL_PTR;
    }
    
    print("[PMM] Initializing Physical Memory Manager...\n");
    
    // Find total usable memory and highest address
    uint64 total_memory = 0;
    uint64 highest_addr = 0;
    
    for (uint32 i = 0; i < region_count; i++) {
        if (regions[i].type == 1) { // Available memory
            if (regions[i].base_address > UINT64_MAX - regions[i].length) {
                continue; // Skip overflowed entries
            }

            total_memory += regions[i].length;
            uint64 end_addr = regions[i].base_address + regions[i].length;
            if (end_addr > highest_addr) {
                highest_addr = end_addr;
            }
        }
    }

    // Calculate frame count based on highest address, not total memory
    // This ensures we can address all possible frames
    pmm_state.total_frames = (uint32)(highest_addr >> MEMORY_PAGE_SHIFT);

    // Limit to reasonable maximum to prevent excessive bitmap size
    if (pmm_state.total_frames > MEMORY_PMM_MAX_FRAMES) { // 4GB worth of frames
        pmm_state.total_frames = MEMORY_PMM_MAX_FRAMES;
    }
    
    print("[PMM] Total memory: ");
    print_dec((uint32)(total_memory / 1024));
    print(" KB, Total frames: ");
    print_dec(pmm_state.total_frames);
    print("\n");
    
    if (pmm_state.total_frames == 0) {
        return MEMORY_ERROR_OUT_OF_MEMORY;
    }

    // Calculate bitmap size in uint32s
    pmm_state.bitmap_size = (pmm_state.total_frames + 31) / 32;
    if (pmm_state.bitmap_size == 0 || pmm_state.bitmap_size > (UINT32_MAX / sizeof(uint32))) {
        return MEMORY_ERROR_INVALID_SIZE;
    }
    
    // Place bitmap in its dedicated memory region
    uint32 bitmap_addr = memory_align_up(MEMORY_PMM_START, MEMORY_PAGE_SIZE);
    pmm_state.bitmap = (uint32*)bitmap_addr;
    
    print("[PMM] Bitmap at 0x");
    print_hex(bitmap_addr);
    print(", size: ");
    print_dec(pmm_state.bitmap_size * 4);
    print(" bytes\n");
    
            // Clear bitmap (all frames initially free)
    
            memset(pmm_state.bitmap, 0, pmm_state.bitmap_size * sizeof(uint32));
    
            pmm_state.free_frames = pmm_state.total_frames;
    
            
    
            // Mark all memory as used initially
    
            for (uint32 i = 0; i < pmm_state.total_frames; i++) {
    
                mark_frame_used(i);
    
            }
    
            
    
            // Mark available regions as free
    
            for (uint32 i = 0; i < region_count; i++) {
    
                if (regions[i].type == 1) { // Available memory
    
                    uint32 start_frame = (uint32)(regions[i].base_address >> MEMORY_PAGE_SHIFT);
    
                    uint32 end_frame = (uint32)((regions[i].base_address + regions[i].length) >> MEMORY_PAGE_SHIFT);
    
                    
    
                    for (uint32 frame = start_frame; frame < end_frame && frame < pmm_state.total_frames; frame++) {
    
                        mark_frame_free(frame);
    
                    }
    
                }
    
            }
    
            
    
            // Mark kernel area and bitmap as used
    
            pmm_state.kernel_start_frame = addr_to_frame(MEMORY_KERNEL_START);
    
            pmm_state.kernel_end_frame = addr_to_frame(bitmap_addr + pmm_state.bitmap_size * sizeof(uint32));
    
            
    
            for (uint32 frame = 0; frame <= pmm_state.kernel_end_frame; frame++) {
    
                mark_frame_used(frame);
    
            }
    
            
    
            pmm_state.last_frame = pmm_state.kernel_end_frame + 1;
    
            pmm_state.initialized = true;
    
            
    
            print("[PMM] Initialization complete. Free frames: ");
    
            print_dec(pmm_state.free_frames);
    
            print("\n");
    
            
    
            return MEMORY_OK;
    
        }
    
        
    
        uint32 pmm_alloc_frame(void) {
    
            if (!pmm_state.initialized) {
    
                return 0; // Not initialized
    
            }
    
            
    
            if (pmm_state.free_frames == 0) {
    
                return 0; // Out of memory
    
            }
    
            
    
            uint32 frame = find_free_frame_from(pmm_state.last_frame);
    
            if (frame == 0xFFFFFFFF) {
    
                return 0; // No free frame found
    
            }
    
            // Add bounds checking to prevent allocations beyond reasonable limits
            uint32 addr = frame_to_addr(frame);
            const uint32 MAX_SAFE_PHYSICAL_ADDR = 0x10000000; // 256MB limit for safety
            
            if (addr >= MAX_SAFE_PHYSICAL_ADDR) {
                // Try to find a frame within the safe range
                for (uint32 safe_frame = 0; safe_frame < addr_to_frame(MAX_SAFE_PHYSICAL_ADDR); safe_frame++) {
                    if (!is_frame_used(safe_frame)) {
                        frame = safe_frame;
                        addr = frame_to_addr(frame);
                        break;
                    }
                }
                
                // If no safe frame found, return 0
                if (addr >= MAX_SAFE_PHYSICAL_ADDR) {
                    return 0;
                }
            }
    
            
    
            mark_frame_used(frame);
    
            pmm_state.last_frame = frame + 1;
    
            
    
            return addr;
}

uint32 pmm_alloc_frames(uint32 count) {
    if (!pmm_state.initialized) {
        return 0;
    }
    
    if (count == 0 || count > pmm_state.free_frames) {
        return 0;
    }
    
    uint32 start_frame = find_contiguous_frames(count);
    if (start_frame == 0xFFFFFFFF) {
        return 0;
    }
    
    // Mark all frames as used
    for (uint32 i = 0; i < count; i++) {
        mark_frame_used(start_frame + i);
    }
    
    pmm_state.last_frame = start_frame + count;

    return frame_to_addr(start_frame);
}

memory_result_t pmm_alloc_scattered(uint32 count, uint32* frames_out,
                                    uint32 max_frames, uint32* allocated) {
    if (!pmm_state.initialized) {
        return MEMORY_ERROR_NOT_INITIALIZED;
    }

    if (!frames_out || !allocated) {
        return MEMORY_ERROR_NULL_PTR;
    }

    if (count == 0 || max_frames == 0) {
        *allocated = 0;
        return MEMORY_ERROR_INVALID_SIZE;
    }

    // Try contiguous allocation first
    uint32 contiguous_addr = pmm_alloc_frames(count);
    if (contiguous_addr != 0) {
        uint32 frames_to_record = (count < max_frames) ? count : max_frames;
        for (uint32 i = 0; i < frames_to_record; i++) {
            frames_out[i] = contiguous_addr + (i * MEMORY_PAGE_SIZE);
        }
        *allocated = frames_to_record;
        return MEMORY_OK;
    }

    // Fall back to scattered frames when fragmentation prevents contiguous blocks
    *allocated = 0;
    for (uint32 i = 0; i < count && *allocated < max_frames; i++) {
        uint32 frame_addr = pmm_alloc_frame();
        if (frame_addr == 0) {
            break;
        }
        frames_out[*allocated] = frame_addr;
        (*allocated)++;
    }

    if (*allocated == 0) {
        return MEMORY_ERROR_OUT_OF_MEMORY;
    }

    return MEMORY_OK;
}

memory_result_t pmm_free_frame(uint32 frame_addr) {
    if (!pmm_state.initialized) {
        return MEMORY_ERROR_NOT_INITIALIZED;
    }
    
    if (frame_addr == 0 || (frame_addr & MEMORY_PAGE_MASK) != 0) {
        return MEMORY_ERROR_INVALID_ADDR; // Not page-aligned
    }
    
    uint32 frame = addr_to_frame(frame_addr);
    
    if (frame < pmm_state.kernel_end_frame) {
        return MEMORY_ERROR_INVALID_ADDR; // Cannot free kernel frames
    }
    
    if (frame >= pmm_state.total_frames) {
        return MEMORY_ERROR_INVALID_ADDR;
    }
    
    if (!is_frame_used(frame)) {
        return MEMORY_ERROR_INVALID_ADDR; // Double free
    }
    
    mark_frame_free(frame);
    return MEMORY_OK;
}

memory_result_t pmm_free_frames(uint32 frame_addr, uint32 count) {
    if (!pmm_state.initialized) {
        return MEMORY_ERROR_NOT_INITIALIZED;
    }
    
    if (count == 0) {
        return MEMORY_OK;
    }
    
    // Validate all frames first
    uint32 start_frame = addr_to_frame(frame_addr);
    for (uint32 i = 0; i < count; i++) {
        uint32 frame = start_frame + i;
        if (frame < pmm_state.kernel_end_frame || 
            frame >= pmm_state.total_frames ||
            !is_frame_used(frame)) {
            return MEMORY_ERROR_INVALID_ADDR;
        }
    }
    
    // Free all frames
    for (uint32 i = 0; i < count; i++) {
        mark_frame_free(start_frame + i);
    }
    
    return MEMORY_OK;
}

bool pmm_is_frame_free(uint32 frame_addr) {
    if (!pmm_state.initialized) {
        return false;
    }
    
    uint32 frame = addr_to_frame(frame_addr);
    return !is_frame_used(frame);
}

uint32 pmm_get_total_frames(void) {
    return pmm_state.total_frames;
}

uint32 pmm_get_free_frames(void) {
    return pmm_state.free_frames;
}
