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
#ifdef __x86_64__
    uint64 total_frames;           // Total page frames
    uint64 free_frames;            // Free page frames
    uint64 last_frame;             // Last allocated frame (for optimization)
    uint64 kernel_start_frame;     // First frame after kernel
    uint64 kernel_end_frame;       // Last frame used by kernel
#else
    uint32_t total_frames;         // Total page frames
    uint32_t free_frames;          // Free page frames
    uint32_t last_frame;           // Last allocated frame (for optimization)
    uint32_t kernel_start_frame;   // First frame after kernel
    uint32_t kernel_end_frame;     // Last frame used by kernel
#endif
} pmm_state = {0};

// =============================================================================
// INTERNAL HELPER FUNCTIONS
// =============================================================================

// Convert physical address to frame number
#ifdef __x86_64__
static inline uint64 addr_to_frame(uint64 addr) {
#else
static inline uint32_t addr_to_frame(uint32_t addr) {
#endif
    return addr >> MEMORY_PAGE_SHIFT;
}

// Convert frame number to physical address
#ifdef __x86_64__
static inline uint64 frame_to_addr(uint64 frame) {
#else
static inline uint32_t frame_to_addr(uint32_t frame) {
#endif
    return frame << MEMORY_PAGE_SHIFT;
}

// Get bitmap word and bit for a frame
#ifdef __x86_64__
static inline void frame_to_bitmap_pos(uint64 frame, uint32* word, uint32* bit) {
#else
static inline void frame_to_bitmap_pos(uint32_t frame, uint32* word, uint32* bit) {
#endif
    *word = (uint32)(frame / 32);
    *bit = (uint32)(frame % 32);
}

// Check if frame is marked as used in bitmap
#ifdef __x86_64__
static inline bool is_frame_used(uint64 frame) {
#else
static inline bool is_frame_used(uint32_t frame) {
#endif
    if (frame >= pmm_state.total_frames) return true;
    
    uint32 word, bit;
    frame_to_bitmap_pos(frame, &word, &bit);
    return (pmm_state.bitmap[word] & (1 << bit)) != 0;
}

// Mark frame as used in bitmap
#ifdef __x86_64__
static inline void mark_frame_used(uint64 frame) {
#else
static inline void mark_frame_used(uint32_t frame) {
#endif
    if (frame >= pmm_state.total_frames) return;
    
    uint32 word, bit;
    frame_to_bitmap_pos(frame, &word, &bit);
    
    if (!(pmm_state.bitmap[word] & (1 << bit))) {
        pmm_state.bitmap[word] |= (1 << bit);
        pmm_state.free_frames--;
    }
}

// Mark frame as free in bitmap  
#ifdef __x86_64__
static inline void mark_frame_free(uint64 frame) {
#else
static inline void mark_frame_free(uint32_t frame) {
#endif
    if (frame >= pmm_state.total_frames) return;
    
    uint32 word, bit;
    frame_to_bitmap_pos(frame, &word, &bit);
    
    if (pmm_state.bitmap[word] & (1 << bit)) {
        pmm_state.bitmap[word] &= ~(1 << bit);
        pmm_state.free_frames++;
    }
}

// Find first free frame starting from given frame
#ifdef __x86_64__
static uint64 find_free_frame_from(uint64 start_frame) {
#else
static uint32_t find_free_frame_from(uint32_t start_frame) {
#endif
    // Start from specified frame and wrap around if necessary
#ifdef __x86_64__
    for (uint64 i = 0; i < pmm_state.total_frames; i++) {
        uint64 frame = (start_frame + i) % pmm_state.total_frames;
#else
    for (uint32_t i = 0; i < pmm_state.total_frames; i++) {
        uint32_t frame = (start_frame + i) % pmm_state.total_frames;
#endif
        
        // Skip frames below kernel area
        if (frame < pmm_state.kernel_start_frame) {
            continue;
        }
        
        if (!is_frame_used(frame)) {
            return frame;
        }
    }
    
#ifdef __x86_64__
    return 0xFFFFFFFFFFFFFFFF; // No free frame found (UINT64_MAX)
#else
    return 0xFFFFFFFF; // No free frame found (UINT32_MAX)
#endif
}

// Find contiguous free frames
#ifdef __x86_64__
static uint64 find_contiguous_frames(uint32 count) {
    if (count == 0) return 0xFFFFFFFFFFFFFFFF;
    if (count == 1) return find_free_frame_from(pmm_state.last_frame);
    
    for (uint64 start = pmm_state.kernel_start_frame;
         start + count <= pmm_state.total_frames && start + count > start;
         start++) {
        
        bool found = true;
        for (uint64 i = 0; i < count; i++) {
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
    
    return 0xFFFFFFFFFFFFFFFF; // Not found
#else
static uint32_t find_contiguous_frames(uint32 count) {
    if (count == 0) return 0xFFFFFFFF;
    if (count == 1) return find_free_frame_from(pmm_state.last_frame);
    
    for (uint32_t start = pmm_state.kernel_start_frame;
         start + count <= pmm_state.total_frames && start + count > start;
         start++) {
        
        bool found = true;
        for (uint32_t i = 0; i < count; i++) {
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
#endif
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
#ifdef __x86_64__
    uint64 total_memory = 0;
    uint64 highest_addr = 0;
#else
    uint32_t total_memory = 0;
    uint32_t highest_addr = 0;
#endif
    
    for (uint32 i = 0; i < region_count; i++) {
        if (regions[i].type == 1) { // Available memory
#ifdef __x86_64__
            if (regions[i].base_address > UINT64_MAX - regions[i].length) {
#else
            if (regions[i].base_address > UINT32_MAX - regions[i].length) {
#endif
                continue; // Skip overflowed entries
            }

            total_memory += regions[i].length;
#ifdef __x86_64__
            uint64 end_addr = regions[i].base_address + regions[i].length;
#else
            uint32_t end_addr = regions[i].base_address + regions[i].length;
#endif
            if (end_addr > highest_addr) {
                highest_addr = end_addr;
            }
        }
    }

    // Calculate frame count based on highest address, not total memory
    // This ensures we can address all possible frames
    pmm_state.total_frames = highest_addr >> MEMORY_PAGE_SHIFT;

    // Limit to reasonable maximum to prevent excessive bitmap size
    if (pmm_state.total_frames > MEMORY_PMM_MAX_FRAMES) {
        pmm_state.total_frames = MEMORY_PMM_MAX_FRAMES;
    }
        
    print("[PMM] Total memory: ");
    print_dec((uint32)(total_memory / 1024));
    print(" KB, Total frames: ");
    print_dec((uint32)pmm_state.total_frames); // Cast for printing
    print("\n");
        
    if (pmm_state.total_frames == 0) {
        return MEMORY_ERROR_OUT_OF_MEMORY;
    }

    // Calculate bitmap size in uint32s
    // The number of frames is now uint64, but bitmap_size itself can still be uint32
    // since MEMORY_PMM_MAX_FRAMES is (16 * 1024 * 1024) which fits in uint32.
    pmm_state.bitmap_size = (uint32)((pmm_state.total_frames + 31) / 32);
    if (pmm_state.bitmap_size == 0 || pmm_state.bitmap_size > (UINT32_MAX / sizeof(uint32))) {
        return MEMORY_ERROR_INVALID_SIZE;
    }
        
    // Place bitmap in its dedicated memory region
    uint32 bitmap_addr = memory_align_up(memory_get_pmm_start(), MEMORY_PAGE_SIZE);
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
#ifdef __x86_64__
    for (uint64 i = 0; i < pmm_state.total_frames; i++) {
#else
    for (uint32_t i = 0; i < pmm_state.total_frames; i++) {
#endif
        mark_frame_used(i);
    }
        
    // Mark available regions as free
    for (uint32 i = 0; i < region_count; i++) {
        if (regions[i].type == 1) { // Available memory
#ifdef __x86_64__
            uint64 start_frame = regions[i].base_address >> MEMORY_PAGE_SHIFT;
            uint64 end_frame = (regions[i].base_address + regions[i].length) >> MEMORY_PAGE_SHIFT;
            for (uint64 frame = start_frame; frame < end_frame && frame < pmm_state.total_frames; frame++) {
#else
            uint32_t start_frame = regions[i].base_address >> MEMORY_PAGE_SHIFT;
            uint32_t end_frame = (regions[i].base_address + regions[i].length) >> MEMORY_PAGE_SHIFT;
            for (uint32_t frame = start_frame; frame < end_frame && frame < pmm_state.total_frames; frame++) {
#endif
                mark_frame_free(frame);
            }
        }
    }
        
    // Mark kernel area and bitmap as used
    pmm_state.kernel_start_frame = addr_to_frame(MEMORY_KERNEL_START);
    pmm_state.kernel_end_frame = addr_to_frame((uint64)bitmap_addr + pmm_state.bitmap_size * sizeof(uint32));
        
#ifdef __x86_64__
    for (uint64 frame = 0; frame <= pmm_state.kernel_end_frame; frame++) {
#else
    for (uint32_t frame = 0; frame <= pmm_state.kernel_end_frame; frame++) {
#endif
        mark_frame_used(frame);
    }
        
    pmm_state.last_frame = pmm_state.kernel_end_frame + 1;
        
    pmm_state.initialized = true;
        
    print("[PMM] Initialization complete. Free frames: ");
    print_dec((uint32)pmm_state.free_frames); // Cast for printing
    print("\n");
        
    return MEMORY_OK;
}
            
#ifdef __x86_64__
uint64 pmm_alloc_frame(void) {
#else
uint32_t pmm_alloc_frame(void) {
#endif
    if (!pmm_state.initialized) {
        return 0; // Not initialized
    }
                
    if (pmm_state.free_frames == 0) {
        return 0; // Out of memory
    }
                
#ifdef __x86_64__
    uint64 frame = find_free_frame_from(pmm_state.last_frame);
#else
    uint32_t frame = find_free_frame_from(pmm_state.last_frame);
#endif
                
#ifdef __x86_64__
    if (frame == 0xFFFFFFFFFFFFFFFF) {
#else
    if (frame == 0xFFFFFFFF) {
#endif
        return 0; // No free frame found
    }
                
    mark_frame_used(frame);
                
    pmm_state.last_frame = frame + 1;
                
    return frame_to_addr(frame);
}
            
#ifdef __x86_64__
uint64 pmm_alloc_frames(uint32 count) {
#else
uint32_t pmm_alloc_frames(uint32 count) {
#endif
    if (!pmm_state.initialized) {
        return 0;
    }
                
    if (count == 0 || count > pmm_state.free_frames) {
        return 0;
    }
                
#ifdef __x86_64__
    uint64 start_frame = find_contiguous_frames(count);
#else
    uint32_t start_frame = find_contiguous_frames(count);
#endif
                
#ifdef __x86_64__
    if (start_frame == 0xFFFFFFFFFFFFFFFF) {
#else
    if (start_frame == 0xFFFFFFFF) {
#endif
        return 0;
    }
                
    // Mark all frames as used
    for (uint32 i = 0; i < count; i++) { // 'i' can remain uint32 as 'count' is uint32
        mark_frame_used(start_frame + i);
    }
                
    pmm_state.last_frame = start_frame + count;
                
    return frame_to_addr(start_frame);
}
            
#ifdef __x86_64__
memory_result_t pmm_alloc_scattered(uint32 count, uint64* frames_out,
#else
memory_result_t pmm_alloc_scattered(uint32 count, uint32_t* frames_out,
#endif
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
#ifdef __x86_64__
    uint64 contiguous_addr = pmm_alloc_frames(count);
#else
    uint32_t contiguous_addr = pmm_alloc_frames(count);
#endif
                
    if (contiguous_addr != 0) {
        uint32 frames_to_record = (count < max_frames) ? count : max_frames;
        for (uint32 i = 0; i < frames_to_record; i++) {
#ifdef __x86_64__
            frames_out[i] = contiguous_addr + (i * MEMORY_PAGE_SIZE);
#else
            frames_out[i] = (uint32_t)(contiguous_addr + (i * MEMORY_PAGE_SIZE));
#endif
        }
        *allocated = frames_to_record;
        return MEMORY_OK;
    }
                
    // Fall back to scattered frames when fragmentation prevents contiguous blocks
    *allocated = 0;
    for (uint32 i = 0; i < count && *allocated < max_frames; i++) {
#ifdef __x86_64__
        uint64 frame_addr = pmm_alloc_frame();
#else
        uint32_t frame_addr = pmm_alloc_frame();
#endif
                
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

#ifdef __x86_64__
memory_result_t pmm_free_frame(uint64 frame_addr) {
#else
memory_result_t pmm_free_frame(uint32_t frame_addr) {
#endif
    if (!pmm_state.initialized) {
        return MEMORY_ERROR_NOT_INITIALIZED;
    }
    
    if (frame_addr == 0 || (frame_addr & MEMORY_PAGE_MASK) != 0) {
        return MEMORY_ERROR_INVALID_ADDR; // Not page-aligned
    }
    
#ifdef __x86_64__
    uint64 frame = addr_to_frame(frame_addr);
#else
    uint32_t frame = addr_to_frame(frame_addr);
#endif
    
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

#ifdef __x86_64__
memory_result_t pmm_free_frames(uint64 frame_addr, uint32 count) {
#else
memory_result_t pmm_free_frames(uint32_t frame_addr, uint32 count) {
#endif
    if (!pmm_state.initialized) {
        return MEMORY_ERROR_NOT_INITIALIZED;
    }
    
    if (count == 0) {
        return MEMORY_OK;
    }
    
    // Validate all frames first
#ifdef __x86_64__
    uint64 start_frame = addr_to_frame(frame_addr);
    for (uint32 i = 0; i < count; i++) { // 'i' can remain uint32 as 'count' is uint32
        uint64 frame = start_frame + i;
#else
    uint32_t start_frame = addr_to_frame(frame_addr);
    for (uint32 i = 0; i < count; i++) { // 'i' can remain uint32 as 'count' is uint32
        uint32_t frame = start_frame + i;
#endif
        if (frame < pmm_state.kernel_end_frame || 
            frame >= pmm_state.total_frames ||
            !is_frame_used(frame)) {
            return MEMORY_ERROR_INVALID_ADDR;
        }
    }
    
    // Free all frames
    for (uint32 i = 0; i < count; i++) { // 'i' can remain uint32
        mark_frame_free(start_frame + i);
    }
    
    return MEMORY_OK;
}

#ifdef __x86_64__
bool pmm_is_frame_free(uint64 frame_addr) {
#else
bool pmm_is_frame_free(uint32_t frame_addr) {
#endif
    if (!pmm_state.initialized) {
        return false;
    }
    
#ifdef __x86_64__
    uint64 frame = addr_to_frame(frame_addr);
#else
    uint32_t frame = addr_to_frame(frame_addr);
#endif
    return !is_frame_used(frame);
}

// Reserve a range of physical memory (mark as used)
// Used to protect regions like initrd from being allocated
#ifdef __x86_64__
void pmm_reserve_range(uint64 start_addr, uint64 end_addr) {
#else
void pmm_reserve_range(uint32_t start_addr, uint32_t end_addr) {
#endif
    if (!pmm_state.initialized) {
        return;
    }

    // Align to page boundaries
#ifdef __x86_64__
    uint64 start_frame = addr_to_frame(start_addr & ~(MEMORY_PAGE_SIZE - 1));
    uint64 end_frame = addr_to_frame((end_addr + MEMORY_PAGE_SIZE - 1) & ~(MEMORY_PAGE_SIZE - 1));
    for (uint64 frame = start_frame; frame < end_frame && frame < pmm_state.total_frames; frame++) {
#else
    uint32_t start_frame = addr_to_frame(start_addr & ~(MEMORY_PAGE_SIZE - 1));
    uint32_t end_frame = addr_to_frame((end_addr + MEMORY_PAGE_SIZE - 1) & ~(MEMORY_PAGE_SIZE - 1));
    for (uint32_t frame = start_frame; frame < end_frame && frame < pmm_state.total_frames; frame++) {
#endif
        mark_frame_used(frame);
    }
}

// Get total number of frames
#ifdef __x86_64__
uint64 pmm_get_total_frames(void) {
#else
uint32_t pmm_get_total_frames(void) {
#endif
    return pmm_state.total_frames;
}

// Get number of free frames
#ifdef __x86_64__
uint64 pmm_get_free_frames(void) {
#else
uint32_t pmm_get_free_frames(void) {
#endif
    return pmm_state.free_frames;
}
