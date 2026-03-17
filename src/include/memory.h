#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "errno_defs.h"
#include "types.h"

struct interrupt_frame;

/* Common page size alias */
#ifndef PAGE_SIZE
#define PAGE_SIZE MEMORY_PAGE_SIZE
#endif

/* Memory allocation flags for kernel memory allocation */
#ifndef GFP_KERNEL
#define GFP_KERNEL      0x01    /* Kernel memory allocation */
#define GFP_ATOMIC      0x02    /* Atomic allocation (no sleeping) */
#define GFP_USER        0x04    /* User memory allocation */
#define GFP_ZERO        0x08    /* Zero allocated memory */
#define GFP_DMA         0x10    /* DMA-capable memory */
#endif

// =============================================================================
// FOREST OS MEMORY MANAGEMENT SYSTEM v2.0
// =============================================================================
// A clean, reliable, and efficient memory management implementation
//
// Architecture:
// 1. Physical Memory Manager (PMM) - manages physical page frames
// 2. Virtual Memory Manager (VMM) - manages virtual address spaces and paging
// 3. Kernel Heap Manager (KHM) - manages dynamic kernel memory allocation
// 4. Memory Detection - detects and parses system memory map
//
// Design Principles:
// - Simple and reliable over complex and feature-rich
// - Clear separation of concerns
// - Minimal but effective validation
// - Predictable behavior
// - Easy to debug
// =============================================================================

// =============================================================================
// CONSTANTS AND CONFIGURATION
// =============================================================================

#define MEMORY_PAGE_SIZE        0x1000      // 4KB pages
#define MEMORY_PAGE_SHIFT       12
#define MEMORY_PAGE_MASK        0xFFF

// Bootstrap tuning to adapt to different RAM sizes
#define MEMORY_BOOTSTRAP_MIN_IDENTITY_KB (16 * 1024)   // 16MB minimum identity map
#define MEMORY_BOOTSTRAP_MAX_IDENTITY_KB (1024 * 1024)  // 1GB maximum bootstrap identity map (up to MEMORY_USER_START)
#define MEMORY_PRETOUCH_LIMIT_BYTES      (8 * 1024 * 1024) // Pre-touch only within first 8MB

// PMM guardrails
#define MEMORY_PMM_MAX_FRAMES (16 * 1024 * 1024) // Cap to 64GB worth of frames by default

// Memory layout constants
#define MEMORY_KERNEL_START     0x00100000  // 1MB - where kernel starts
#define MEMORY_PMM_START        0x00400000  // 4MB - where PMM bitmap starts
#define MEMORY_PMM_SIZE         0x40000     // 256KB for PMM bitmap
#define MEMORY_KERNEL_HEAP_START (MEMORY_PMM_START + MEMORY_PMM_SIZE) // Heap starts after PMM
#define MEMORY_KERNEL_HEAP_INITIAL_SIZE (1024 * 1024)    // 1MB bootstrap heap
#define MEMORY_KERNEL_HEAP_MAX_SIZE     (128 * 1024 * 1024) // 128MB max heap (tuned for large assets)
#define MEMORY_USER_START       0x40000000  // 1GB - user space starts
#define MEMORY_USER_END         0xC0000000  // 3GB - user space ends
#define USER_STACK_TOP          0xC0000000  // Top of user-mode stack
#define MEMORY_MAX_ADDR         0xFFFFF000  // Maximum addressable memory

// Page flags for page table entries
#define PAGE_PRESENT        0x001
#define PAGE_WRITABLE       0x002
#define PAGE_USER           0x004
#define PAGE_ACCESSED       0x020
#define PAGE_DIRTY          0x040
#define PAGE_CACHE_DISABLE  0x010
#define PAGE_WRITE_THROUGH  0x008

// Memory allocation flags
#define ALLOC_ZERO      0x001   // Zero the allocated memory
#define ALLOC_URGENT    0x002   // High priority allocation

// =============================================================================
// CORE DATA STRUCTURES
// =============================================================================

// Page table entry - matches x86 hardware format
typedef struct {
    uint32_t present     : 1;
    uint32_t writable    : 1; 
    uint32_t user        : 1;
    uint32_t pwt         : 1;  // Page write-through
    uint32_t pcd         : 1;  // Page cache disable
    uint32_t accessed    : 1;
    uint32_t dirty       : 1;
    uint32_t pat         : 1;  // Page attribute table
    uint32_t global      : 1;
    uint32_t avail       : 3;  // Available for OS use
    uint32_t frame       : 20; // Physical page frame number
} __attribute__((packed)) page_entry_t;

// Page directory and table structures
typedef page_entry_t page_table_t[1024];
typedef page_entry_t page_directory_t[1024];

// Helper to convert a page directory pointer to the physical address for CR3.
static inline uintptr_t vmm_pdir_phys(page_directory_t* dir) {
    return (uintptr_t)dir;
}

// Architecture-sized physical address helpers
#if defined(__x86_64__)
typedef uint64_t phys_addr_t;
typedef uint64_t frame_count_t;
#else
typedef uint32_t phys_addr_t;
typedef uint32_t frame_count_t;
#endif

// Memory region types (multiboot/e820 compatible)
#ifndef MEMORY_REGION_TYPE_T_DEFINED
typedef enum {
    MEMORY_REGION_INVALID = 0,
    MEMORY_REGION_AVAILABLE = 1,
    MEMORY_REGION_RESERVED = 2,
    MEMORY_REGION_ACPI_RECLAIM = 3,
    MEMORY_REGION_ACPI_NVS = 4,
    MEMORY_REGION_BADRAM = 5,
    MEMORY_REGION_KERNEL = 6,
    MEMORY_REGION_INITRD = 7
} memory_region_type_t;
#define MEMORY_REGION_TYPE_T_DEFINED 1
#endif

// Memory region information from bootloader
typedef struct {
    union {
        uint64_t base_address;   // Base physical address
        uint64_t base_addr;      // Alternate name for compatibility
    };
    uint64_t length;         // Length in bytes
    memory_region_type_t type;   // Region type
    bool validated;          // True when region has been validated
    bool usable;             // True when safe for allocations
    const char* description; // Optional human-readable description
} memory_region_t;
#define MEMORY_REGION_T_DEFINED 1

// Memory statistics
typedef struct {
    uint32_t total_memory_kb;      // Total detected memory
    uint32_t usable_memory_kb;     // Usable memory
    uint32_t total_frames;         // Total page frames
    uint32_t free_frames;          // Free page frames
    uint32_t used_frames;          // Used page frames
    uint32_t kernel_frames;        // Frames used by kernel
    uint32_t heap_size_kb;         // Current heap size
    uint32_t heap_used_kb;         // Used heap memory
    uint32_t heap_free_kb;         // Free heap memory
} memory_stats_t;

// Simple result codes
typedef enum {
    MEMORY_OK = 0,
    MEMORY_ERROR_NULL_PTR,
    MEMORY_ERROR_INVALID_ADDR,
    MEMORY_ERROR_OUT_OF_MEMORY,
    MEMORY_ERROR_ALREADY_MAPPED,
    MEMORY_ERROR_NOT_MAPPED,
    MEMORY_ERROR_INVALID_SIZE,
    MEMORY_ERROR_NOT_INITIALIZED
} memory_result_t;

// =============================================================================
// PHYSICAL MEMORY MANAGER (PMM)
// =============================================================================

// Initialize physical memory manager
memory_result_t pmm_init(memory_region_t* regions, uint32_t region_count);

// Allocate a single page frame
phys_addr_t pmm_alloc_frame(void);

// Allocate multiple contiguous page frames
phys_addr_t pmm_alloc_frames(uint32_t count);

// Best-effort allocation that can fall back to scattered frames when fragmented
memory_result_t pmm_alloc_scattered(uint32_t count, phys_addr_t* frames_out,
                                    uint32_t max_frames, uint32_t* allocated);

// Free a page frame
memory_result_t pmm_free_frame(phys_addr_t frame_addr);

// Free multiple page frames
memory_result_t pmm_free_frames(phys_addr_t frame_addr, uint32_t count);

// Check if frame is free
bool pmm_is_frame_free(phys_addr_t frame_addr);

// Get total number of frames
frame_count_t pmm_get_total_frames(void);

// Get number of free frames
frame_count_t pmm_get_free_frames(void);

// Reserve a range of physical memory (mark as used)
void pmm_reserve_range(phys_addr_t start_addr, phys_addr_t end_addr);

// =============================================================================
// VIRTUAL MEMORY MANAGER (VMM)
// =============================================================================

// Initialize virtual memory manager
memory_result_t vmm_init(void);

// Enable paging
void vmm_enable_paging(void);

// Create a new page directory
page_directory_t* vmm_create_page_directory(void);

// Destroy a page directory and free all associated frames
void vmm_destroy_page_directory(page_directory_t* dir);

// Switch to a page directory
void vmm_switch_page_directory(page_directory_t* dir);

// Get current page directory
page_directory_t* vmm_get_current_page_directory(void);

// Update the software-tracked current directory (does NOT switch CR3)
void vmm_set_current_directory(page_directory_t* dir);

// Map a virtual page to a physical frame
memory_result_t vmm_map_page(page_directory_t* dir, uint32_t vaddr, uint32_t paddr, uint32_t flags);

// Unmap a virtual page
memory_result_t vmm_unmap_page(page_directory_t* dir, uint32_t vaddr);

// Get physical address for virtual address
uint32_t vmm_get_physical_addr(page_directory_t* dir, uint32_t vaddr);

// Check if virtual address is mapped
bool vmm_is_mapped(page_directory_t* dir, uint32_t vaddr);

// Identity map a range (virtual = physical)
memory_result_t vmm_identity_map_range(page_directory_t* dir, uint32_t start, uint32_t end, uint32_t flags);

// =============================================================================
// KERNEL HEAP MANAGER (KHM)
// =============================================================================

// Initialize kernel heap
memory_result_t heap_init(uint32_t start_addr, uint32_t initial_size);

// Simple memory allocation functions (original Forest OS API)
void* khalloc_simple(size_t size);      // Simple heap allocation
void* khzalloc_simple(size_t size);     // Simple zeroed allocation

// Allocate memory from heap
void* kmalloc(size_t size);

// Allocate zeroed memory from heap
void* kzalloc(size_t size);

// Allocate aligned memory from heap
void* kmalloc_aligned(size_t size, uint32_t alignment);

// Note: For full Linux-compatible memory management, include mm.h instead

// Free memory to heap
void kfree(void* ptr);

// Reallocate memory
void* krealloc(void* ptr, size_t new_size);

// Get heap statistics
void heap_get_stats(uint32_t* total_size, uint32_t* used_size, uint32_t* free_size);

// =============================================================================
// MEMORY DETECTION AND INITIALIZATION
// =============================================================================

// Parse GRUB memory map
memory_result_t memory_detect_grub(uint32_t multiboot_magic, uint32_t multiboot_info);

// Initialize complete memory subsystem
memory_result_t memory_init(uint32_t multiboot_magic, uint32_t multiboot_info);

// Get memory regions detected by bootloader
memory_region_t* memory_get_regions(uint32_t* count);

// Get total usable memory
uint32_t memory_get_usable_kb(void);

// =============================================================================
// PAGE FAULT HANDLING
// =============================================================================

// Page fault handler
void page_fault_handler(struct interrupt_frame* frame, uint32_t error_code);

// =============================================================================
// UTILITIES AND DEBUGGING
// =============================================================================

// Alignment utilities
uint32_t memory_align_up(uint32_t addr, uint32_t align);
uint32_t memory_align_down(uint32_t addr, uint32_t align);
bool memory_is_aligned(uint32_t addr, uint32_t align);
uint32_t memory_get_cached_initrd_start(void);
uint32_t memory_get_cached_initrd_end(void);

// Get comprehensive memory statistics
memory_stats_t memory_get_stats(void);

// Dynamic layout helpers (PMM/heap base can shift if modules overlap defaults)
uint32 memory_get_pmm_start(void);
uint32 memory_get_pmm_size(void);
uint32 memory_get_kernel_heap_start(void);

// Dump memory information
void memory_dump_info(void);

// Dump page table information
void memory_dump_page_tables(page_directory_t* dir);
void memory_debug_report_fault(uint32_t fault_addr, uint32_t error_code);

// Check memory subsystem integrity
bool memory_check_integrity(void);

// Convert result codes to strings
const char* memory_result_to_string(memory_result_t result);

#endif // MEMORY_H
