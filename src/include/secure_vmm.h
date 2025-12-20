#ifndef __SECURE_VMM_H__
#define __SECURE_VMM_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// SECURE VIRTUAL MEMORY MANAGER HEADER
// =============================================================================
// Advanced virtual memory management with protection and corruption detection
// =============================================================================

// Page table entry flags
#define VMM_PAGE_PRESENT        0x001
#define VMM_PAGE_WRITABLE       0x002
#define VMM_PAGE_USER           0x004
#define VMM_PAGE_WRITE_THROUGH  0x008
#define VMM_PAGE_CACHE_DISABLE  0x010
#define VMM_PAGE_ACCESSED       0x020
#define VMM_PAGE_DIRTY          0x040
#define VMM_PAGE_PAT            0x080
#define VMM_PAGE_GLOBAL         0x100
#define VMM_PAGE_NO_EXECUTE     0x8000000000000000ULL  // NX bit (if supported)

// Custom protection flags
#define VMM_PROT_GUARD          0x10000    // Guard page (causes fault on access)
#define VMM_PROT_STACK          0x20000    // Stack page (stack overflow detection)
#define VMM_PROT_HEAP           0x40000    // Heap page (heap corruption detection)
#define VMM_PROT_COW            0x80000    // Copy-on-write page
#define VMM_PROT_SHARED         0x100000   // Shared memory page

// Memory protection levels
typedef enum {
    VMM_PROT_NONE = 0,                          // No access
    VMM_PROT_READ = VMM_PAGE_PRESENT,           // Read only
    VMM_PROT_WRITE = VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE,  // Read/Write
    VMM_PROT_EXEC = VMM_PAGE_PRESENT,           // Execute (read implied)
    VMM_PROT_RW = VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE,     // Read/Write
    VMM_PROT_RX = VMM_PAGE_PRESENT,             // Read/Execute
    VMM_PROT_RWX = VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE     // Read/Write/Execute
} vmm_protection_t;

// Address space types
typedef enum {
    VMM_SPACE_KERNEL = 0,
    VMM_SPACE_USER = 1,
    VMM_SPACE_SHARED = 2,
    VMM_SPACE_DEVICE = 3
} vmm_address_space_type_t;

// Memory mapping types
typedef enum {
    VMM_MAP_ANONYMOUS,      // Anonymous memory (heap, stack)
    VMM_MAP_HEAP,          // Kernel heap allocation
    VMM_MAP_STACK,         // Stack mapping
    VMM_MAP_FILE,          // File-backed memory
    VMM_MAP_DEVICE,        // Device memory
    VMM_MAP_SHARED,        // Shared memory
    VMM_MAP_GUARD          // Guard pages
} vmm_mapping_type_t;

// Virtual memory area descriptor
typedef struct vmm_area {
    uint32_t start_addr;           // Start virtual address
    uint32_t end_addr;             // End virtual address
    vmm_protection_t protection;   // Protection flags
    vmm_mapping_type_t type;       // Mapping type
    uint32_t flags;                // Additional flags
    
    // Linked list for area management
    struct vmm_area* next;
    struct vmm_area* prev;
    
    // Corruption detection
    uint32_t magic;                // Magic number
    uint32_t checksum;             // Area integrity checksum
    
    // Statistics
    uint32_t access_count;         // Number of accesses
    uint32_t fault_count;          // Number of page faults
    uint32_t creation_time;        // When area was created
    
    // Debugging information
    const char* description;       // Human-readable description
    uint32_t allocation_id;        // Unique allocation ID
} vmm_area_t;

// Page table entry with extended information
typedef struct vmm_pte_extended {
    uint32_t pte;                  // Standard page table entry
    uint32_t physical_addr;        // Physical address (for validation)
    uint32_t access_count;         // Access tracking
    uint32_t last_access_time;     // Last access timestamp
    uint16_t protection_flags;     // Protection flags
    uint16_t corruption_canary;    // Corruption detection canary
} vmm_pte_extended_t;

// Address space descriptor
typedef struct vmm_address_space {
    uint32_t magic_header;         // Header magic
    uint32_t page_directory_phys;  // Physical address of page directory
    uint32_t* page_directory_virt; // Virtual address of page directory
    vmm_address_space_type_t type; // Address space type
    
    // Memory area management
    vmm_area_t* areas_head;        // Head of area list
    uint32_t area_count;           // Number of memory areas
    
    // Statistics
    uint32_t total_pages;          // Total mapped pages
    uint32_t user_pages;           // User-accessible pages
    uint32_t kernel_pages;         // Kernel-only pages
    uint32_t guard_pages;          // Guard pages
    uint32_t page_faults;          // Total page faults
    
    // Security features
    bool aslr_enabled;             // Address space layout randomization
    bool dep_enabled;              // Data execution prevention
    bool stack_guard_enabled;     // Stack guard pages
    bool heap_guard_enabled;      // Heap guard pages
    
    // Corruption detection
    uint32_t magic_footer;         // Footer magic
    uint32_t checksum;             // Address space checksum
} vmm_address_space_t;

// VMM configuration
typedef struct vmm_config {
    bool corruption_detection_enabled;
    bool access_tracking_enabled;
    bool guard_pages_enabled;
    bool aslr_enabled;
    bool dep_enabled;
    bool debug_mode_enabled;
    uint32_t kernel_heap_start;
    uint32_t kernel_heap_size;
    uint32_t user_space_start;
    uint32_t user_space_size;
} vmm_config_t;

// VMM statistics
typedef struct vmm_stats {
    uint32_t total_mappings;
    uint32_t active_mappings;
    uint32_t total_page_faults;
    uint32_t protection_violations;
    uint32_t guard_page_hits;
    uint32_t corruption_detected;
    uint32_t tlb_flushes;
    uint32_t cow_faults;
    uint32_t pages_allocated;
    uint32_t pages_freed;
    
    // Memory usage by type
    struct {
        uint32_t kernel_pages;
        uint32_t user_pages;
        uint32_t device_pages;
        uint32_t guard_pages;
        uint32_t shared_pages;
    } usage;
    
    // Protection violations by type
    struct {
        uint32_t read_violations;
        uint32_t write_violations;
        uint32_t execute_violations;
        uint32_t access_violations;
    } violations;
} vmm_stats_t;

// Initialization and configuration
void secure_vmm_init(const vmm_config_t* config);
void secure_vmm_enable_protection_features(bool aslr, bool dep, bool guards);
vmm_address_space_t* secure_vmm_create_address_space(vmm_address_space_type_t type);
void secure_vmm_destroy_address_space(vmm_address_space_t* space);

// Core mapping functions
int secure_vmm_map_pages(vmm_address_space_t* space, uint32_t vaddr, uint32_t paddr, 
                        uint32_t count, vmm_protection_t protection);
int secure_vmm_unmap_pages(vmm_address_space_t* space, uint32_t vaddr, uint32_t count);
int secure_vmm_protect_pages(vmm_address_space_t* space, uint32_t vaddr, uint32_t count, 
                            vmm_protection_t protection);

// Advanced mapping functions
uint32_t secure_vmm_allocate_region(vmm_address_space_t* space, uint32_t size, 
                                   vmm_protection_t protection, vmm_mapping_type_t type);
int secure_vmm_deallocate_region(vmm_address_space_t* space, uint32_t vaddr, uint32_t size);
int secure_vmm_create_guard_pages(vmm_address_space_t* space, uint32_t vaddr, uint32_t count);

// Memory area management
vmm_area_t* secure_vmm_create_area(vmm_address_space_t* space, uint32_t start, uint32_t size,
                                  vmm_protection_t protection, vmm_mapping_type_t type,
                                  const char* description);
int secure_vmm_remove_area(vmm_address_space_t* space, vmm_area_t* area);
vmm_area_t* secure_vmm_find_area(vmm_address_space_t* space, uint32_t vaddr);

// Page table management
uint32_t secure_vmm_virt_to_phys(vmm_address_space_t* space, uint32_t vaddr);
bool secure_vmm_is_mapped(vmm_address_space_t* space, uint32_t vaddr);
vmm_protection_t secure_vmm_get_protection(vmm_address_space_t* space, uint32_t vaddr);

// Validation and integrity
bool secure_vmm_validate_address_space(vmm_address_space_t* space);
bool secure_vmm_validate_mapping(vmm_address_space_t* space, uint32_t vaddr);
bool secure_vmm_check_corruption(vmm_address_space_t* space);
void secure_vmm_scan_for_corruption(vmm_address_space_t* space);

// Page fault handling
int secure_vmm_handle_page_fault(uint32_t fault_addr, uint32_t error_code);
void secure_vmm_register_fault_handler(int (*handler)(uint32_t addr, uint32_t error));

// Statistics and monitoring
void secure_vmm_get_stats(vmm_stats_t* stats);
void secure_vmm_dump_stats(void);
void secure_vmm_dump_address_space(vmm_address_space_t* space);
void secure_vmm_dump_memory_areas(vmm_address_space_t* space);

// Security features
int secure_vmm_enable_aslr(vmm_address_space_t* space);
int secure_vmm_enable_dep(vmm_address_space_t* space);
int secure_vmm_create_stack_guard(vmm_address_space_t* space, uint32_t stack_base, uint32_t stack_size);
int secure_vmm_create_heap_guard(vmm_address_space_t* space, uint32_t heap_base, uint32_t heap_size);

// Copy-on-write support
int secure_vmm_setup_cow_mapping(vmm_address_space_t* space, uint32_t vaddr, uint32_t count);
int secure_vmm_handle_cow_fault(vmm_address_space_t* space, uint32_t fault_addr);

// Debugging and diagnostics
void secure_vmm_dump_page_tables(vmm_address_space_t* space);
void secure_vmm_verify_page_table_integrity(vmm_address_space_t* space);
void secure_vmm_trace_memory_access(uint32_t vaddr, const char* operation);

// Utility macros
#define VMM_PAGE_ALIGN(addr) ((addr) & ~(4095))
#define VMM_PAGE_OFFSET(addr) ((addr) & 4095)
#define VMM_IS_PAGE_ALIGNED(addr) (((addr) & 4095) == 0)
#define VMM_PAGES_FOR_SIZE(size) (((size) + 4095) / 4096)

// Magic numbers for corruption detection
#define VMM_MAGIC_SPACE_HEADER  0x564D4D48  // "VMMH"
#define VMM_MAGIC_SPACE_FOOTER  0x564D4D46  // "VMMF"
#define VMM_MAGIC_AREA          0x564D4141  // "VMAA"
#define VMM_CORRUPTION_MARKER   0xDEADC0DE

#endif /* __SECURE_VMM_H__ */
