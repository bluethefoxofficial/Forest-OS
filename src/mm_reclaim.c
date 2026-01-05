// =============================================================================
// MEMORY RECLAIM AND LRU MANAGEMENT - FOREST OS v3.0
// =============================================================================
// Linux-inspired memory reclaim implementation for handling memory pressure
// Provides LRU-based page eviction and intelligent memory management
// =============================================================================

#include "include/mm.h"
#include "include/atomic_mm.h"
#include "include/list.h"
#include "include/interrupt.h"
#include "include/system.h"
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// RECLAIM CONSTANTS AND CONFIGURATION
// =============================================================================

// Reclaim priority levels (higher = more aggressive)
#define DEF_PRIORITY            12
#define MIN_PRIORITY            0
#define MAX_PRIORITY            15

// LRU list parameters
#define INACTIVE_RATIO          2       // Inactive:active ratio
#define RECLAIM_BATCH_SIZE      32      // Pages to reclaim in one batch
#define MAX_SCAN_BATCH          128     // Max pages to scan at once

// Memory pressure thresholds
#define PRESSURE_LOW            25      // 25% free memory - start reclaim
#define PRESSURE_MEDIUM         10      // 10% free memory - aggressive reclaim
#define PRESSURE_HIGH           5       // 5% free memory - emergency reclaim

// Page aging parameters
#define PAGE_AGE_MAX            7       // Maximum page age
#define PAGE_AGE_THRESHOLD      3       // Age threshold for eviction

// =============================================================================
// GLOBAL LRU LISTS AND STATE
// =============================================================================

// Per-zone LRU lists
struct lru_lists {
    struct list_head lists[NR_LRU_LISTS];
    spinlock_t locks[NR_LRU_LISTS];
    unsigned long nr_pages[NR_LRU_LISTS];
};

static struct lru_lists zone_lru[MAX_NR_ZONES];

// Global reclaim state
static struct reclaim_state {
    bool reclaim_active;            // Reclaim in progress
    unsigned long total_reclaimed;  // Total pages reclaimed
    unsigned long last_reclaim;     // Last reclaim timestamp
    unsigned long pressure_level;   // Current memory pressure (0-100)
    
    atomic_t nr_reclaimers;        // Number of active reclaimers
    atomic_t reclaim_requests;     // Pending reclaim requests
} reclaim_state = {0};

// Reclaim statistics (struct reclaim_stats defined in mm.h)
static struct reclaim_stats reclaim_stats = {0};

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

static unsigned long shrink_zone_lru(zone_t *zone, struct scan_control *sc);
static unsigned long shrink_inactive_list(struct list_head *list, 
                                         struct scan_control *sc);
static unsigned long shrink_active_list(struct list_head *list, 
                                       struct scan_control *sc);
static bool page_should_reclaim(page_t *page, struct scan_control *sc);
static int page_reclaim(page_t *page);
static void page_activate(page_t *page);
static void page_deactivate(page_t *page);
static unsigned long calculate_memory_pressure(void);

// =============================================================================
// LRU MANAGEMENT
// =============================================================================

/**
 * lru_init - Initialize LRU lists for all zones
 */
int lru_init(void)
{
    int zone, lru;
    
    for (zone = 0; zone < MAX_NR_ZONES; zone++) {
        for (lru = 0; lru < NR_LRU_LISTS; lru++) {
            INIT_LIST_HEAD(&zone_lru[zone].lists[lru]);
            spin_lock_init(&zone_lru[zone].locks[lru]);
            zone_lru[zone].nr_pages[lru] = 0;
        }
    }
    
    // Initialize reclaim state
    reclaim_state.reclaim_active = false;
    reclaim_state.total_reclaimed = 0;
    reclaim_state.pressure_level = 0;
    atomic_set(&reclaim_state.nr_reclaimers, 0);
    atomic_set(&reclaim_state.reclaim_requests, 0);
    
    memset(&reclaim_stats, 0, sizeof(reclaim_stats));
    
    return 0;
}

/**
 * lru_add_page - Add page to appropriate LRU list
 * @page: Page to add
 * @zone: Memory zone
 */
void lru_add_page(page_t *page, zone_t *zone)
{
    enum lru_list lru;
    
    if (!page || !zone) {
        return;
    }
    
    // Determine appropriate LRU list
    if (page->mapping) {
        // File-backed page
        if (page->flags & PG_ACTIVE) {
            lru = LRU_ACTIVE_FILE;
        } else {
            lru = LRU_INACTIVE_FILE;
        }
    } else {
        // Anonymous page
        if (page->flags & PG_ACTIVE) {
            lru = LRU_ACTIVE_ANON;
        } else {
            lru = LRU_INACTIVE_ANON;
        }
    }
    
    // Add to LRU list
    spin_lock(&zone_lru[zone->type].locks[lru]);
    list_add(&page->lru, &zone_lru[zone->type].lists[lru]);
    zone_lru[zone->type].nr_pages[lru]++;
    spin_unlock(&zone_lru[zone->type].locks[lru]);
    
    page->flags |= PG_LRU;
}

/**
 * lru_remove_page - Remove page from LRU list
 * @page: Page to remove
 * @zone: Memory zone
 */
void lru_remove_page(page_t *page, zone_t *zone)
{
    enum lru_list lru;
    
    if (!page || !zone || !(page->flags & PG_LRU)) {
        return;
    }
    
    // Determine LRU list
    if (page->mapping) {
        if (page->flags & PG_ACTIVE) {
            lru = LRU_ACTIVE_FILE;
        } else {
            lru = LRU_INACTIVE_FILE;
        }
    } else {
        if (page->flags & PG_ACTIVE) {
            lru = LRU_ACTIVE_ANON;
        } else {
            lru = LRU_INACTIVE_ANON;
        }
    }
    
    // Remove from LRU list
    spin_lock(&zone_lru[zone->type].locks[lru]);
    list_del(&page->lru);
    zone_lru[zone->type].nr_pages[lru]--;
    spin_unlock(&zone_lru[zone->type].locks[lru]);
    
    page->flags &= ~PG_LRU;
}

// =============================================================================
// PAGE AGING AND ACTIVATION
// =============================================================================

/**
 * page_activate - Mark page as active (frequently used)
 * @page: Page to activate
 */
static void page_activate(page_t *page)
{
    if (!page) {
        return;
    }
    
    // Mark page as active and referenced
    page->flags |= PG_ACTIVE | PG_REFERENCED;
    
    // Reset page age
    // TODO: Implement page age tracking
}

/**
 * page_deactivate - Mark page as inactive (candidate for eviction)
 * @page: Page to deactivate
 */
static void page_deactivate(page_t *page)
{
    if (!page) {
        return;
    }
    
    // Remove active flag
    page->flags &= ~PG_ACTIVE;
    
    // Clear referenced flag
    page->flags &= ~PG_REFERENCED;
}

/**
 * page_mark_accessed - Mark page as recently accessed
 * @page: Page that was accessed
 */
void page_mark_accessed(page_t *page)
{
    if (!page) {
        return;
    }
    
    // If page is already active, just mark as referenced
    if (page->flags & PG_ACTIVE) {
        page->flags |= PG_REFERENCED;
        return;
    }
    
    // If page was referenced before, activate it
    if (page->flags & PG_REFERENCED) {
        page_activate(page);
    } else {
        // First reference - just mark as referenced
        page->flags |= PG_REFERENCED;
    }
}

// =============================================================================
// MEMORY PRESSURE CALCULATION
// =============================================================================

/**
 * calculate_memory_pressure - Calculate current memory pressure level
 * Returns: Pressure level 0-100 (0 = no pressure, 100 = critical)
 */
static unsigned long calculate_memory_pressure(void)
{
    unsigned long total_free = 0;
    unsigned long total_pages = 0;
    unsigned long pressure;
    int zone;
    
    // Calculate total free and total pages across all zones
    for (zone = 0; zone < MAX_NR_ZONES; zone++) {
        // TODO: Get actual zone statistics
        // For now, use placeholder values
        total_free += 1000;  // Placeholder
        total_pages += 10000; // Placeholder
    }
    
    if (total_pages == 0) {
        return 100; // Critical pressure if no pages
    }
    
    // Calculate pressure percentage
    pressure = 100 - ((total_free * 100) / total_pages);
    
    return pressure;
}

/**
 * memory_pressure_threshold - Check if pressure exceeds threshold
 * @threshold: Pressure threshold (0-100)
 * Returns: true if pressure exceeds threshold
 */
bool memory_pressure_threshold(unsigned long threshold)
{
    unsigned long current_pressure = calculate_memory_pressure();
    reclaim_state.pressure_level = current_pressure;
    
    if (current_pressure >= threshold) {
        reclaim_stats.pressure_events++;
        return true;
    }
    
    return false;
}

// =============================================================================
// MAIN RECLAIM FUNCTIONS
// =============================================================================

/**
 * try_to_free_pages - Main reclaim entry point
 * @gfp_mask: GFP allocation flags
 * Returns: Number of pages reclaimed
 */
int try_to_free_pages(gfp_t gfp_mask)
{
    struct scan_control sc = {
        .priority = DEF_PRIORITY,
        .may_writepage = 1,
        .may_unmap = 1,
        .may_swap = 0  // No swap support yet
    };
    unsigned long total_reclaimed = 0;
    int zone;
    
    // Check if reclaim is already active
    if (reclaim_state.reclaim_active) {
        atomic_inc(&reclaim_state.reclaim_requests);
        return 0; // Another reclaimer is active
    }
    
    // Mark reclaim as active
    reclaim_state.reclaim_active = true;
    atomic_inc(&reclaim_state.nr_reclaimers);
    
    // Adjust scan control based on GFP flags
    if (gfp_mask & __GFP_WAIT) {
        sc.may_writepage = 1;
    } else {
        sc.may_writepage = 0;  // Atomic allocation - don't block
    }
    
    // Reclaim from all zones
    for (zone = 0; zone < MAX_NR_ZONES; zone++) {
        // TODO: Get actual zone pointer
        zone_t *zone_ptr = NULL; // Placeholder
        if (zone_ptr) {
            total_reclaimed += shrink_zone_lru(zone_ptr, &sc);
        }
    }
    
    // Update statistics
    reclaim_state.total_reclaimed += total_reclaimed;
    reclaim_stats.pages_reclaimed += total_reclaimed;
    reclaim_stats.scan_cycles++;
    
    // Mark reclaim as inactive
    reclaim_state.reclaim_active = false;
    atomic_dec(&reclaim_state.nr_reclaimers);
    
    return total_reclaimed;
}

/**
 * shrink_zone - Shrink a specific memory zone
 * @zone: Zone to shrink
 * @sc: Scan control parameters
 */
int shrink_zone(zone_t *zone, struct scan_control *sc)
{
    if (!zone || !sc) {
        return 0;
    }
    
    return shrink_zone_lru(zone, sc);
}

/**
 * shrink_zone_lru - Shrink zone's LRU lists
 * @zone: Zone to shrink
 * @sc: Scan control parameters
 */
static unsigned long shrink_zone_lru(zone_t *zone, struct scan_control *sc)
{
    unsigned long total_reclaimed = 0;
    unsigned long nr_to_scan;
    enum lru_list lru;
    
    if (!zone || !sc) {
        return 0;
    }
    
    // Scan inactive lists first (easier to reclaim)
    for (lru = 0; lru < NR_LRU_LISTS; lru += 2) { // Even indices are inactive
        nr_to_scan = zone_lru[zone->type].nr_pages[lru];
        nr_to_scan >>= sc->priority;  // Adjust based on priority
        
        if (nr_to_scan > MAX_SCAN_BATCH) {
            nr_to_scan = MAX_SCAN_BATCH;
        }
        
        if (nr_to_scan > 0) {
            total_reclaimed += shrink_inactive_list(
                &zone_lru[zone->type].lists[lru], sc);
        }
    }
    
    // If we need more pages, scan active lists
    if (total_reclaimed < RECLAIM_BATCH_SIZE) {
        for (lru = 1; lru < NR_LRU_LISTS; lru += 2) { // Odd indices are active
            nr_to_scan = zone_lru[zone->type].nr_pages[lru];
            nr_to_scan >>= (sc->priority + 2); // Be more conservative with active pages
            
            if (nr_to_scan > MAX_SCAN_BATCH / 2) {
                nr_to_scan = MAX_SCAN_BATCH / 2;
            }
            
            if (nr_to_scan > 0) {
                total_reclaimed += shrink_active_list(
                    &zone_lru[zone->type].lists[lru], sc);
            }
        }
    }
    
    return total_reclaimed;
}

// =============================================================================
// LRU LIST SHRINKING
// =============================================================================

/**
 * shrink_inactive_list - Shrink inactive LRU list
 * @list: LRU list to shrink
 * @sc: Scan control parameters
 */
static unsigned long shrink_inactive_list(struct list_head *list, 
                                         struct scan_control *sc)
{
    struct list_head *pos, *tmp;
    page_t *page;
    unsigned long reclaimed = 0;
    unsigned long scanned = 0;
    
    if (!list || !sc) {
        return 0;
    }
    
    list_for_each_safe(pos, tmp, list) {
        page = list_entry(pos, page_t, lru);
        
        scanned++;
        reclaim_stats.pages_scanned++;
        
        // Check if we should reclaim this page
        if (page_should_reclaim(page, sc)) {
            if (page_reclaim(page) == 0) {
                reclaimed++;
                
                if (reclaimed >= RECLAIM_BATCH_SIZE) {
                    break; // Reclaimed enough for now
                }
            }
        } else {
            // Page is not reclaimable - move to active list if referenced
            if (page->flags & PG_REFERENCED) {
                page_activate(page);
            }
        }
        
        if (scanned >= MAX_SCAN_BATCH) {
            break; // Scanned enough for now
        }
    }
    
    return reclaimed;
}

/**
 * shrink_active_list - Shrink active LRU list
 * @list: Active LRU list to shrink
 * @sc: Scan control parameters
 */
static unsigned long shrink_active_list(struct list_head *list, 
                                       struct scan_control *sc)
{
    struct list_head *pos, *tmp;
    page_t *page;
    unsigned long deactivated = 0;
    unsigned long scanned = 0;
    
    if (!list || !sc) {
        return 0;
    }
    
    // Scan active list and deactivate unreferenced pages
    list_for_each_safe(pos, tmp, list) {
        page = list_entry(pos, page_t, lru);
        
        scanned++;
        reclaim_stats.pages_scanned++;
        
        // If page was not recently referenced, deactivate it
        if (!(page->flags & PG_REFERENCED)) {
            page_deactivate(page);
            deactivated++;
        } else {
            // Clear referenced bit for next scan
            page->flags &= ~PG_REFERENCED;
        }
        
        if (scanned >= MAX_SCAN_BATCH / 2) {
            break; // Don't scan too many active pages at once
        }
    }
    
    return 0; // Active list shrinking doesn't immediately reclaim pages
}

// =============================================================================
// PAGE RECLAIM DECISION AND EXECUTION
// =============================================================================

/**
 * page_should_reclaim - Check if page should be reclaimed
 * @page: Page to check
 * @sc: Scan control parameters
 */
static bool page_should_reclaim(page_t *page, struct scan_control *sc)
{
    if (!page || !sc) {
        return false;
    }
    
    // Don't reclaim locked pages
    if (page->flags & PG_LOCKED) {
        return false;
    }
    
    // Don't reclaim pages with multiple references (unless under pressure)
    if (atomic_read(&page->refcount) > 1 && sc->priority > 5) {
        return false;
    }
    
    // Don't reclaim dirty pages unless we can write them
    if ((page->flags & PG_DIRTY) && !sc->may_writepage) {
        return false;
    }
    
    // Don't reclaim mapped pages unless we can unmap them
    if (page->mapping && !sc->may_unmap) {
        return false;
    }
    
    return true;
}

/**
 * page_reclaim - Actually reclaim a page
 * @page: Page to reclaim
 * Returns: 0 on success, negative error code on failure
 */
static int page_reclaim(page_t *page)
{
    if (!page) {
        return -1;
    }
    
    // If page is dirty, write it out
    if (page->flags & PG_DIRTY) {
        if (page->mapping && page->mapping->ops && page->mapping->ops->writepage) {
            if (page->mapping->ops->writepage(page) != 0) {
                return -1; // Write failed
            }
            reclaim_stats.pages_written++;
        }
    }
    
    // Remove from page cache if present
    if (page->mapping) {
        page_cache_remove(page);
    }
    
    // Remove from LRU list
    list_del(&page->lru);
    page->flags &= ~PG_LRU;
    
    // Free the page
    free_page(page);
    
    return 0;
}

// =============================================================================
// RECLAIM STATISTICS AND MONITORING
// =============================================================================

/**
 * reclaim_get_stats - Get reclaim statistics
 */
struct reclaim_stats *reclaim_get_stats(void)
{
    return &reclaim_stats;
}

/**
 * reclaim_print_stats - Print reclaim statistics
 */
void reclaim_print_stats(void)
{
    // TODO: Implement reclaim statistics printing
    // This would be useful for performance monitoring
}

/**
 * reclaim_get_pressure - Get current memory pressure level
 */
unsigned long reclaim_get_pressure(void)
{
    return calculate_memory_pressure();
}