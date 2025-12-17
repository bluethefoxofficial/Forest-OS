#ifndef MEMORY_SAFE_H
#define MEMORY_SAFE_H

#include <stddef.h>
#include "types.h"
#include "memory.h"

// === MEMORY SAFETY CONSTANTS AND ENUMS ===

// Memory sizes - all powers of 2 to prevent alignment issues
#ifndef MEMORY_PAGE_SIZE
#define MEMORY_PAGE_SIZE        0x1000U      // 4KB
#endif
#ifndef MEMORY_PAGE_SHIFT
#define MEMORY_PAGE_SHIFT       12U
#endif
#ifndef MEMORY_PAGE_MASK
#define MEMORY_PAGE_MASK        (MEMORY_PAGE_SIZE - 1U)
#endif

// Memory limits - enforced to prevent overflow
#define MEMORY_MAX_USABLE_KB    (1024U * 1024U)  // 1GB max for 32-bit systems
#define MEMORY_MAX_USABLE_BYTES (MEMORY_MAX_USABLE_KB * 1024U)
#define MEMORY_MAX_FRAMES       (MEMORY_MAX_USABLE_BYTES / MEMORY_PAGE_SIZE)
#define MEMORY_MAX_BITMAP_SIZE  (16U * 1024U * 1024U)  // 16MB max bitmap

// Reserved memory regions - must never be allocated
#define MEMORY_RESERVED_LOW_START    0x00000000U   // Start of low memory
#define MEMORY_RESERVED_LOW_END      0x00100000U   // End at 1MB
#define MEMORY_VGA_START             0x000B8000U   // VGA text buffer start
#define MEMORY_VGA_END               0x000B9000U   // VGA text buffer end
#define MEMORY_BIOS_START            0x000F0000U   // BIOS area start
#define MEMORY_BIOS_END              0x00100000U   // BIOS area end

// Kernel memory layout - fixed and validated
#ifndef MEMORY_KERNEL_START
#define MEMORY_KERNEL_START          0x00100000U   // 1MB
#endif
#ifndef MEMORY_KERNEL_HEAP_START
#define MEMORY_KERNEL_HEAP_START     0x00400000U   // 4MB
#endif
#define MEMORY_USER_SPACE_START      0x40000000U   // 1GB
#define MEMORY_USER_STACK_TOP        0xC0000000U   // 3GB
#define MEMORY_KERNEL_SPACE_START    0xC0000000U   // Higher half kernel

// Frame state tracking - explicit state machine
typedef enum {
    FRAME_STATE_INVALID = 0,    // Invalid/uninitialized state
    FRAME_STATE_FREE = 1,       // Available for allocation
    FRAME_STATE_RESERVED = 2,   // Reserved (kernel, BIOS, etc)
    FRAME_STATE_ALLOCATED = 3,  // Allocated but not mapped
    FRAME_STATE_MAPPED = 4,     // Allocated and mapped to virtual memory
    FRAME_STATE_CORRUPTED = 255 // Detected corruption
} memory_frame_state_t;

// Page flags - use named constants instead of magic numbers
typedef enum {
    MEMORY_PAGE_FLAG_NONE = 0x00,
    MEMORY_PAGE_FLAG_PRESENT = 0x01,
    MEMORY_PAGE_FLAG_WRITABLE = 0x02,
    MEMORY_PAGE_FLAG_USER = 0x04,
    MEMORY_PAGE_FLAG_WRITE_THROUGH = 0x08,
    MEMORY_PAGE_FLAG_CACHE_DISABLE = 0x10,
    MEMORY_PAGE_FLAG_ACCESSED = 0x20,
    MEMORY_PAGE_FLAG_DIRTY = 0x40,
    MEMORY_PAGE_FLAG_PAT = 0x80,
    MEMORY_PAGE_FLAG_GLOBAL = 0x100
} memory_page_flags_t;

// Memory region types - validated against multiboot specification
typedef enum {
    MEMORY_REGION_INVALID = 0,
    MEMORY_REGION_AVAILABLE = 1,
    MEMORY_REGION_RESERVED = 2,
    MEMORY_REGION_ACPI_RECLAIM = 3,
    MEMORY_REGION_ACPI_NVS = 4,
    MEMORY_REGION_BADRAM = 5
} memory_region_type_t;

// Memory allocation flags - clear semantics
typedef enum {
    MEMORY_ALLOC_NONE = 0x00,
    MEMORY_ALLOC_KERNEL = 0x01,
    MEMORY_ALLOC_USER = 0x02,
    MEMORY_ALLOC_ZERO = 0x04,
    MEMORY_ALLOC_EXEC = 0x08,
    MEMORY_ALLOC_CONTIGUOUS = 0x10,
    MEMORY_ALLOC_URGENT = 0x20
} memory_alloc_flags_t;

// Memory validation results
typedef enum {
    MEMORY_VALIDATION_SUCCESS = 0,
    MEMORY_VALIDATION_NULL_POINTER,
    MEMORY_VALIDATION_MISALIGNED,
    MEMORY_VALIDATION_OUT_OF_BOUNDS,
    MEMORY_VALIDATION_REGION_OVERLAP,
    MEMORY_VALIDATION_SIZE_OVERFLOW,
    MEMORY_VALIDATION_CORRUPTED_METADATA,
    MEMORY_VALIDATION_INVALID_STATE_TRANSITION,
    MEMORY_VALIDATION_RESERVED_REGION_ACCESS
} memory_validation_result_t;

// === SAFE PAGE DIRECTORY/TABLE STRUCTURES ===

typedef page_entry_t memory_page_entry_t;
typedef page_table_t memory_page_table_t;
typedef page_directory_t memory_page_directory_t;

// === SAFE MEMORY REGION DESCRIPTOR ===

#ifndef MEMORY_REGION_T_DEFINED
typedef struct {
    uint64 base_address;           // Physical base address
    uint64 length;                 // Length in bytes
    memory_region_type_t type;     // Region type (validated)
    bool validated;                // Has been validated for safety
    bool usable;                   // Safe to allocate from
} memory_region_t;
#define MEMORY_REGION_T_DEFINED 1
#endif

// === PHYSICAL FRAME DESCRIPTOR ===

typedef struct {
    uint32 physical_address;       // Physical address (page-aligned)
    memory_frame_state_t state;    // Current state
    uint32 ref_count;              // Reference count (for sharing)
    uint32 flags;                  // Additional flags
    uint32 magic;                  // Corruption detection magic
} memory_frame_descriptor_t;

// === MEMORY STATISTICS ===

typedef struct {
    uint32 total_memory_bytes;
    uint32 usable_memory_bytes;
    uint32 reserved_memory_bytes;
    uint32 total_frames;
    uint32 free_frames;
    uint32 allocated_frames;
    uint32 mapped_frames;
    uint32 reserved_frames;
    uint32 corrupted_frames;
    uint32 largest_free_block_pages;
    uint32 fragmentation_percent;
    uint32 allocation_failures;
    uint32 double_free_attempts;
    uint32 invalid_free_attempts;
    uint32 out_of_memory_count;
} memory_statistics_t;

// === HEAP BLOCK STRUCTURE ===

#define MEMORY_HEAP_MAGIC_FREE      0xDEADBEEFU
#define MEMORY_HEAP_MAGIC_ALLOCATED 0xFEEDFACEU
#define MEMORY_HEAP_MAGIC_GUARD     0xCAFEBABEU

typedef struct memory_heap_block {
    uint32 magic_header;           // Corruption detection
    size_t size;                   // Block size including header/footer
    memory_frame_state_t state;    // Block state
    struct memory_heap_block* next; // Next block in free list
    struct memory_heap_block* prev; // Previous block in free list
    uint32 magic_footer;           // Footer corruption detection
} memory_heap_block_t;

// === VIRTUAL MEMORY AREA ===

typedef struct memory_vm_area {
    uint32 virtual_start;          // Virtual address start
    uint32 virtual_end;            // Virtual address end
    uint32 physical_start;         // Physical address start (if mapped)
    memory_alloc_flags_t flags;    // Access flags
    memory_frame_state_t state;    // Mapping state
    struct memory_vm_area* next;   // Next VMA
    uint32 magic;                  // Corruption detection
} memory_vm_area_t;

// === MEMORY SUBSYSTEM STATE ===

typedef struct {
    // Initialization state
    bool early_init_complete;
    bool pmm_init_complete;
    bool vmm_init_complete;
    bool heap_init_complete;
    
    // Physical memory manager
    uint32 total_physical_memory;
    uint32 usable_physical_memory;
    uint32 pmm_bitmap_address;
    uint32 pmm_bitmap_size;
    uint32 pmm_total_frames;
    
    // Virtual memory manager
    memory_page_directory_t* kernel_page_directory;
    memory_page_directory_t* current_page_directory;
    uint32 next_user_vaddr;
    
    // Heap manager
    uint32 heap_start_address;
    uint32 heap_current_end;
    uint32 heap_maximum_size;
    memory_heap_block_t* free_list_head;
    
    // Statistics and diagnostics
    memory_statistics_t stats;
    uint32 allocation_count;
    uint32 free_count;
    uint32 corruption_detected_count;
    
    // Safety and locks
    volatile bool memory_lock_held;
    uint32 lock_owner_address;
    uint32 system_integrity_checksum;
    
} memory_subsystem_state_t;

// === FUNCTION PROTOTYPES ===

// Memory validation initialization
memory_validation_result_t memory_validation_init(void);

// Memory subsystem initialization
memory_validation_result_t memory_early_init(void);
memory_validation_result_t memory_init_pmm(uint32 multiboot_magic, uint32 multiboot_addr);
memory_validation_result_t memory_init_vmm(void);
memory_validation_result_t memory_init_heap(void);
void memory_enable_paging(void);

// Physical memory management
uint32 memory_pmm_alloc_frame(void);
memory_validation_result_t memory_pmm_free_frame(uint32 frame_addr);
uint32 memory_pmm_alloc_frames(uint32 count);
memory_validation_result_t memory_pmm_free_frames(uint32 frame_addr, uint32 count);
bool memory_pmm_is_frame_available(uint32 frame_addr);
memory_frame_state_t memory_pmm_get_frame_state(uint32 frame_addr);

// Virtual memory management
memory_validation_result_t memory_vmm_map_page(memory_page_directory_t* dir, 
                                               uint32 virtual_addr, 
                                               uint32 physical_addr, 
                                               memory_page_flags_t flags);
memory_validation_result_t memory_vmm_unmap_page(uint32 virtual_addr);
uint32 memory_vmm_get_physical_address(uint32 virtual_addr);
bool memory_vmm_is_mapped(uint32 virtual_addr);
memory_page_directory_t* memory_vmm_create_page_directory(void);
memory_validation_result_t memory_vmm_destroy_page_directory(memory_page_directory_t* dir);

// Heap management
void* memory_heap_alloc(size_t size);
void* memory_heap_alloc_aligned(size_t size, uint32 alignment);
memory_validation_result_t memory_heap_free(void* ptr);
memory_validation_result_t memory_heap_check_integrity(void);

// Memory utilities and validation
uint32 memory_align_up(uint32 addr, uint32 alignment);
uint32 memory_align_down(uint32 addr, uint32 alignment);
bool memory_is_page_aligned(uint32 addr);
bool memory_is_address_valid(uint32 addr);
bool memory_is_range_valid(uint32 start, uint32 length);
memory_validation_result_t memory_validate_pointer(const void* ptr);
memory_validation_result_t memory_validate_region(uint32 start, uint32 length);
memory_validation_result_t memory_validate_region_descriptor(const memory_region_t* region);
memory_validation_result_t memory_validate_frame_address(uint32 frame_addr);

// Memory map processing
memory_validation_result_t memory_process_multiboot_map(uint32 multiboot_magic, 
                                                         uint32 multiboot_addr);
memory_region_t* memory_get_regions(uint32* count);
uint32 memory_get_usable_memory_size(void);

// Statistics and debugging
memory_statistics_t memory_get_statistics(void);
void memory_dump_statistics(void);
void memory_dump_page_tables(memory_page_directory_t* dir);
memory_validation_result_t memory_check_system_integrity(void);
void memory_dump_heap_state(void);
void memory_dump_frame_allocation_map(void);

// Error handling and panic
void memory_panic_with_details(const char* reason, uint32 addr, uint32 extra_info);
void memory_corruption_handler(uint32 addr, uint32 expected, uint32 actual);

#endif // MEMORY_SAFE_H
