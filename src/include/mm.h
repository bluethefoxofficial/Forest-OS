#ifndef MM_H
#define MM_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "atomic.h"
#include "spinlock.h"
#include "list.h"

// Forward declarations for types referenced by interrupt subsystem
#ifndef IRQ_RETURN_T_DEFINED
#define IRQ_RETURN_T_DEFINED
typedef enum {
    IRQ_NONE = 0,
    IRQ_HANDLED,
    IRQ_WAKE_THREAD,
    IRQ_SHARED_CONTINUE
} irq_return_t;
#endif

#ifndef GFP_T_DEFINED
#define GFP_T_DEFINED
typedef uint32_t gfp_t;          // Get Free Page flags
#endif

#ifndef PGOFF_T_DEFINED
#define PGOFF_T_DEFINED
typedef unsigned long pgoff_t;    // Page offset type
#endif

#ifndef PTE_T_DEFINED
#define PTE_T_DEFINED
typedef unsigned long pte_t;      // Page table entry
#endif

#ifndef PGD_T_DEFINED
#define PGD_T_DEFINED
typedef unsigned long pgd_t;      // Page global directory
#endif

#ifndef PGPROT_T_STRUCT_DEFINED
#define PGPROT_T_STRUCT_DEFINED
typedef struct { unsigned long pgprot; } pgprot_t;   // Page protection flags
#define pgprot_val(x)       ((x).pgprot)
#define __pgprot(x)         ((pgprot_t) { (x) })
#endif

// Additional forward declarations
struct list_head;
struct file;

// Red-black tree node (simplified)
#ifndef RB_NODE_DEFINED
#define RB_NODE_DEFINED
struct rb_node {
    struct rb_node *rb_parent;
    struct rb_node *rb_left;
    struct rb_node *rb_right;
    int rb_color;
};

#define RB_RED   0
#define RB_BLACK 1
#endif

// Red-black tree root
#ifndef RB_ROOT_DEFINED
#define RB_ROOT_DEFINED
struct rb_root {
    struct rb_node *rb_node;
};

#define RB_ROOT_INIT { .rb_node = NULL }
#define RB_ROOT ((struct rb_root) { NULL })
#endif

// Read-write semaphore (simplified)
#ifndef RW_SEMAPHORE_DEFINED
#define RW_SEMAPHORE_DEFINED
struct rw_semaphore {
    long count;
    spinlock_t wait_lock;
    struct list_head wait_list;
};

// Stub semaphore operations - to be replaced with real implementation
#ifndef init_rwsem
#define init_rwsem(sem) do { (sem)->count = 0; } while(0)
#endif
#ifndef down_read
#define down_read(sem)  do { (void)(sem); } while(0)
#endif
#ifndef up_read
#define up_read(sem)    do { (void)(sem); } while(0)
#endif
#ifndef down_write
#define down_write(sem) do { (void)(sem); } while(0)
#endif
#ifndef up_write
#define up_write(sem)   do { (void)(sem); } while(0)
#endif
#endif

// =============================================================================
// FOREST OS MEMORY MANAGEMENT SYSTEM v3.0 - LINUX INSPIRED
// =============================================================================
// A comprehensive, Linux-inspired memory management implementation
//
// Architecture Layers (bottom-up):
// 1. Buddy Allocator - Efficient physical memory allocation with coalescing
// 2. SLAB Allocator - Object caches for frequent kernel allocations  
// 3. Virtual Memory Areas (VMAs) - Per-process memory regions
// 4. Page Cache - Unified model for file-backed pages
// 5. Memory Reclaim - LRU-based page eviction
// 6. Copy-on-Write - Memory sharing and lazy allocation
// =============================================================================

// Page size constants
#ifndef PAGE_SIZE
#define PAGE_SIZE           4096
#endif
#ifndef PAGE_SHIFT
#define PAGE_SHIFT          12
#endif
#ifndef PAGE_MASK
#define PAGE_MASK           (PAGE_SIZE - 1)
#endif
#ifndef PAGES_PER_MB
#define PAGES_PER_MB        (1024 * 1024 / PAGE_SIZE)
#endif

// Buddy allocator constants
#define BUDDY_MAX_ORDER     11      // Max 2^11 pages = 8MB contiguous
#define BUDDY_MIN_ORDER     0       // Single pages

// SLAB allocator constants  
#define SLAB_MAX_SIZE       (PAGE_SIZE / 2)
#define SLAB_MIN_SIZE       32
#define SLAB_ALIGN_SIZE     8

// Memory zone types
typedef enum {
    ZONE_DMA = 0,       // < 16MB for legacy DMA
    ZONE_NORMAL,        // 16MB - 896MB
    ZONE_HIGHMEM,       // > 896MB (on 32-bit systems)
    MAX_NR_ZONES
} zone_type_t;

// Page frame flags
#define PG_LOCKED       (1 << 0)    // Page is locked
#define PG_REFERENCED   (1 << 1)    // Page was referenced
#define PG_UPTODATE     (1 << 2)    // Page data is valid
#define PG_DIRTY        (1 << 3)    // Page needs writeback
#define PG_LRU          (1 << 4)    // Page is on LRU list
#define PG_ACTIVE       (1 << 5)    // Page is on active LRU
#define PG_SLAB         (1 << 6)    // Page is used by SLAB
#define PG_RESERVED     (1 << 7)    // Page is reserved

// =============================================================================
// BUDDY ALLOCATOR
// =============================================================================

// Free area structure for buddy allocator
typedef struct free_area {
    struct list_head free_list;    // List of free blocks
    unsigned long nr_free;         // Number of free blocks
} free_area_t;

// Memory zone structure
typedef struct zone {
    zone_type_t type;
    unsigned long start_pfn;        // Start page frame number
    unsigned long spanned_pages;    // Total pages in zone
    unsigned long present_pages;    // Present pages (may have holes)
    unsigned long managed_pages;    // Managed by buddy allocator
    
    free_area_t free_area[BUDDY_MAX_ORDER + 1];
    unsigned long free_pages;       // Total free pages
    
    spinlock_t lock;               // Zone lock
    
    // Reclaim related
    unsigned long pages_min;        // Minimum free pages
    unsigned long pages_low;        // Low watermark
    unsigned long pages_high;       // High watermark
} zone_t;

// Page frame descriptor
typedef struct page {
    unsigned long flags;            // Page flags (PG_*)
    atomic_t refcount;             // Reference count
    
    union {
        // For buddy allocator
        struct {
            struct list_head lru;   // LRU list linkage
            unsigned int order;     // Buddy order (when free)
        };
        
        // For SLAB allocator
        struct {
            void *slab_cache;      // Pointer to slab cache
            void *freelist;        // Freelist pointer
        };
        
        // For page cache
        struct {
            struct address_space *mapping; // Address space
            pgoff_t index;         // Page index in mapping
        };
    };
    
    void *virtual;                 // Virtual address (if mapped)
} page_t;

// Physical memory initialization
int buddy_init(void);
void buddy_add_zone(zone_type_t type, unsigned long start_pfn, 
                   unsigned long end_pfn);

// Page allocation/deallocation
page_t *alloc_pages(gfp_t gfp_mask, unsigned int order);
page_t *alloc_page(gfp_t gfp_mask);
void __free_pages(page_t *page, unsigned int order);
void free_page(page_t *page);

// External page array (defined in mm_buddy.c)
extern page_t *mem_map;
extern unsigned long mem_map_size;

// Conversion functions (defined in mm_buddy.c)
unsigned long page_to_pfn(page_t *page);
page_t *pfn_to_page(unsigned long pfn);
void *page_address(page_t *page);

// =============================================================================
// SLAB ALLOCATOR
// =============================================================================

// SLAB cache flags
#define SLAB_POISON         0x00000001  // Poison objects
#define SLAB_RED_ZONE       0x00000002  // Red zones
#define SLAB_DEBUG_FREE     0x00000004  // Debug free objects
#define SLAB_HWCACHE_ALIGN  0x00000008  // Align to L1 cache line

// SLAB cache descriptor
typedef struct kmem_cache {
    struct list_head list;         // List of all caches
    const char *name;              // Cache name
    size_t size;                   // Object size
    size_t align;                  // Object alignment
    unsigned long flags;           // Cache flags
    
    // Per-CPU data
    struct kmem_cache_cpu *cpu_cache;
    
    // Node specific data  
    struct kmem_cache_node *node;
    
    // Slab management
    unsigned int object_size;      // Real object size
    unsigned int objects_per_slab; // Objects per slab
    unsigned int slab_size;        // Size of each slab
    
    // Statistics
    unsigned long num_allocations;
    unsigned long num_frees;
    unsigned long active_objs;     // Active objects
    unsigned long num_slabs;       // Total slabs
    
    // Constructor/destructor
    void (*ctor)(void *obj);
} kmem_cache_t;

// SLAB initialization
int slab_init(void);

// Cache management
kmem_cache_t *kmem_cache_create(const char *name, size_t size, size_t align,
                               unsigned long flags, void (*ctor)(void *));
void kmem_cache_destroy(kmem_cache_t *cache);

// Object allocation/deallocation
void *kmem_cache_alloc(kmem_cache_t *cache, gfp_t flags);
void kmem_cache_free(kmem_cache_t *cache, void *obj);

// General purpose allocators
void *kmalloc(size_t size, gfp_t flags);
void *kzalloc(size_t size, gfp_t flags);
void kfree(void *ptr);

// =============================================================================
// VIRTUAL MEMORY AREAS (VMA)
// =============================================================================

// VMA flags
#define VM_READ         0x00000001  // Pages can be read
#define VM_WRITE        0x00000002  // Pages can be written
#define VM_EXEC         0x00000004  // Pages can be executed
#define VM_SHARED       0x00000008  // Pages are shared
#define VM_MAYREAD      0x00000010  // VM_READ may be added
#define VM_MAYWRITE     0x00000020  // VM_WRITE may be added
#define VM_MAYEXEC      0x00000040  // VM_EXEC may be added
#define VM_MAYSHARE     0x00000080  // VM_SHARED may be added
#define VM_GROWSDOWN    0x00000100  // Stack area
#define VM_GROWSUP      0x00000200  // Heap area
#define VM_LOCKED       0x00002000  // Pages are locked in memory

// Virtual memory area
typedef struct vm_area_struct {
    unsigned long vm_start;        // Start virtual address
    unsigned long vm_end;          // End virtual address
    unsigned long vm_flags;        // VMA flags
    
    struct mm_struct *vm_mm;       // Associated mm_struct
    pgprot_t vm_page_prot;        // Page protection
    
    struct rb_node vm_rb;          // Red-black tree node
    struct list_head vm_list;      // List of VMAs
    
    // File backing
    struct file *vm_file;          // Associated file
    unsigned long vm_pgoff;        // Page offset in file
    
    // Operations
    const struct vm_operations_struct *vm_ops;
} vm_area_struct_t;

// Memory descriptor (per process)
typedef struct mm_struct {
    struct vm_area_struct *mmap;   // List of VMAs
    struct rb_root mm_rb;          // VMA red-black tree
    
    pgd_t *pgd;                   // Page global directory
    atomic_t mm_users;            // User count
    atomic_t mm_count;            // Reference count
    
    unsigned long total_vm;        // Total pages mapped
    unsigned long locked_vm;       // Locked pages
    unsigned long shared_vm;       // Shared pages
    unsigned long exec_vm;         // Executable pages
    unsigned long stack_vm;        // Stack pages
    
    unsigned long start_code;      // Start of code section
    unsigned long end_code;        // End of code section
    unsigned long start_data;      // Start of data section
    unsigned long end_data;        // End of data section
    unsigned long start_brk;       // Start of heap
    unsigned long brk;            // End of heap
    unsigned long start_stack;     // Start of stack
    
    spinlock_t page_table_lock;    // Page table lock
    struct rw_semaphore mmap_sem;  // VMA semaphore
} mm_struct_t;

// VMA management
mm_struct_t *mm_alloc(void);
void mm_free(mm_struct_t *mm);
vm_area_struct_t *find_vma(mm_struct_t *mm, unsigned long addr);
int do_munmap(mm_struct_t *mm, unsigned long start, size_t len);
unsigned long do_mmap(struct file *file, unsigned long addr, 
                      unsigned long len, unsigned long prot,
                      unsigned long flags, unsigned long offset);

// =============================================================================
// PAGE FAULT HANDLING
// =============================================================================

// Page fault error codes
#define FAULT_FLAG_WRITE    0x01    // Write access
#define FAULT_FLAG_NONLINEAR 0x02   // Non-linear mapping
#define FAULT_FLAG_MKWRITE  0x04    // Make page writable

// Page fault handler return codes
#define VM_FAULT_MINOR      0       // Minor fault
#define VM_FAULT_MAJOR      1       // Major fault (I/O required)
#define VM_FAULT_SIGBUS     2       // Send SIGBUS
#define VM_FAULT_OOM        3       // Out of memory

// Forward declaration for vm_fault (used in vm_operations_struct)
struct vm_fault;

// VMA operations
struct vm_operations_struct {
    void (*open)(vm_area_struct_t *vma);
    void (*close)(vm_area_struct_t *vma);
    int (*fault)(vm_area_struct_t *vma, struct vm_fault *vmf);
};

// Page fault context
struct vm_fault {
    unsigned long address;         // Faulting address
    unsigned int flags;           // Fault flags
    pte_t *ptep;                  // Page table entry
    pte_t orig_pte;               // Original PTE value
    page_t *page;                 // Allocated page
};

// Page fault entry points
struct interrupt_frame; // Forward declaration
struct interrupt_context; // Forward declaration
int mm_handle_page_fault(struct interrupt_frame *frame, unsigned long address,
                        unsigned long error_code);
irq_return_t enhanced_page_fault_handler(int vector, struct interrupt_context *ctx);
void install_enhanced_page_fault_handler(void);

// =============================================================================
// COPY-ON-WRITE SUPPORT
// =============================================================================

// COW page handling
int do_cow_fault(mm_struct_t *mm, vm_area_struct_t *vma, 
                unsigned long address, pte_t *ptep, pte_t entry);
int do_wp_page(mm_struct_t *mm, vm_area_struct_t *vma,
              unsigned long address, pte_t *ptep, pte_t entry);

// COW memory management
mm_struct_t *cow_copy_mm(mm_struct_t *mm);
int cow_share_page(mm_struct_t *mm, vm_area_struct_t *vma,
                  unsigned long address, page_t *page);

// COW statistics
struct cow_stats {
    unsigned long cow_faults;
    unsigned long pages_copied;
    unsigned long shared_pages;
    unsigned long cow_failures;
    unsigned long zero_page_cows;
};

struct cow_stats *cow_get_stats(void);
void cow_print_stats(void);
int cow_init(void);

// =============================================================================
// PAGE CACHE AND ADDRESS SPACES
// =============================================================================

// Read-ahead state
struct file_ra_state {
    pgoff_t start;              // Read-ahead window start
    unsigned int size;          // Read-ahead window size
    pgoff_t ahead_start;        // Async read-ahead start
    unsigned int ahead_size;    // Async read-ahead size
};

// Forward declarations
struct address_space;
struct address_space_operations;

// Address space operations
struct address_space_operations {
    int (*readpage)(page_t *page);
    int (*writepage)(page_t *page);
    int (*set_page_dirty)(page_t *page);
    void (*invalidatepage)(page_t *page, unsigned int offset, unsigned int length);
    int (*releasepage)(page_t *page, gfp_t gfp);
    int (*direct_IO)(int rw, struct address_space *mapping, 
                    unsigned long offset, unsigned long nr_pages);
};

// Address space descriptor
struct address_space {
    struct list_head clean_pages;       // Clean cached pages
    struct list_head dirty_pages;       // Dirty cached pages  
    struct list_head locked_pages;      // Locked cached pages
    
    atomic_t nr_pages;                  // Number of pages
    unsigned long flags;                // Address space flags
    
    spinlock_t lock;                    // Address space lock
    struct rw_semaphore rwsem;          // Read-write semaphore
    
    const struct address_space_operations *ops; // Operations
    void *private_data;                 // Private data (e.g., inode)
    
    struct file_ra_state readahead;     // Read-ahead state
};

// Page cache operations
int page_cache_init(void);
int address_space_init(struct address_space *mapping,
                      const struct address_space_operations *ops,
                      void *private_data);
void address_space_destroy(struct address_space *mapping);

// Page cache lookup and manipulation
page_t *page_cache_lookup(struct address_space *mapping, pgoff_t offset);
int page_cache_insert(struct address_space *mapping, page_t *page, pgoff_t offset);
void page_cache_remove(page_t *page);
page_t *page_cache_read(struct address_space *mapping, pgoff_t offset, gfp_t gfp_mask);
int page_cache_write(page_t *page);

// Read-ahead operations
void page_cache_sync_readahead(struct address_space *mapping,
                              struct file_ra_state *ra, pgoff_t offset);

// Page cache statistics
struct page_cache_stats {
    unsigned long total_pages;
    unsigned long clean_pages;
    unsigned long dirty_pages;
    unsigned long locked_pages;
    unsigned long cache_hits;
    unsigned long cache_misses;
    unsigned long readahead_pages;
    unsigned long writeout_pages;
    unsigned long allocation_failures;
    unsigned long evictions;
};

struct page_cache_stats *page_cache_get_stats(void);
void page_cache_print_stats(void);

// =============================================================================
// MEMORY RECLAIM
// =============================================================================

// LRU list types
enum lru_list {
    LRU_INACTIVE_ANON = 0,
    LRU_ACTIVE_ANON,
    LRU_INACTIVE_FILE,
    LRU_ACTIVE_FILE,
    NR_LRU_LISTS
};

// Reclaim control
struct scan_control {
    int priority;                  // Scan priority
    int may_writepage;            // May write dirty pages
    int may_unmap;                // May unmap pages
    int may_swap;                 // May swap pages
};

// LRU management
int lru_init(void);
void lru_add_page(page_t *page, zone_t *zone);
void lru_remove_page(page_t *page, zone_t *zone);
void page_mark_accessed(page_t *page);

// Memory pressure monitoring
bool memory_pressure_threshold(unsigned long threshold);
unsigned long reclaim_get_pressure(void);

// Reclaim functions
int try_to_free_pages(gfp_t gfp_mask);
int shrink_zone(zone_t *zone, struct scan_control *sc);

// Reclaim statistics
struct reclaim_stats {
    unsigned long pages_scanned;
    unsigned long pages_reclaimed;
    unsigned long pages_written;
    unsigned long scan_cycles;
    unsigned long pressure_events;
    unsigned long oom_events;
};

struct reclaim_stats *reclaim_get_stats(void);
void reclaim_print_stats(void);

// =============================================================================
// OOM KILLER
// =============================================================================

// OOM killer functions
int oom_init(void);
int oom_killer_trigger(gfp_t gfp_mask, unsigned int order);

// OOM statistics
struct oom_stats {
    unsigned long oom_events;
    unsigned long processes_killed;
    unsigned long pages_freed;
    unsigned long false_alarms;
    unsigned long panic_events;
};

struct oom_stats *oom_get_stats(void);
void oom_print_stats(void);

// =============================================================================
// SWAP SUPPORT
// =============================================================================

// Swap entry
typedef struct {
    unsigned long val;
} swp_entry_t;

// Swap operations
int swap_init(void);
int swap_out_page(page_t *page);
page_t *swap_in_page(swp_entry_t entry);
void swap_get_info(unsigned long *total, unsigned long *used, unsigned long *free);

// =============================================================================
// MEMORY DEBUGGING AND ROBUSTNESS
// =============================================================================

// Debug initialization
int mm_debug_init(void);

// Memory poisoning
void mm_poison_memory(void *addr, size_t size, unsigned long pattern);
bool mm_check_poison(const void *addr, size_t size, unsigned long pattern);

// Allocation tracking
void mm_track_allocation(void *addr, size_t size, const char *file, 
                        int line, const char *func);
bool mm_untrack_allocation(void *addr);

// Guard pages
int mm_create_guard_pages(unsigned long start, size_t size, const char *purpose);
void mm_remove_guard_pages(unsigned long start);
bool mm_handle_guard_page_fault(unsigned long address);

// Stack protection
int mm_setup_stack_guard(unsigned long stack_base, size_t stack_size);

// Leak detection
unsigned long mm_scan_for_leaks(void);

// Debug statistics
struct debug_stats {
    unsigned long total_allocations;
    unsigned long active_allocations;
    unsigned long total_frees;
    unsigned long double_frees;
    unsigned long guard_page_hits;
    unsigned long corruption_detected;
    unsigned long leaks_detected;
};

struct debug_stats *mm_debug_get_stats(void);
void mm_debug_print_stats(void);
void mm_debug_dump_allocations(void);

// Debug configuration
void mm_debug_enable_feature(const char *feature);
void mm_debug_set_poison_pattern(int type, unsigned long pattern);

// Debugging macros for tracked allocations
#define mm_malloc_debug(size) \
    mm_track_allocation(kmalloc(size, GFP_KERNEL), size, __FILE__, __LINE__, __FUNCTION__)
#define mm_free_debug(ptr) \
    do { if (mm_untrack_allocation(ptr)) kfree(ptr); } while(0)

// =============================================================================
// MEMORY INITIALIZATION
// =============================================================================

// Main memory subsystem initialization
int mm_init(void);

// Zone initialization
void zone_init(void);

// Memory detection and parsing
int parse_memory_map(void);

// Compatibility and monitoring functions
unsigned long mm_get_free_pages(void);
unsigned long mm_get_total_pages(void);
unsigned long mm_get_memory_pressure(void);
unsigned long mm_emergency_reclaim(unsigned long pages_needed);

// System statistics and monitoring
struct mm_system_stats {
    unsigned long total_pages;
    unsigned long free_pages;
    unsigned long cached_pages;
    unsigned long slab_pages;
    unsigned long user_pages;
    unsigned long buddy_efficiency;
    unsigned long slab_efficiency;
    unsigned long cache_hit_rate;
};

struct mm_system_stats *mm_get_system_stats(void);
void mm_print_system_info(void);

// System health and diagnostics
struct mm_init_state {
    bool mm_initialized;
    bool buddy_initialized;
    bool slab_initialized;
    bool vma_initialized;
    bool pagecache_initialized;
    bool reclaim_initialized;
    bool fault_handler_installed;
    unsigned long init_errors;
    const char *last_error;
};

struct mm_init_state *mm_get_init_status(void);
int mm_check_system_health(void);

// Configuration and shutdown
int mm_configure_feature(const char *feature, unsigned long value);
void mm_shutdown(void);
const char *mm_get_version_info(void);

#endif // MM_H