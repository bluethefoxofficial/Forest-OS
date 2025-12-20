#include "include/memory.h"
#include "include/screen.h"
#include "include/string.h"
#include "include/tlb_manager.h"

// =============================================================================
// KERNEL HEAP MANAGER IMPLEMENTATION
// =============================================================================
// Simple block-based allocator for kernel dynamic memory
// Uses linked lists of free blocks with first-fit allocation strategy
// =============================================================================

#define HEAP_MAGIC      0x48454150  // "HEAP"
#define BLOCK_MAGIC     0x424C4B53  // "BLKS"
#define BLOCK_FREE      0xFEEEFEEE
#define BLOCK_USED      0xDEADBEEF

// Block header structure
typedef struct heap_block {
    uint32 magic;               // Magic number for validation
    uint32 size;               // Size of this block (including header)
    uint32 status;             // Free or used marker
    struct heap_block* next;    // Next block in chain
    struct heap_block* prev;    // Previous block in chain
} heap_block_t;

// Heap state
static struct {
    bool initialized;
    uint32 start_addr;         // Virtual start address of heap
    uint32 current_end;        // Current end of heap
    uint32 max_size;           // Maximum heap size
    heap_block_t* first_block; // First block in heap
    heap_block_t* free_list;   // Head of free block list
    uint32 total_size;         // Total heap size
    uint32 used_size;          // Used heap size  
    uint32 free_size;          // Free heap size
    uint32 block_count;        // Total number of blocks
    uint32 alloc_count;        // Number of allocations
    uint32 free_count;         // Number of frees
} heap_state = {0};

// External VMM and PMM functions
extern memory_result_t vmm_map_page(page_directory_t* dir, uint32 vaddr, uint32 paddr, uint32 flags);
extern page_directory_t* vmm_get_current_page_directory(void);
extern uint32 pmm_alloc_frame(void);

// =============================================================================
// INTERNAL HELPER FUNCTIONS
// =============================================================================

// Validate block magic and structure
static bool is_valid_block(heap_block_t* block) {
    if (!block) {
        return false;
    }
    
    // Check if pointer is within heap bounds
    if ((uint32)block < heap_state.start_addr || 
        (uint32)block >= heap_state.current_end) {
        return false;
    }
    
    // Check basic alignment
    if (((uint32)block & 0x3) != 0) {
        return false;
    }
    
    // Validate magic number and status
    if (block->magic != BLOCK_MAGIC) {
        return false;
    }
    
    if (block->status != BLOCK_FREE && block->status != BLOCK_USED) {
        return false;
    }
    
    // Check minimum size
    if (block->size < sizeof(heap_block_t)) {
        return false;
    }
    
    // Check that block doesn't extend beyond heap
    if ((uint32)block + block->size > heap_state.current_end) {
        return false;
    }
    
    return true;
}

// Get data pointer from block
static void* block_to_data(heap_block_t* block) {
    return (void*)((uint32)block + sizeof(heap_block_t));
}

// Get block from data pointer
static heap_block_t* data_to_block(void* data) {
    if (!data) return NULL;
    return (heap_block_t*)((uint32)data - sizeof(heap_block_t));
}

// Calculate aligned size
static uint32 align_size(uint32 size) {
    return memory_align_up(size, sizeof(uint32));
}

// Add block to free list in address-sorted order and coalesce
static void add_to_free_list(heap_block_t* block) {
    print("[HEAP] add_to_free_list: addr=0x"); print_hex((uint32)block); print(", size="); print_dec(block->size); print("\n");
    block->status = BLOCK_FREE;

    heap_block_t *current = heap_state.free_list;
    heap_block_t *prev = NULL;

    // Find the correct position to insert the block to keep the list sorted by address
    while (current != NULL && (uint32)current < (uint32)block) {
        // Validate current block before dereferencing
        if (!is_valid_block(current)) {
            print("[HEAP] ERROR: Corrupted block in free list at 0x"); print_hex((uint32)current); print("\n");
            heap_state.free_list = block; // Reset free list to this block
            block->next = NULL;
            block->prev = NULL;
            return;
        }
        prev = current;
        current = current->next;
    }

    // Insert the block into the list
    if (prev == NULL) { // Insert at head
        heap_state.free_list = block;
    } else {
        prev->next = block;
    }
    block->prev = prev;
    block->next = current;
    if (current != NULL) {
        current->prev = block;
    }

    // Coalesce with the next block if it's adjacent and free
    if (block->next != NULL && is_valid_block(block->next) && 
        (uint32)block + block->size == (uint32)block->next) {
        print("[HEAP] Coalesce next\n");
        heap_block_t* next_block = block->next;
        block->size += next_block->size;
        block->next = next_block->next;
        if (next_block->next != NULL) {
            next_block->next->prev = block;
        }
        heap_state.block_count--;
    }

    // Coalesce with the previous block if it's adjacent and free
    if (block->prev != NULL && is_valid_block(block->prev) && 
        (uint32)block->prev + block->prev->size == (uint32)block) {
        print("[HEAP] Coalesce prev\n");
        heap_block_t* prev_block = block->prev;
        prev_block->size += block->size;
        prev_block->next = block->next;
        if (block->next != NULL) {
            block->next->prev = prev_block;
        }
        heap_state.block_count--;
    }
}

// Remove block from free list
static void remove_from_free_list(heap_block_t* block) {
    print("[HEAP] remove_from_free_list: addr=0x"); print_hex((uint32)block); print("\n");
    
    if (!is_valid_block(block)) {
        print("[HEAP] ERROR: Attempting to remove invalid block\n");
        return;
    }
    
    if (block->prev) {
        if (is_valid_block(block->prev)) {
            block->prev->next = block->next;
        } else {
            print("[HEAP] ERROR: Corrupted prev pointer\n");
        }
    } else {
        heap_state.free_list = block->next;
    }
    
    if (block->next) {
        if (is_valid_block(block->next)) {
            block->next->prev = block->prev;
        } else {
            print("[HEAP] ERROR: Corrupted next pointer\n");
        }
    }
    
    block->next = NULL;
    block->prev = NULL;
    block->status = BLOCK_USED;
}

// Find free block of at least given size
static heap_block_t* find_free_block(uint32 size) {
    // Reduced debugging - only print for large allocations or errors
    static uint32 debug_counter = 0;
    bool should_debug = (size > 1024 || (debug_counter++ % 100) == 0);
    
    if (should_debug) {
        print("[HEAP] find_free_block: looking for size="); print_dec(size); print("\n");
    }
    heap_block_t* block = heap_state.free_list;
    
    int i = 0;
    while (block) {
        // Validate block before accessing its members
        if (!is_valid_block(block)) {
            print("[HEAP] ERROR: Invalid block in free list at 0x"); print_hex((uint32)block); print("\n");
            // Remove corrupted block from free list
            if (block == heap_state.free_list) {
                heap_state.free_list = NULL;
            }
            return NULL;
        }
        
        // Check if virtual address is within valid heap range
        if ((uint32)block < heap_state.start_addr || 
            (uint32)block >= heap_state.current_end) {
            print("[HEAP] ERROR: Block outside heap range at 0x"); print_hex((uint32)block); print("\n");
            return NULL;
        }
        
        if (should_debug && i < 3) {
            print("       block "); print_dec(i); print(": 0x"); print_hex((uint32)block);
            print(" next: 0x"); print_hex((uint32)block->next); print("\n");
        }

        if (block->size >= size) {
            if (should_debug) {
                print("       found suitable block\n");
            }
            return block;
        }
        block = block->next;
        i++;
        
        // Prevent infinite loops
        if (i > 1000) {
            print("[HEAP] ERROR: Infinite loop detected in free list\n");
            return NULL;
        }
    }
    
    print("       find_free_block returning NULL\n");
    return NULL;
}

// Split a block if it's large enough
static void split_block(heap_block_t* block, uint32 size) {
    if (!is_valid_block(block)) {
        print("[HEAP] ERROR: Attempting to split invalid block\n");
        return;
    }
    
    uint32 remaining = block->size - size;
    
    // Only split if remaining space is large enough for a new block
    if (remaining >= sizeof(heap_block_t) + 16) {
        heap_block_t* new_block = (heap_block_t*)((uint32)block + size);
        
        // Ensure new block doesn't exceed heap bounds
        if ((uint32)new_block + remaining > heap_state.current_end) {
            print("[HEAP] ERROR: Split would exceed heap bounds\n");
            return;
        }
        
        new_block->magic = BLOCK_MAGIC;
        new_block->size = remaining;
        new_block->status = BLOCK_FREE;
        new_block->next = NULL;
        new_block->prev = NULL;
        
        block->size = size;
        
        add_to_free_list(new_block);
        heap_state.block_count++;
    }
}



// Expand heap by allocating more pages
static memory_result_t expand_heap(uint32 needed_size) {
    // Validate expansion parameters
    if (needed_size == 0 || needed_size > heap_state.max_size) {
        return MEMORY_ERROR_INVALID_SIZE;
    }
    
    uint32 pages_needed = (needed_size + MEMORY_PAGE_SIZE - 1) / MEMORY_PAGE_SIZE;
    uint32 expand_size = pages_needed * MEMORY_PAGE_SIZE;
    
    if (heap_state.current_end + expand_size > heap_state.start_addr + heap_state.max_size) {
        print("[HEAP] ERROR: Heap expansion would exceed maximum size\n");
        return MEMORY_ERROR_OUT_OF_MEMORY; // Hit heap size limit
    }
    
    print("[HEAP] Expanding heap by "); print_dec(expand_size/1024); print(" KB\n");
    
    // Ensure TLB entries for this range are clean before mapping
    tlb_safe_heap_expand(heap_state.current_end, pages_needed);
    
    // Allocate and map new pages
    for (uint32 i = 0; i < pages_needed; i++) {
        uint32 phys_frame = pmm_alloc_frame();
        if (phys_frame == 0) {
            print("[HEAP] ERROR: PMM allocation failed during heap expansion\n");
            return MEMORY_ERROR_OUT_OF_MEMORY;
        }
        
        uint32 vaddr = heap_state.current_end + (i * MEMORY_PAGE_SIZE);
        memory_result_t result = vmm_map_page(vmm_get_current_page_directory(), 
                                            vaddr, phys_frame, 
                                            PAGE_PRESENT | PAGE_WRITABLE);
        
        if (result != MEMORY_OK) {
            print("[HEAP] ERROR: VMM mapping failed during heap expansion\n");
            return result;
        }
        
        // Ensure TLB knows about the new mapping
        tlb_invalidate_page(vaddr);
    }
    
    // Create a new free block from the expanded space
    heap_block_t* new_block = (heap_block_t*)heap_state.current_end;
    new_block->magic = BLOCK_MAGIC;
    new_block->size = expand_size;
    new_block->status = BLOCK_FREE;
    new_block->next = NULL;
    new_block->prev = NULL;
    
    heap_state.current_end += expand_size;
    heap_state.total_size += expand_size;
    heap_state.free_size += expand_size;
    heap_state.block_count++;
    
    add_to_free_list(new_block);
    
    return MEMORY_OK;
}

// =============================================================================
// PUBLIC INTERFACE IMPLEMENTATION
// =============================================================================

memory_result_t heap_init(uint32 start_addr, uint32 initial_size) {
    print("[HEAP] Initializing kernel heap...\n");
    
    // Validate parameters
    if (start_addr == 0 || (start_addr & MEMORY_PAGE_MASK) != 0) {
        return MEMORY_ERROR_INVALID_ADDR;
    }
    
    if (initial_size < MEMORY_PAGE_SIZE) {
        initial_size = MEMORY_PAGE_SIZE;
    }
    
    // Align initial size to page boundary
    initial_size = memory_align_up(initial_size, MEMORY_PAGE_SIZE);
    
    heap_state.start_addr = start_addr;
    heap_state.current_end = start_addr;
    heap_state.max_size = 16 * 1024 * 1024; // 16MB max heap
    heap_state.total_size = 0;
    heap_state.used_size = 0;
    heap_state.free_size = 0;
    heap_state.block_count = 0;
    heap_state.alloc_count = 0;
    heap_state.free_count = 0;
    heap_state.first_block = NULL;
    heap_state.free_list = NULL;
    
    print("[HEAP] Heap start: 0x");
    print_hex(start_addr);
    print(", initial size: ");
    print_dec(initial_size / 1024);
    print(" KB\n");
    
    // Allocate initial heap space
    memory_result_t result = expand_heap(initial_size);
    if (result != MEMORY_OK) {
        return result;
    }
    
    // The first block is already set up by expand_heap() and added to free_list
    heap_state.first_block = heap_state.free_list;
    heap_state.initialized = true;
    
    print("[HEAP] Initialization complete\n");
    
    return MEMORY_OK;
}

void* kmalloc(size_t size) {
    if (!heap_state.initialized || size == 0) {
        return NULL;
    }
    
    // Prevent extremely large allocations
    if (size > heap_state.max_size / 2) {
        print("[HEAP] ERROR: Allocation too large: "); print_dec(size); print("\n");
        return NULL;
    }
    
    // Calculate total size needed (including header)
    uint32 total_size = align_size(sizeof(heap_block_t) + size);
    
    // Find suitable free block
    heap_block_t* block = find_free_block(total_size);
    
    if (!block) {
        // Try to expand heap
        if (expand_heap(total_size) != MEMORY_OK) {
            print("[HEAP] ERROR: Failed to expand heap for size "); print_dec(total_size); print("\n");
            return NULL; // Out of memory
        }
        
        block = find_free_block(total_size);
        if (!block) {
            print("[HEAP] ERROR: No suitable block after heap expansion\n");
            return NULL; // Still no suitable block
        }
    }
    
    // Double-check block validity before use
    if (!is_valid_block(block)) {
        print("[HEAP] ERROR: Found invalid block during allocation\n");
        return NULL;
    }
    
    // Remove from free list
    remove_from_free_list(block);
    
    // Split block if it's too large
    split_block(block, total_size);
    
    // Update statistics
    heap_state.used_size += block->size;
    heap_state.free_size -= block->size;
    heap_state.alloc_count++;
    
    return block_to_data(block);
}

void* kmalloc_aligned(size_t size, uint32 alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return kmalloc(size); // Fall back to regular malloc for invalid alignment
    }
    
    // Allocate extra space for alignment adjustment
    uint32 extra_size = size + alignment + sizeof(heap_block_t);
    void* raw_ptr = kmalloc(extra_size);
    
    if (!raw_ptr) {
        return NULL;
    }
    
    // Calculate aligned address
    uint32 aligned_addr = memory_align_up((uint32)raw_ptr, alignment);
    
    // If already aligned, just return the raw pointer
    if (aligned_addr == (uint32)raw_ptr) {
        return raw_ptr;
    }
    
    // For now, just return the raw pointer
    // A more sophisticated implementation would create a new block at the aligned address
    return raw_ptr;
}

void* kzalloc(size_t size) {
    void* ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void kfree(void* ptr) {
    if (!heap_state.initialized || !ptr) {
        return;
    }
    
    // Validate pointer is within heap bounds
    if ((uint32)ptr < heap_state.start_addr + sizeof(heap_block_t) || 
        (uint32)ptr >= heap_state.current_end) {
        print("[HEAP] Error: Pointer outside heap bounds: 0x");
        print_hex((uint32)ptr);
        print("\n");
        return;
    }
    
    heap_block_t* block = data_to_block(ptr);
    
    if (!is_valid_block(block) || block->status != BLOCK_USED) {
        print("[HEAP] Error: Invalid free() on ");
        print_hex((uint32)ptr);
        print(" (block at 0x");
        print_hex((uint32)block);
        print(")\n");
        return;
    }
    
    // Update statistics
    heap_state.used_size -= block->size;
    heap_state.free_size += block->size;
    heap_state.free_count++;
    
    // Add to free list (which now handles coalescing)
    add_to_free_list(block);
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return kmalloc(new_size);
    }
    
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    // Validate input pointer bounds
    if ((uint32)ptr < heap_state.start_addr + sizeof(heap_block_t) || 
        (uint32)ptr >= heap_state.current_end) {
        print("[HEAP] ERROR: realloc on invalid pointer 0x"); print_hex((uint32)ptr); print("\n");
        return NULL;
    }
    
    heap_block_t* block = data_to_block(ptr);
    
    if (!is_valid_block(block) || block->status != BLOCK_USED) {
        print("[HEAP] ERROR: realloc on invalid or free block\n");
        return NULL;
    }
    
    uint32 old_data_size = block->size - sizeof(heap_block_t);
    
    // If new size fits in current block, just return the same pointer
    if (new_size <= old_data_size) {
        return ptr;
    }
    
    // Need to allocate a new larger block
    void* new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }
    
    // Copy old data
    memcpy(new_ptr, ptr, old_data_size);
    
    // Free old block
    kfree(ptr);
    
    return new_ptr;
}

void heap_get_stats(uint32* total_size, uint32* used_size, uint32* free_size) {
    if (total_size) *total_size = heap_state.total_size;
    if (used_size) *used_size = heap_state.used_size;
    if (free_size) *free_size = heap_state.free_size;
}