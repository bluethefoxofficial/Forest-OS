#include "include/memory_safe.h"
#include "include/screen.h"
#include "include/panic.h"
#include <stdint.h>

// === MEMORY VALIDATION AND BOUNDS CHECKING ===

static bool memory_validation_initialized = false;
static uint32 memory_validation_checksum = 0;

// Internal validation state
typedef struct {
    uint32 magic;
    uint32 min_valid_address;
    uint32 max_valid_address;
    uint32 min_kernel_address;
    uint32 max_kernel_address;
    uint32 min_user_address;
    uint32 max_user_address;
    uint32 validation_count;
    uint32 error_count;
} memory_validation_state_t;

static memory_validation_state_t validation_state = {0};

#define VALIDATION_MAGIC 0x56414C49U  // "VALI"

// === INTERNAL VALIDATION HELPERS ===

static inline bool validate_alignment(uint32 addr, uint32 alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return false; // Alignment must be power of 2
    }
    return (addr & (alignment - 1)) == 0;
}

static inline bool validate_address_range_no_overflow(uint32 start, uint32 length) {
    // Check for overflow in address calculation
    if (length == 0) return false;
    if (start > UINT32_MAX - length) return false; // Overflow check
    return true;
}

static inline bool validate_frame_address(uint32 frame_addr) {
    // Frame must be page-aligned
    if (!validate_alignment(frame_addr, MEMORY_PAGE_SIZE)) return false;
    
    // Must be within valid memory range
    if (frame_addr < MEMORY_KERNEL_START) return false;
    if (frame_addr >= MEMORY_MAX_USABLE_BYTES) return false;
    
    return true;
}

static inline bool validate_virtual_address(uint32 virtual_addr) {
    // Check canonical form for 32-bit addresses
    // Virtual addresses must not fall in reserved ranges
    
    // VGA buffer is reserved
    if (virtual_addr >= MEMORY_VGA_START && virtual_addr < MEMORY_VGA_END) {
        return false;
    }
    
    // BIOS areas are reserved
    if (virtual_addr >= MEMORY_BIOS_START && virtual_addr < MEMORY_BIOS_END) {
        return false;
    }
    
    return true;
}

// === PUBLIC VALIDATION FUNCTIONS ===

memory_validation_result_t memory_validation_init(void) {
    if (memory_validation_initialized) {
        return MEMORY_VALIDATION_SUCCESS;
    }
    
    // Initialize validation state
    validation_state.magic = VALIDATION_MAGIC;
    validation_state.min_valid_address = MEMORY_KERNEL_START;
    validation_state.max_valid_address = MEMORY_MAX_USABLE_BYTES - 1;
    validation_state.min_kernel_address = MEMORY_KERNEL_START;
    validation_state.max_kernel_address = MEMORY_USER_SPACE_START - 1;
    validation_state.min_user_address = MEMORY_USER_SPACE_START;
    validation_state.max_user_address = MEMORY_USER_STACK_TOP - 1;
    validation_state.validation_count = 0;
    validation_state.error_count = 0;
    
    memory_validation_checksum = VALIDATION_MAGIC ^ validation_state.min_valid_address ^ 
                                validation_state.max_valid_address;
    
    memory_validation_initialized = true;
    return MEMORY_VALIDATION_SUCCESS;
}

memory_validation_result_t memory_validate_pointer(const void* ptr) {
    if (!memory_validation_initialized) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    validation_state.validation_count++;
    
    if (ptr == NULL) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_NULL_POINTER;
    }
    
    uint32 addr = (uint32)ptr;
    
    // Check basic range
    if (addr < validation_state.min_valid_address || 
        addr > validation_state.max_valid_address) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_OUT_OF_BOUNDS;
    }
    
    // Additional virtual address validation
    if (!validate_virtual_address(addr)) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_RESERVED_REGION_ACCESS;
    }
    
    return MEMORY_VALIDATION_SUCCESS;
}

memory_validation_result_t memory_validate_region(uint32 start, uint32 length) {
    if (!memory_validation_initialized) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    validation_state.validation_count++;
    
    // Check for overflow
    if (!validate_address_range_no_overflow(start, length)) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_SIZE_OVERFLOW;
    }
    
    uint32 end = start + length - 1;
    
    // Check bounds
    if (start < validation_state.min_valid_address || 
        end > validation_state.max_valid_address) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_OUT_OF_BOUNDS;
    }
    
    // Check for overlap with reserved regions
    if ((start >= MEMORY_VGA_START && start < MEMORY_VGA_END) ||
        (end >= MEMORY_VGA_START && end < MEMORY_VGA_END) ||
        (start < MEMORY_VGA_START && end >= MEMORY_VGA_END)) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_RESERVED_REGION_ACCESS;
    }
    
    if ((start >= MEMORY_BIOS_START && start < MEMORY_BIOS_END) ||
        (end >= MEMORY_BIOS_START && end < MEMORY_BIOS_END) ||
        (start < MEMORY_BIOS_START && end >= MEMORY_BIOS_END)) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_RESERVED_REGION_ACCESS;
    }
    
    return MEMORY_VALIDATION_SUCCESS;
}

memory_validation_result_t memory_validate_frame_address(uint32 frame_addr) {
    if (!memory_validation_initialized) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    validation_state.validation_count++;
    
    if (!validate_frame_address(frame_addr)) {
        validation_state.error_count++;
        if (!validate_alignment(frame_addr, MEMORY_PAGE_SIZE)) {
            return MEMORY_VALIDATION_MISALIGNED;
        } else {
            return MEMORY_VALIDATION_OUT_OF_BOUNDS;
        }
    }
    
    return MEMORY_VALIDATION_SUCCESS;
}

memory_validation_result_t memory_validate_page_entry(const memory_page_entry_t* entry) {
    if (!memory_validation_initialized) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    validation_state.validation_count++;
    
    memory_validation_result_t ptr_result = memory_validate_pointer(entry);
    if (ptr_result != MEMORY_VALIDATION_SUCCESS) {
        validation_state.error_count++;
        return ptr_result;
    }
    
    // If page is present, validate the frame number
    if (entry->present) {
        uint32 frame_addr = entry->frame * MEMORY_PAGE_SIZE;
        memory_validation_result_t frame_result = memory_validate_frame_address(frame_addr);
        if (frame_result != MEMORY_VALIDATION_SUCCESS) {
            validation_state.error_count++;
            return frame_result;
        }
    }
    
    return MEMORY_VALIDATION_SUCCESS;
}

memory_validation_result_t memory_validate_heap_block(const memory_heap_block_t* block) {
    if (!memory_validation_initialized) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    validation_state.validation_count++;
    
    memory_validation_result_t ptr_result = memory_validate_pointer(block);
    if (ptr_result != MEMORY_VALIDATION_SUCCESS) {
        validation_state.error_count++;
        return ptr_result;
    }
    
    // Validate magic numbers
    if (block->magic_header != MEMORY_HEAP_MAGIC_FREE &&
        block->magic_header != MEMORY_HEAP_MAGIC_ALLOCATED) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    if (block->magic_footer != MEMORY_HEAP_MAGIC_GUARD) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    // Validate size
    if (block->size < sizeof(memory_heap_block_t) || 
        block->size > MEMORY_MAX_USABLE_BYTES) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_SIZE_OVERFLOW;
    }
    
    // Validate state
    if (block->state != FRAME_STATE_FREE && block->state != FRAME_STATE_ALLOCATED) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_INVALID_STATE_TRANSITION;
    }
    
    return MEMORY_VALIDATION_SUCCESS;
}

memory_validation_result_t memory_validate_region_descriptor(const memory_region_t* region) {
    if (!memory_validation_initialized) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    validation_state.validation_count++;
    
    memory_validation_result_t ptr_result = memory_validate_pointer(region);
    if (ptr_result != MEMORY_VALIDATION_SUCCESS) {
        validation_state.error_count++;
        return ptr_result;
    }
    
    // Validate region type
    if (region->type < MEMORY_REGION_INVALID || region->type > MEMORY_REGION_BADRAM) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    // Check for overflow in region size
    if (!validate_address_range_no_overflow(region->base_address, region->length)) {
        validation_state.error_count++;
        return MEMORY_VALIDATION_SIZE_OVERFLOW;
    }
    
    // Validate base address alignment for usable regions
    if (region->type == MEMORY_REGION_AVAILABLE && region->usable) {
        if (!validate_alignment(region->base_address, MEMORY_PAGE_SIZE)) {
            validation_state.error_count++;
            return MEMORY_VALIDATION_MISALIGNED;
        }
    }
    
    return MEMORY_VALIDATION_SUCCESS;
}

bool memory_is_address_valid(uint32 addr) {
    return (memory_validate_pointer((void*)addr) == MEMORY_VALIDATION_SUCCESS);
}

bool memory_is_range_valid(uint32 start, uint32 length) {
    return (memory_validate_region(start, length) == MEMORY_VALIDATION_SUCCESS);
}

bool memory_is_page_aligned(uint32 addr) {
    return validate_alignment(addr, MEMORY_PAGE_SIZE);
}

uint32 memory_align_up(uint32 addr, uint32 alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return addr; // Invalid alignment, return unchanged
    }
    
    uint32 mask = alignment - 1;
    
    // Check for overflow
    if (addr > UINT32_MAX - mask) {
        return UINT32_MAX; // Return max value to prevent overflow
    }
    
    return (addr + mask) & ~mask;
}

uint32 memory_align_down(uint32 addr, uint32 alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return addr; // Invalid alignment, return unchanged
    }
    
    uint32 mask = alignment - 1;
    return addr & ~mask;
}

// === VALIDATION STATE CHECKING ===

memory_validation_result_t memory_check_validation_integrity(void) {
    if (!memory_validation_initialized) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    // Check magic number
    if (validation_state.magic != VALIDATION_MAGIC) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    // Check checksum
    uint32 calculated_checksum = VALIDATION_MAGIC ^ validation_state.min_valid_address ^ 
                                validation_state.max_valid_address;
    if (memory_validation_checksum != calculated_checksum) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    // Validate address ranges
    if (validation_state.min_valid_address >= validation_state.max_valid_address) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    if (validation_state.min_kernel_address >= validation_state.max_kernel_address) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    if (validation_state.min_user_address >= validation_state.max_user_address) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    return MEMORY_VALIDATION_SUCCESS;
}

void memory_get_validation_statistics(uint32* validation_count, uint32* error_count) {
    if (validation_count) {
        *validation_count = memory_validation_initialized ? validation_state.validation_count : 0;
    }
    if (error_count) {
        *error_count = memory_validation_initialized ? validation_state.error_count : 0;
    }
}

// === ERROR HANDLING ===

void memory_panic_with_details(const char* reason, uint32 addr, uint32 extra_info) {
    print("\n=== MEMORY SUBSYSTEM PANIC ===\n");
    print("Reason: ");
    print(reason);
    print("\n");
    print("Address: ");
    print_hex(addr);
    print("\n");
    print("Extra Info: ");
    print_hex(extra_info);
    print("\n");
    
    if (memory_validation_initialized) {
        print("Validation Count: ");
        print_dec(validation_state.validation_count);
        print("\n");
        print("Error Count: ");
        print_dec(validation_state.error_count);
        print("\n");
    }
    
    print("=== END PANIC DETAILS ===\n");
    PANIC("Memory subsystem failure");
}

void memory_corruption_handler(uint32 addr, uint32 expected, uint32 actual) {
    print("\n=== MEMORY CORRUPTION DETECTED ===\n");
    print("Address: ");
    print_hex(addr);
    print("\n");
    print("Expected: ");
    print_hex(expected);
    print("\n");
    print("Actual: ");
    print_hex(actual);
    print("\n");
    
    if (memory_validation_initialized) {
        validation_state.error_count++;
    }
    
    memory_panic_with_details("Memory corruption detected", addr, actual);
}

const char* memory_validation_result_to_string(memory_validation_result_t result) {
    switch (result) {
        case MEMORY_VALIDATION_SUCCESS:
            return "Success";
        case MEMORY_VALIDATION_NULL_POINTER:
            return "Null pointer";
        case MEMORY_VALIDATION_MISALIGNED:
            return "Misaligned address";
        case MEMORY_VALIDATION_OUT_OF_BOUNDS:
            return "Out of bounds";
        case MEMORY_VALIDATION_REGION_OVERLAP:
            return "Region overlap";
        case MEMORY_VALIDATION_SIZE_OVERFLOW:
            return "Size overflow";
        case MEMORY_VALIDATION_CORRUPTED_METADATA:
            return "Corrupted metadata";
        case MEMORY_VALIDATION_INVALID_STATE_TRANSITION:
            return "Invalid state transition";
        case MEMORY_VALIDATION_RESERVED_REGION_ACCESS:
            return "Reserved region access";
        default:
            return "Unknown error";
    }
}
