#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

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
#define MEMORY_BOOTSTRAP_MAX_IDENTITY_KB (128 * 1024)  // 128MB maximum bootstrap identity map
#define MEMORY_PRETOUCH_LIMIT_BYTES      (8 * 1024 * 1024) // Pre-touch only within first 8MB

// PMM guardrails
#define MEMORY_PMM_MAX_FRAMES (1024 * 1024) // Cap to 4GB worth of frames by default

// Memory layout constants
#define MEMORY_KERNEL_START     0x00100000  // 1MB - where kernel starts
#define MEMORY_PMM_START        0x00400000  // 4MB - where PMM bitmap starts
#define MEMORY_PMM_SIZE         0x40000     // 256KB for PMM bitmap
#define MEMORY_KERNEL_HEAP_START (MEMORY_PMM_START + MEMORY_PMM_SIZE) // Heap starts after PMM
#define MEMORY_USER_START       0x40000000  // 1GB - user space starts
#define USER_STACK_TOP          0xC0000000  // Top of user-mode stack
#define MEMORY_MAX_ADDR         0xFFFFF000  // Maximum addressable memory

// Page flags for page table entries
#define PAGE_PRESENT    0x001
#define PAGE_WRITABLE   0x002
#define PAGE_USER       0x004
#define PAGE_ACCESSED   0x020
#define PAGE_DIRTY      0x040

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

// Memory region information from bootloader
typedef struct {
    uint64_t base_address;   // Base physical address
    uint64_t length;         // Length in bytes
    uint32_t type;           // Type (1=available, 2=reserved, etc.)
    bool validated;          // True when region has been validated
    bool usable;             // True when safe for allocations
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
uint32_t pmm_alloc_frame(void);

// Allocate multiple contiguous page frames
uint32_t pmm_alloc_frames(uint32_t count);

// Best-effort allocation that can fall back to scattered frames when fragmented
memory_result_t pmm_alloc_scattered(uint32_t count, uint32_t* frames_out,
                                    uint32_t max_frames, uint32_t* allocated);

// Free a page frame
memory_result_t pmm_free_frame(uint32_t frame_addr);

// Free multiple page frames
memory_result_t pmm_free_frames(uint32_t frame_addr, uint32_t count);

// Check if frame is free
bool pmm_is_frame_free(uint32_t frame_addr);

// Get total number of frames
uint32_t pmm_get_total_frames(void);

// Get number of free frames
uint32_t pmm_get_free_frames(void);

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

// Allocate memory from heap
void* kmalloc(size_t size);

// Allocate aligned memory from heap
void* kmalloc_aligned(size_t size, uint32_t alignment);

// Allocate zeroed memory from heap
void* kzalloc(size_t size);

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
void page_fault_handler(uint32_t fault_addr, uint32_t error_code);

// =============================================================================
// UTILITIES AND DEBUGGING
// =============================================================================

// Alignment utilities
uint32_t memory_align_up(uint32_t addr, uint32_t align);
uint32_t memory_align_down(uint32_t addr, uint32_t align);
bool memory_is_aligned(uint32_t addr, uint32_t align);

// Get comprehensive memory statistics
memory_stats_t memory_get_stats(void);

// Dump memory information
void memory_dump_info(void);

// Dump page table information
void memory_dump_page_tables(page_directory_t* dir);

// Check memory subsystem integrity
bool memory_check_integrity(void);

// Convert result codes to strings
const char* memory_result_to_string(memory_result_t result);

#endif // MEMORY_H
