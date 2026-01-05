// =============================================================================
// MEMORY MANAGEMENT INITIALIZATION - FOREST OS v3.0  
// =============================================================================
// Linux-inspired memory management system integration and initialization
// Provides unified initialization and compatibility with existing Forest OS
// =============================================================================

#include "include/mm.h"
#include "include/atomic_mm.h"
#include "include/list.h"
#include "include/interrupt.h"
#include "include/system.h"
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// INITIALIZATION CONFIGURATION
// =============================================================================

// Feature enable flags (can be configured at build time)
#define MM_ENABLE_BUDDY         1       // Enable buddy allocator
#define MM_ENABLE_SLAB          1       // Enable SLAB allocator
#define MM_ENABLE_VMA           1       // Enable VMA management
#define MM_ENABLE_PAGECACHE     1       // Enable page cache
#define MM_ENABLE_RECLAIM       1       // Enable memory reclaim
#define MM_ENABLE_COW           1       // Enable copy-on-write
#define MM_ENABLE_OOM           1       // Enable OOM killer
#define MM_ENABLE_SWAP          1       // Enable swap support
#define MM_ENABLE_DEBUG         1       // Enable debug features

// Memory layout configuration
#define MM_ZONE_DMA_SIZE        (16 * 1024 * 1024)     // 16MB DMA zone
#define MM_ZONE_NORMAL_SIZE     (880 * 1024 * 1024)    // 880MB normal zone
#define MM_TOTAL_MEMORY_SIZE    (1024 * 1024 * 1024)   // 1GB total (example)

// =============================================================================
// GLOBAL INITIALIZATION STATE
// =============================================================================

// Memory management initialization state (struct mm_init_state defined in mm.h)
static struct mm_init_state mm_init_state = {0};

// Memory statistics for monitoring (struct mm_system_stats defined in mm.h)
static struct mm_system_stats mm_system_stats = {0};

// mem_map and mem_map_size are defined in mm_buddy.c

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

static int init_memory_zones(void);
static int init_buddy_allocator(void);
static int init_slab_allocator(void);
static int init_vma_system(void);
static int init_page_cache(void);
static int init_reclaim_system(void);
static int init_fault_handler(void);
static int init_debug_features(void);
static void update_system_stats(void);

// =============================================================================
// MAIN INITIALIZATION FUNCTION
// =============================================================================

/**
 * mm_init - Initialize the complete memory management system
 * 
 * This function initializes all memory management subsystems in the
 * correct order, ensuring dependencies are satisfied.
 */
int mm_init(void)
{
    int result;
    
    // Check if already initialized
    if (mm_init_state.mm_initialized) {
        return 0; // Already initialized
    }
    
    // Initialize memory zones first
    result = init_memory_zones();
    if (result != 0) {
        mm_init_state.last_error = "Failed to initialize memory zones";
        mm_init_state.init_errors++;
        return result;
    }
    
    // Initialize buddy allocator (provides physical pages)
    if (MM_ENABLE_BUDDY) {
        result = init_buddy_allocator();
        if (result != 0) {
            mm_init_state.last_error = "Failed to initialize buddy allocator";
            mm_init_state.init_errors++;
            return result;
        }
        mm_init_state.buddy_initialized = true;
    }
    
    // Initialize SLAB allocator (depends on buddy allocator)
    if (MM_ENABLE_SLAB && mm_init_state.buddy_initialized) {
        result = init_slab_allocator();
        if (result != 0) {
            mm_init_state.last_error = "Failed to initialize SLAB allocator";
            mm_init_state.init_errors++;
            return result;
        }
        mm_init_state.slab_initialized = true;
    }
    
    // Initialize VMA system (depends on SLAB for allocations)
    if (MM_ENABLE_VMA && mm_init_state.slab_initialized) {
        result = init_vma_system();
        if (result != 0) {
            mm_init_state.last_error = "Failed to initialize VMA system";
            mm_init_state.init_errors++;
            return result;
        }
        mm_init_state.vma_initialized = true;
    }
    
    // Initialize page cache (depends on buddy and SLAB)
    if (MM_ENABLE_PAGECACHE && mm_init_state.slab_initialized) {
        result = init_page_cache();
        if (result != 0) {
            mm_init_state.last_error = "Failed to initialize page cache";
            mm_init_state.init_errors++;
            return result;
        }
        mm_init_state.pagecache_initialized = true;
    }
    
    // Initialize memory reclaim system
    if (MM_ENABLE_RECLAIM && mm_init_state.pagecache_initialized) {
        result = init_reclaim_system();
        if (result != 0) {
            mm_init_state.last_error = "Failed to initialize reclaim system";
            mm_init_state.init_errors++;
            return result;
        }
        mm_init_state.reclaim_initialized = true;
    }
    
    // Initialize page fault handler (brings everything together)
    if (mm_init_state.vma_initialized) {
        result = init_fault_handler();
        if (result != 0) {
            mm_init_state.last_error = "Failed to initialize page fault handler";
            mm_init_state.init_errors++;
            return result;
        }
        mm_init_state.fault_handler_installed = true;
    }
    
    // Initialize debug features (optional)
    if (MM_ENABLE_DEBUG) {
        result = init_debug_features();
        if (result != 0) {
            // Debug failure is not fatal - continue
            mm_init_state.last_error = "Failed to initialize debug features";
            mm_init_state.init_errors++;
        }
    }
    
    // Initialize OOM killer and swap (optional)
    if (MM_ENABLE_OOM) {
        oom_init();
    }
    
    if (MM_ENABLE_SWAP) {
        swap_init();
    }
    
    if (MM_ENABLE_COW) {
        cow_init();
    }
    
    // Mark as fully initialized
    mm_init_state.mm_initialized = true;
    
    // Update system statistics
    update_system_stats();
    
    return 0;
}

// =============================================================================
// SUBSYSTEM INITIALIZATION FUNCTIONS
// =============================================================================

/**
 * init_memory_zones - Initialize memory zone information
 */
static int init_memory_zones(void)
{
    // TODO: Detect actual memory layout from hardware
    // For now, use configured values
    
    // This would typically:
    // 1. Parse memory map from bootloader
    // 2. Reserve kernel memory regions
    // 3. Set up zone boundaries
    // 4. Initialize zone descriptors
    
    return 0; // Success
}

/**
 * init_buddy_allocator - Initialize the buddy allocator
 */
static int init_buddy_allocator(void)
{
    int result;
    
    // Initialize buddy allocator
    result = buddy_init();
    if (result != 0) {
        return result;
    }
    
    // Add memory zones to buddy allocator
    // TODO: Use actual memory layout
    buddy_add_zone(ZONE_DMA, 0, MM_ZONE_DMA_SIZE / PAGE_SIZE);
    buddy_add_zone(ZONE_NORMAL, MM_ZONE_DMA_SIZE / PAGE_SIZE, 
                  (MM_ZONE_DMA_SIZE + MM_ZONE_NORMAL_SIZE) / PAGE_SIZE);
    
    return 0;
}

/**
 * init_slab_allocator - Initialize the SLAB allocator
 */
static int init_slab_allocator(void)
{
    return slab_init();
}

/**
 * init_vma_system - Initialize the VMA management system
 */
static int init_vma_system(void)
{
    // VMA system initialization is mostly done on-demand
    // when processes are created. No global initialization needed.
    return 0;
}

/**
 * init_page_cache - Initialize the page cache system
 */
static int init_page_cache(void)
{
    return page_cache_init();
}

/**
 * init_reclaim_system - Initialize memory reclaim and LRU management
 */
static int init_reclaim_system(void)
{
    return lru_init();
}

/**
 * init_fault_handler - Initialize and install the page fault handler
 */
static int init_fault_handler(void)
{
    // Install the enhanced page fault handler
    install_enhanced_page_fault_handler();
    return 0;
}

/**
 * init_debug_features - Initialize debugging and robustness features
 */
static int init_debug_features(void)
{
    return mm_debug_init();
}

// =============================================================================
// COMPATIBILITY LAYER
// =============================================================================

/**
 * mm_get_free_pages - Get number of free pages (compatibility function)
 */
unsigned long mm_get_free_pages(void)
{
    update_system_stats();
    return mm_system_stats.free_pages;
}

/**
 * mm_get_total_pages - Get total number of pages (compatibility function)
 */
unsigned long mm_get_total_pages(void)
{
    return mm_system_stats.total_pages;
}

/**
 * mm_get_memory_pressure - Get current memory pressure level
 */
unsigned long mm_get_memory_pressure(void)
{
    if (!mm_init_state.reclaim_initialized) {
        return 0;
    }
    
    return reclaim_get_pressure();
}

/**
 * mm_emergency_reclaim - Trigger emergency memory reclaim
 * @pages_needed: Number of pages needed
 * Returns: Number of pages actually reclaimed
 */
unsigned long mm_emergency_reclaim(unsigned long pages_needed)
{
    if (!mm_init_state.reclaim_initialized) {
        return 0;
    }
    
    return try_to_free_pages(GFP_KERNEL);
}

// =============================================================================
// SYSTEM MONITORING AND STATISTICS
// =============================================================================

/**
 * update_system_stats - Update system-wide memory statistics
 */
static void update_system_stats(void)
{
    // TODO: Gather actual statistics from each subsystem
    
    // Buddy allocator stats
    if (mm_init_state.buddy_initialized) {
        // mm_system_stats.free_pages = buddy_get_free_pages();
        // mm_system_stats.buddy_efficiency = buddy_get_efficiency();
    }
    
    // SLAB allocator stats  
    if (mm_init_state.slab_initialized) {
        // mm_system_stats.slab_pages = slab_get_used_pages();
        // mm_system_stats.slab_efficiency = slab_get_efficiency();
    }
    
    // Page cache stats
    if (mm_init_state.pagecache_initialized) {
        struct page_cache_stats *cache_stats = page_cache_get_stats();
        mm_system_stats.cached_pages = cache_stats->total_pages;
        
        if (cache_stats->cache_hits + cache_stats->cache_misses > 0) {
            mm_system_stats.cache_hit_rate = 
                (cache_stats->cache_hits * 100) / 
                (cache_stats->cache_hits + cache_stats->cache_misses);
        }
    }
    
    // TODO: Add more statistics gathering
}

/**
 * mm_get_system_stats - Get comprehensive system statistics
 */
struct mm_system_stats *mm_get_system_stats(void)
{
    update_system_stats();
    return &mm_system_stats;
}

/**
 * mm_print_system_info - Print comprehensive system information
 */
void mm_print_system_info(void)
{
    update_system_stats();
    
    // TODO: Implement system information printing
    // This would show:
    // - Total/free/used memory
    // - Memory zone information
    // - Cache statistics
    // - Allocation efficiency
    // - Recent OOM events
    // - Memory pressure levels
}

// =============================================================================
// ERROR HANDLING AND DIAGNOSTICS
// =============================================================================

/**
 * mm_get_init_status - Get initialization status information
 */
struct mm_init_state *mm_get_init_status(void)
{
    return &mm_init_state;
}

/**
 * mm_check_system_health - Perform system health check
 * Returns: 0 if healthy, negative value if problems detected
 */
int mm_check_system_health(void)
{
    int issues = 0;
    
    // Check if core systems are initialized
    if (!mm_init_state.buddy_initialized) {
        issues++;
    }
    
    if (!mm_init_state.slab_initialized) {
        issues++;
    }
    
    // Check memory pressure
    if (mm_get_memory_pressure() > 90) {
        issues++;
    }
    
    // Check for recent OOM events
    if (MM_ENABLE_OOM) {
        struct oom_stats *oom_stats = oom_get_stats();
        if (oom_stats->oom_events > 0) {
            issues++;
        }
    }
    
    // Check for memory leaks
    if (MM_ENABLE_DEBUG) {
        struct debug_stats *debug_stats = mm_debug_get_stats();
        if (debug_stats->leaks_detected > 100) {
            issues++;
        }
    }
    
    return (issues > 0) ? -issues : 0;
}

// =============================================================================
// SHUTDOWN AND CLEANUP
// =============================================================================

/**
 * mm_shutdown - Clean shutdown of memory management system
 */
void mm_shutdown(void)
{
    // This would be called during system shutdown to:
    // 1. Write out all dirty pages
    // 2. Disable page fault handler
    // 3. Clean up debug tracking
    // 4. Print final statistics
    
    if (mm_init_state.mm_initialized) {
        // TODO: Implement proper shutdown sequence
        mm_init_state.mm_initialized = false;
    }
}

// =============================================================================
// CONFIGURATION AND TUNING
// =============================================================================

/**
 * mm_configure_feature - Configure memory management features
 * @feature: Feature name to configure
 * @value: Configuration value
 */
int mm_configure_feature(const char *feature, unsigned long value)
{
    // TODO: Implement runtime feature configuration
    // This would allow tuning parameters like:
    // - Memory pressure thresholds
    // - Cache sizes
    // - Reclaim aggressiveness
    // - Debug verbosity
    
    return 0;
}

/**
 * mm_get_version_info - Get version and build information
 */
const char *mm_get_version_info(void)
{
    return "Forest OS Memory Management v3.0 - Linux-inspired implementation";
}

// =============================================================================
// STUB IMPLEMENTATIONS FOR MM FAULT HANDLING
// =============================================================================
// These are required by mm_fault.c and will need proper implementation

/**
 * get_current_mm - Get current process mm_struct
 * @return: Pointer to current process mm_struct or NULL
 */
mm_struct_t *get_current_mm(void)
{
    // TODO: Implement proper process tracking
    // For now return NULL indicating no process context
    return NULL;
}

/**
 * get_pte_from_address - Get PTE for virtual address
 * @mm: Memory descriptor
 * @address: Virtual address
 * @return: Pointer to PTE or NULL
 */
pte_t *get_pte_from_address(mm_struct_t *mm, unsigned long address)
{
    (void)mm;
    (void)address;
    // TODO: Implement page table walk
    return NULL;
}

/**
 * set_pte_at - Set PTE value at address
 * @mm: Memory descriptor
 * @address: Virtual address
 * @ptep: Pointer to PTE
 * @entry: New PTE value
 */
void set_pte_at(mm_struct_t *mm, unsigned long address,
                pte_t *ptep, pte_t entry)
{
    (void)mm;
    (void)address;
    if (ptep) {
        *ptep = entry;
    }
}

/**
 * update_mmu_cache - Update MMU TLB cache
 * @vma: VMA containing the address
 * @address: Virtual address
 * @ptep: Pointer to PTE
 */
void update_mmu_cache(vm_area_struct_t *vma, unsigned long address,
                      pte_t *ptep)
{
    (void)vma;
    (void)ptep;
    // Invalidate TLB for this address
    __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
}