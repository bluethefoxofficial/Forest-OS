// =============================================================================
// OOM KILLER AND SWAP SUPPORT - FOREST OS v3.0
// =============================================================================
// Linux-inspired OOM killer and basic swap implementation
// Provides last-resort memory management when reclaim fails
// =============================================================================

#include "include/mm.h"
#include "include/memory.h"
#include "include/atomic_mm.h"
#include "include/list.h"
#include "include/interrupt.h"
#include "include/system.h"
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// OOM KILLER CONSTANTS AND CONFIGURATION
// =============================================================================

// OOM score adjustments
#define OOM_SCORE_ADJ_MIN       -1000   // Never kill
#define OOM_SCORE_ADJ_MAX       1000    // Always kill first
#define OOM_DISABLE             -17     // Disable OOM kill for process

// OOM thresholds
#define OOM_THRESHOLD_PAGES     64      // Trigger OOM when < 64 pages free
#define OOM_PANIC_THRESHOLD     8       // Panic when < 8 pages free

// Process importance factors
#define KERNEL_PROCESS_BONUS    (-500)  // Protect kernel processes
#define SYSTEM_PROCESS_BONUS    (-100)  // Protect important system processes
#define ROOT_PROCESS_BONUS      (-50)   // Slight protection for root processes

// Swap constants
#define SWAP_MAP_MAX            32768   // Maximum swap entries
#define SWAP_MAP_BAD            0xFFFF  // Bad swap entry marker
#define SWAP_CLUSTER_SIZE       256     // Swap cluster size

// =============================================================================
// DATA STRUCTURES
// =============================================================================

// Process descriptor (simplified for OOM killer)
struct oom_task {
    int pid;                    // Process ID
    char comm[16];              // Process name
    unsigned long total_vm;     // Total virtual memory
    unsigned long rss;          // Resident set size
    unsigned long swap_usage;   // Current swap usage
    int oom_score_adj;          // OOM score adjustment
    bool is_kernel_thread;      // Is this a kernel thread?
    bool is_init;              // Is this the init process?
    struct list_head list;      // List linkage
};

// Swap area descriptor
struct swap_info {
    int type;                   // Swap type identifier
    unsigned long pages;        // Total pages in swap
    unsigned long inuse_pages;  // Pages currently in use
    unsigned short *swap_map;   // Swap allocation map
    spinlock_t lock;           // Swap area lock
    
    // Cluster allocation optimization
    unsigned long cluster_next; // Next cluster to allocate from
    unsigned long cluster_nr;   // Current cluster number
    
    struct list_head list;      // List of swap areas
};

// =============================================================================
// GLOBAL OOM AND SWAP STATE
// =============================================================================

// OOM killer state
static struct oom_control {
    bool oom_in_progress;       // OOM killer is active
    unsigned long oom_count;    // Total OOM events
    unsigned long last_oom;     // Last OOM timestamp
    int last_victim_pid;        // Last process killed
    
    spinlock_t lock;           // OOM control lock
    struct list_head task_list; // List of tasks for OOM consideration
} oom_control = {0};

// Swap management
static struct swap_control {
    struct list_head swap_list; // List of swap areas
    unsigned long total_swap;   // Total swap space
    unsigned long used_swap;    // Used swap space
    spinlock_t lock;           // Swap control lock
    
    // Swap cache (simplified)
    struct list_head swap_cache[256]; // Simple hash table
    spinlock_t cache_locks[256];
} swap_control = {0};

// OOM statistics (struct oom_stats defined in mm.h)
static struct oom_stats oom_stats = {0};

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

static int oom_kill_process(struct oom_task *task);
static struct oom_task *select_oom_victim(void);
static unsigned long oom_score_process(struct oom_task *task);
static bool oom_should_trigger(void);
static void oom_update_task_list(void);

// Swap functions
static swp_entry_t swap_alloc_entry(void);
static void swap_free_entry(swp_entry_t entry);
static int swap_write_page(page_t *page, swp_entry_t entry);
static int swap_read_page(swp_entry_t entry, page_t *page);

// =============================================================================
// OOM KILLER INITIALIZATION
// =============================================================================

/**
 * oom_init - Initialize OOM killer subsystem
 */
int oom_init(void)
{
    // Initialize OOM control
    oom_control.oom_in_progress = false;
    oom_control.oom_count = 0;
    oom_control.last_victim_pid = -1;
    spin_lock_init(&oom_control.lock);
    INIT_LIST_HEAD(&oom_control.task_list);
    
    // Initialize statistics
    memset(&oom_stats, 0, sizeof(oom_stats));
    
    return 0;
}

/**
 * swap_init - Initialize swap subsystem
 */
int swap_init(void)
{
    int i;
    
    // Initialize swap control
    INIT_LIST_HEAD(&swap_control.swap_list);
    swap_control.total_swap = 0;
    swap_control.used_swap = 0;
    spin_lock_init(&swap_control.lock);
    
    // Initialize swap cache
    for (i = 0; i < 256; i++) {
        INIT_LIST_HEAD(&swap_control.swap_cache[i]);
        spin_lock_init(&swap_control.cache_locks[i]);
    }
    
    return 0;
}

// =============================================================================
// OOM TRIGGER AND VICTIM SELECTION
// =============================================================================

/**
 * oom_killer_trigger - Main OOM killer entry point
 * @gfp_mask: GFP allocation flags that triggered OOM
 * @order: Order of allocation that failed
 *
 * This function is called when memory allocation fails and reclaim
 * is unable to free sufficient memory.
 */
int oom_killer_trigger(gfp_t gfp_mask, unsigned int order)
{
    struct oom_task *victim;
    unsigned long freed_pages = 0;
    
    // Check if OOM killer should actually run
    if (!oom_should_trigger()) {
        oom_stats.false_alarms++;
        return 0; // Memory pressure not critical enough
    }
    
    // Prevent concurrent OOM killing
    spin_lock(&oom_control.lock);
    
    if (oom_control.oom_in_progress) {
        spin_unlock(&oom_control.lock);
        return 0; // Another OOM killer is already running
    }
    
    oom_control.oom_in_progress = true;
    oom_control.oom_count++;
    oom_stats.oom_events++;
    
    spin_unlock(&oom_control.lock);
    
    // Update task list for victim selection
    oom_update_task_list();
    
    // Select victim process
    victim = select_oom_victim();
    if (!victim) {
        // No suitable victim found - this could lead to panic
        if (reclaim_get_pressure() >= 95) {
            oom_stats.panic_events++;
            // TODO: Implement kernel panic for extreme OOM
            // kernel_panic("Out of memory: no killable process found");
        }
        
        spin_lock(&oom_control.lock);
        oom_control.oom_in_progress = false;
        spin_unlock(&oom_control.lock);
        return -1;
    }
    
    // Kill the selected victim
    freed_pages = oom_kill_process(victim);
    
    if (freed_pages > 0) {
        oom_stats.processes_killed++;
        oom_stats.pages_freed += freed_pages;
        oom_control.last_victim_pid = victim->pid;
    }
    
    // Clean up
    kfree(victim);
    
    spin_lock(&oom_control.lock);
    oom_control.oom_in_progress = false;
    spin_unlock(&oom_control.lock);
    
    return freed_pages;
}

/**
 * oom_should_trigger - Check if OOM killer should be triggered
 */
static bool oom_should_trigger(void)
{
    unsigned long free_pages = 0;
    unsigned long pressure;
    
    // Get current memory pressure
    pressure = reclaim_get_pressure();
    
    // TODO: Get actual free page count from zones
    free_pages = 100; // Placeholder
    
    // Trigger OOM if:
    // 1. Memory pressure is very high (>95%)
    // 2. Free pages below threshold
    // 3. Reclaim has failed to free memory
    
    if (pressure >= 95 && free_pages < OOM_THRESHOLD_PAGES) {
        return true;
    }
    
    // Emergency trigger if critically low
    if (free_pages < OOM_PANIC_THRESHOLD) {
        return true;
    }
    
    return false;
}

/**
 * select_oom_victim - Select the best process to kill
 */
static struct oom_task *select_oom_victim(void)
{
    struct oom_task *task, *victim = NULL;
    unsigned long highest_score = 0;
    unsigned long score;
    
    // Scan task list and find highest scoring process
    list_for_each_entry(task, &oom_control.task_list, list) {
        // Skip processes that should never be killed
        if (task->oom_score_adj == OOM_SCORE_ADJ_MIN) {
            continue;
        }
        
        // Skip init process
        if (task->is_init) {
            continue;
        }
        
        // Skip kernel threads (usually)
        if (task->is_kernel_thread && task->oom_score_adj >= 0) {
            continue;
        }
        
        score = oom_score_process(task);
        if (score > highest_score) {
            highest_score = score;
            victim = task;
        }
    }
    
    return victim;
}

/**
 * oom_score_process - Calculate OOM score for a process
 * @task: Process to score
 * Returns: OOM score (higher = more likely to be killed)
 */
static unsigned long oom_score_process(struct oom_task *task)
{
    unsigned long score;
    
    if (!task) {
        return 0;
    }
    
    // Base score on RSS (resident memory usage)
    score = task->rss;
    
    // Add virtual memory usage (with lower weight)
    score += task->total_vm / 4;
    
    // Add swap usage
    score += task->swap_usage / 2;
    
    // Apply OOM score adjustment
    if (task->oom_score_adj > 0) {
        score = score * (1000 + task->oom_score_adj) / 1000;
    } else if (task->oom_score_adj < 0) {
        score = score * 1000 / (1000 - task->oom_score_adj);
    }
    
    // Bonus/penalty factors
    if (task->is_kernel_thread) {
        score += KERNEL_PROCESS_BONUS;
    }
    
    // TODO: Add more sophisticated scoring
    // - Process age
    // - CPU usage
    // - Nice value
    // - Process group relationships
    
    return score;
}

/**
 * oom_kill_process - Kill a process to free memory
 * @task: Task to kill
 * Returns: Number of pages freed
 */
static int oom_kill_process(struct oom_task *task)
{
    unsigned long freed_pages;
    
    if (!task) {
        return 0;
    }
    
    freed_pages = task->rss + task->swap_usage;
    
    // TODO: Actually kill the process
    // This would involve:
    // 1. Sending SIGKILL to the process
    // 2. Cleaning up process memory
    // 3. Updating memory statistics
    
    // For now, just simulate the memory freeing
    // In a real implementation, this would interface with the
    // process management system
    
    return freed_pages;
}

/**
 * oom_update_task_list - Update the task list for OOM consideration
 */
static void oom_update_task_list(void)
{
    // TODO: Implement task list update
    // This would scan the process table and update the OOM task list
    // with current memory usage information
    
    // Placeholder implementation
    struct oom_task *task = kmalloc(sizeof(struct oom_task));
    if (task) {
        task->pid = 1234;
        task->rss = 1000;
        task->total_vm = 2000;
        task->swap_usage = 100;
        task->oom_score_adj = 0;
        task->is_kernel_thread = false;
        task->is_init = false;
        list_add(&task->list, &oom_control.task_list);
    }
}

// =============================================================================
// SWAP SUPPORT
// =============================================================================

/**
 * swap_out_page - Swap out a page to storage
 * @page: Page to swap out
 * Returns: 0 on success, negative error code on failure
 */
int swap_out_page(page_t *page)
{
    swp_entry_t entry;
    
    if (!page) {
        return -1;
    }
    
    // Allocate swap entry
    entry = swap_alloc_entry();
    if (entry.val == 0) {
        return -1; // No swap space available
    }
    
    // Write page to swap
    if (swap_write_page(page, entry) != 0) {
        swap_free_entry(entry);
        return -1;
    }
    
    // Update page table entry to point to swap
    // TODO: Update PTE to swap entry
    
    // Free the physical page
    free_page(page);
    
    return 0;
}

/**
 * swap_in_page - Swap in a page from storage
 * @entry: Swap entry
 * Returns: Page containing swapped data, or NULL on failure
 */
page_t *swap_in_page(swp_entry_t entry)
{
    page_t *page;
    
    if (entry.val == 0) {
        return NULL;
    }
    
    // Allocate new page
    page = alloc_page(GFP_KERNEL);
    if (!page) {
        return NULL;
    }
    
    // Read data from swap
    if (swap_read_page(entry, page) != 0) {
        free_page(page);
        return NULL;
    }
    
    // Free the swap entry
    swap_free_entry(entry);
    
    return page;
}

/**
 * swap_alloc_entry - Allocate a swap entry
 * Returns: Swap entry, or invalid entry on failure
 */
static swp_entry_t swap_alloc_entry(void)
{
    swp_entry_t entry = {0};
    struct swap_info *swap_info;
    unsigned long offset;
    
    spin_lock(&swap_control.lock);
    
    // Find a swap area with free space
    list_for_each_entry(swap_info, &swap_control.swap_list, list) {
        if (swap_info->inuse_pages >= swap_info->pages) {
            continue; // This swap area is full
        }
        
        // Find free slot in swap map
        for (offset = 0; offset < swap_info->pages; offset++) {
            if (swap_info->swap_map[offset] == 0) {
                // Found free slot
                swap_info->swap_map[offset] = 1;
                swap_info->inuse_pages++;
                swap_control.used_swap++;
                
                entry.val = (swap_info->type << 24) | offset;
                break;
            }
        }
        
        if (entry.val != 0) {
            break; // Found entry
        }
    }
    
    spin_unlock(&swap_control.lock);
    return entry;
}

/**
 * swap_free_entry - Free a swap entry
 * @entry: Swap entry to free
 */
static void swap_free_entry(swp_entry_t entry)
{
    struct swap_info *swap_info;
    unsigned long offset;
    int type;
    
    if (entry.val == 0) {
        return;
    }
    
    type = (entry.val >> 24) & 0xFF;
    offset = entry.val & 0xFFFFFF;
    
    spin_lock(&swap_control.lock);
    
    // Find the appropriate swap info
    list_for_each_entry(swap_info, &swap_control.swap_list, list) {
        if (swap_info->type == type) {
            if (offset < swap_info->pages) {
                swap_info->swap_map[offset] = 0;
                swap_info->inuse_pages--;
                swap_control.used_swap--;
            }
            break;
        }
    }
    
    spin_unlock(&swap_control.lock);
}

/**
 * swap_write_page - Write page data to swap storage
 * @page: Page to write
 * @entry: Swap entry destination
 */
static int swap_write_page(page_t *page, swp_entry_t entry)
{
    // TODO: Implement actual swap write
    // This would involve writing the page contents to swap storage
    // (file, block device, etc.)
    
    if (!page || entry.val == 0) {
        return -1;
    }
    
    // Placeholder - in a real implementation this would write
    // the page contents to the swap storage
    return 0;
}

/**
 * swap_read_page - Read page data from swap storage
 * @entry: Swap entry source
 * @page: Page to read into
 */
static int swap_read_page(swp_entry_t entry, page_t *page)
{
    // TODO: Implement actual swap read
    // This would involve reading the page contents from swap storage
    
    if (entry.val == 0 || !page) {
        return -1;
    }
    
    // Placeholder - in a real implementation this would read
    // the page contents from the swap storage
    return 0;
}

// =============================================================================
// STATISTICS AND MONITORING
// =============================================================================

/**
 * oom_get_stats - Get OOM killer statistics
 */
struct oom_stats *oom_get_stats(void)
{
    return &oom_stats;
}

/**
 * swap_get_info - Get swap usage information
 */
void swap_get_info(unsigned long *total, unsigned long *used, unsigned long *free)
{
    if (total) *total = swap_control.total_swap;
    if (used) *used = swap_control.used_swap;
    if (free) *free = swap_control.total_swap - swap_control.used_swap;
}

/**
 * oom_print_stats - Print OOM statistics
 */
void oom_print_stats(void)
{
    // TODO: Implement OOM statistics printing
}