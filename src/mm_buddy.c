#include "include/mm.h"
#include "include/list.h"
#include "include/atomic_mm.h"
#include "include/memory.h"  // For compatibility with existing system
#include "include/string.h"
#include "include/screen.h"
#include "include/spinlock.h"

// =============================================================================
// BUDDY ALLOCATOR IMPLEMENTATION
// =============================================================================
// Linux-style buddy allocator for efficient physical memory management
// Supports orders 0-11 (4KB to 8MB allocations)
// =============================================================================

// Maximum number of memory zones
#define MAX_NR_ZONES 3

// Memory zones array
static zone_t mem_zones[MAX_NR_ZONES];
static int nr_zones = 0;
page_t *mem_map = NULL;  // Array of page descriptors (global for mm.h)
unsigned long mem_map_size = 0;  // Size of mem_map array (global for mm.h)
static unsigned long max_pfn = 0;

// Buddy allocator state
static struct {
    bool initialized;
    unsigned long total_pages;
    unsigned long free_pages;
    spinlock_t zones_lock;
} buddy_state = { .initialized = false };

// =============================================================================
// INTERNAL HELPER FUNCTIONS
// =============================================================================

// Bit manipulation helpers
static inline int test_bit_local(int nr, volatile unsigned long *addr)
{
    return ((*addr) & (1UL << nr)) != 0;
}

static inline void set_bit_local(int nr, volatile unsigned long *addr)
{
    *addr |= (1UL << nr);
}

static inline void clear_bit_local(int nr, volatile unsigned long *addr)
{
    *addr &= ~(1UL << nr);
}

// Page flag manipulation (must be before other functions that use them)
#define PagePrivate(page)       test_bit_local(PG_PRIVATE, &(page)->flags)
#define __SetPagePrivate(page)  set_bit_local(PG_PRIVATE, &(page)->flags)
#define __ClearPagePrivate(page) clear_bit_local(PG_PRIVATE, &(page)->flags)

// Page order accessors
static inline unsigned int page_order_get(page_t *page)
{
    return page->order;
}

// Get buddy page frame number
static inline unsigned long calc_buddy_pfn(unsigned long pfn, unsigned int order)
{
    return pfn ^ (1UL << order);
}

// Forward declarations needed before page_is_buddy
page_t *pfn_to_page(unsigned long pfn);
unsigned long page_to_pfn(page_t *page);

// Check if page is buddy
static inline bool page_is_buddy(page_t *page, page_t *buddy, unsigned int order)
{
    if (PagePrivate(buddy) && page_order_get(buddy) == order) {
        if (page_to_pfn(page) + (1UL << order) == page_to_pfn(buddy)) {
            return true;
        }
        if (page_to_pfn(buddy) + (1UL << order) == page_to_pfn(page)) {
            return true;
        }
    }
    return false;
}

// Set page order
static inline void set_page_order(page_t *page, unsigned int order)
{
    page->order = order;
    __SetPagePrivate(page);
}

// Remove page order
static inline void rmv_page_order(page_t *page)
{
    __ClearPagePrivate(page);
    page->order = 0;
}

// =============================================================================
// PAGE DESCRIPTOR MANAGEMENT
// =============================================================================

// Convert page frame number to page descriptor
page_t *pfn_to_page(unsigned long pfn)
{
    if (pfn >= max_pfn || !mem_map) {
        return NULL;
    }
    return &mem_map[pfn];
}

// Convert page descriptor to page frame number
unsigned long page_to_pfn(page_t *page)
{
    if (!page || !mem_map) {
        return 0;
    }
    return page - mem_map;
}

// Get virtual address for page (identity mapped)
void *page_address(page_t *page)
{
    unsigned long pfn = page_to_pfn(page);
    return (void *)(pfn << PAGE_SHIFT);
}

// Initialize a page descriptor
static void init_page(page_t *page, unsigned long pfn)
{
    page->flags = 0;
    atomic_set(&page->refcount, 0);
    INIT_LIST_HEAD(&page->lru);
    page->order = 0;
    page->virtual = (void *)(pfn << PAGE_SHIFT);
}

// =============================================================================
// ZONE MANAGEMENT
// =============================================================================

// Find appropriate zone for allocation
static zone_t *find_zone(gfp_t gfp_mask)
{
    if (gfp_mask & __GFP_DMA) {
        // Find DMA zone
        for (int i = 0; i < nr_zones; i++) {
            if (mem_zones[i].type == ZONE_DMA) {
                return &mem_zones[i];
            }
        }
    }
    
    // Default to normal zone
    for (int i = 0; i < nr_zones; i++) {
        if (mem_zones[i].type == ZONE_NORMAL) {
            return &mem_zones[i];
        }
    }
    
    // Fallback to first available zone
    return nr_zones > 0 ? &mem_zones[0] : NULL;
}

// Add memory zone
void buddy_add_zone(zone_type_t type, unsigned long start_pfn, 
                   unsigned long end_pfn)
{
    if (nr_zones >= MAX_NR_ZONES) {
        print("[BUDDY] Warning: Maximum zones exceeded\n");
        return;
    }
    
    zone_t *zone = &mem_zones[nr_zones++];
    zone->type = type;
    zone->start_pfn = start_pfn;
    zone->spanned_pages = end_pfn - start_pfn;
    zone->present_pages = zone->spanned_pages;
    zone->managed_pages = zone->spanned_pages;
    zone->free_pages = 0;
    
    spinlock_init(&zone->lock, "zone");
    
    // Initialize free areas
    for (int order = 0; order <= BUDDY_MAX_ORDER; order++) {
        INIT_LIST_HEAD(&zone->free_area[order].free_list);
        zone->free_area[order].nr_free = 0;
    }
    
    // Set watermarks (simplified)
    zone->pages_min = zone->managed_pages / 128;  // ~0.8%
    zone->pages_low = zone->pages_min * 2;
    zone->pages_high = zone->pages_min * 3;
    
    print("[BUDDY] Added zone type "); print_dec((int)type);
    print(" PFN "); print_hex(start_pfn); print("-"); print_hex(end_pfn);
    print(" ("); print_dec(zone->spanned_pages); print(" pages)\n");
}

// =============================================================================
// BUDDY ALLOCATOR CORE
// =============================================================================

// Allocate pages from buddy allocator
static page_t *__alloc_pages_internal(zone_t *zone, unsigned int order)
{
    if (!zone || order > BUDDY_MAX_ORDER) {
        return NULL;
    }
    
    spinlock_acquire(&zone->lock);
    
    // Find a free block of the requested order or higher
    for (unsigned int current_order = order; current_order <= BUDDY_MAX_ORDER; current_order++) {
        free_area_t *area = &zone->free_area[current_order];
        
        if (!list_empty(&area->free_list)) {
            // Found a free block
            page_t *page = list_first_entry(&area->free_list, page_t, lru);
            list_del(&page->lru);
            rmv_page_order(page);
            area->nr_free--;
            
            // Split the block if it's larger than needed
            unsigned int split_order = current_order;
            while (split_order > order) {
                split_order--;
                
                // Get buddy page
                unsigned long buddy_pfn = page_to_pfn(page) + (1UL << split_order);
                page_t *buddy = pfn_to_page(buddy_pfn);
                
                if (buddy && buddy_pfn < zone->start_pfn + zone->spanned_pages) {
                    // Add buddy to free list
                    set_page_order(buddy, split_order);
                    list_add(&buddy->lru, &zone->free_area[split_order].free_list);
                    zone->free_area[split_order].nr_free++;
                }
            }
            
            // Update zone statistics
            zone->free_pages -= (1UL << order);
            buddy_state.free_pages -= (1UL << order);
            
            // Set page properties
            atomic_set(&page->refcount, 1);
            page->flags = 0;
            
            spinlock_release(&zone->lock);
            return page;
        }
    }
    
    spinlock_release(&zone->lock);
    return NULL; // No free blocks available
}

// Free pages to buddy allocator
static void __free_pages_internal(zone_t *zone, page_t *page, unsigned int order)
{
    if (!zone || !page || order > BUDDY_MAX_ORDER) {
        return;
    }
    
    unsigned long pfn = page_to_pfn(page);
    
    spinlock_acquire(&zone->lock);

    // Coalesce with buddy blocks
    while (order < BUDDY_MAX_ORDER) {
        unsigned long bpfn = calc_buddy_pfn(pfn, order);
        page_t *buddy = pfn_to_page(bpfn);

        if (!buddy || bpfn >= zone->start_pfn + zone->spanned_pages) {
            break; // No buddy or buddy outside zone
        }
        
        if (!page_is_buddy(page, buddy, order)) {
            break; // Buddy not free or wrong order
        }
        
        // Remove buddy from free list
        list_del(&buddy->lru);
        rmv_page_order(buddy);
        zone->free_area[order].nr_free--;
        
        // Coalesce - use lower PFN as new block start
        if (bpfn < pfn) {
            page = buddy;
            pfn = bpfn;
        }
        
        order++;
    }
    
    // Add coalesced block to appropriate free list
    set_page_order(page, order);
    list_add(&page->lru, &zone->free_area[order].free_list);
    zone->free_area[order].nr_free++;
    
    // Update statistics
    zone->free_pages += (1UL << order);
    buddy_state.free_pages += (1UL << order);
    
    // Reset page properties
    atomic_set(&page->refcount, 0);
    
    spinlock_release(&zone->lock);
}

// =============================================================================
// PUBLIC ALLOCATION INTERFACE
// =============================================================================

// Allocate multiple pages
page_t *alloc_pages(gfp_t gfp_mask, unsigned int order)
{
    if (!buddy_state.initialized || order > BUDDY_MAX_ORDER) {
        return NULL;
    }
    
    // Find appropriate zone
    zone_t *zone = find_zone(gfp_mask);
    if (!zone) {
        return NULL;
    }
    
    // Try allocation
    page_t *page = __alloc_pages_internal(zone, order);
    
    // Zero pages if requested
    if (page && (gfp_mask & __GFP_ZERO)) {
        void *addr = page_address(page);
        memset(addr, 0, PAGE_SIZE << order);
    }
    
    return page;
}

// Allocate single page
page_t *alloc_page(gfp_t gfp_mask)
{
    return alloc_pages(gfp_mask, 0);
}

// Free multiple pages
void __free_pages(page_t *page, unsigned int order)
{
    if (!page || !buddy_state.initialized) {
        return;
    }
    
    // Find which zone this page belongs to
    unsigned long pfn = page_to_pfn(page);
    zone_t *zone = NULL;
    
    for (int i = 0; i < nr_zones; i++) {
        if (pfn >= mem_zones[i].start_pfn && 
            pfn < mem_zones[i].start_pfn + mem_zones[i].spanned_pages) {
            zone = &mem_zones[i];
            break;
        }
    }
    
    if (!zone) {
        print("[BUDDY] Error: Page PFN "); print_hex(pfn); 
        print(" not in any zone\n");
        return;
    }
    
    __free_pages_internal(zone, page, order);
}

// Free single page
void free_page(page_t *page)
{
    __free_pages(page, 0);
}

// =============================================================================
// INITIALIZATION AND SETUP
// =============================================================================

// Initialize page descriptors
static int init_page_descriptors(unsigned long max_pfn_limit)
{
    // Calculate space needed for page descriptors
    size_t pages_size = max_pfn_limit * sizeof(page_t);
    size_t pages_needed = (pages_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    print("[BUDDY] Need "); print_dec(pages_size / 1024); 
    print(" KB for "); print_dec(max_pfn_limit); print(" page descriptors\n");
    
    // Allocate using existing PMM (will be replaced)
    uint32 mem_map_phys = 0;
    for (size_t i = 0; i < pages_needed; i++) {
        uint32 page = pmm_alloc_frame();
        if (page == 0) {
            print("[BUDDY] Failed to allocate page descriptor memory\n");
            return -1;
        }
        if (i == 0) {
            mem_map_phys = page;
        }
    }
    
    // Map to virtual address space
    mem_map = (page_t *)mem_map_phys;  // Direct mapping for now
    max_pfn = max_pfn_limit;
    
    // Initialize all page descriptors
    for (unsigned long pfn = 0; pfn < max_pfn; pfn++) {
        init_page(&mem_map[pfn], pfn);
    }
    
    print("[BUDDY] Initialized "); print_dec(max_pfn); 
    print(" page descriptors at 0x"); print_hex((uint32)mem_map); print("\n");
    
    return 0;
}

// Initialize all free pages in zones
static void init_zone_free_pages(void)
{
    print("[BUDDY] Initializing free pages in zones...\n");
    
    for (int zone_idx = 0; zone_idx < nr_zones; zone_idx++) {
        zone_t *zone = &mem_zones[zone_idx];
        
        print("[BUDDY] Zone "); print_dec(zone_idx); print(": ");
        print_hex(zone->start_pfn); print("-"); 
        print_hex(zone->start_pfn + zone->spanned_pages); print("\n");
        
        // Mark all pages in zone as free (largest possible blocks)
        unsigned long pfn = zone->start_pfn;
        unsigned long end_pfn = zone->start_pfn + zone->spanned_pages;
        
        while (pfn < end_pfn) {
            // Find largest block we can create
            unsigned int order = BUDDY_MAX_ORDER;
            unsigned long block_size = 1UL << order;
            
            while (order > 0 && (pfn & (block_size - 1)) != 0) {
                order--;
                block_size = 1UL << order;
            }
            
            while (order > 0 && pfn + block_size > end_pfn) {
                order--;
                block_size = 1UL << order;
            }
            
            if (pfn + block_size <= end_pfn) {
                // Add this block to free list
                page_t *page = pfn_to_page(pfn);
                set_page_order(page, order);
                list_add(&page->lru, &zone->free_area[order].free_list);
                zone->free_area[order].nr_free++;
                zone->free_pages += block_size;
                buddy_state.free_pages += block_size;
                
                pfn += block_size;
            } else {
                pfn++;
            }
        }
        
        print("[BUDDY] Zone "); print_dec(zone_idx); 
        print(" has "); print_dec(zone->free_pages); print(" free pages\n");
    }
}

// Main buddy allocator initialization
int buddy_init(void)
{
    print("[BUDDY] Initializing buddy allocator...\n");
    
    if (buddy_state.initialized) {
        print("[BUDDY] Already initialized\n");
        return 0;
    }
    
    spinlock_init(&buddy_state.zones_lock, "buddy_zones");
    buddy_state.total_pages = 0;
    buddy_state.free_pages = 0;
    
    // Get memory information from existing system
    uint32 total_memory_kb = memory_get_usable_kb();
    if (total_memory_kb == 0) {
        print("[BUDDY] No usable memory detected\n");
        return -1;
    }
    
    unsigned long total_pages = (total_memory_kb * 1024) / PAGE_SIZE;
    buddy_state.total_pages = total_pages;
    
    print("[BUDDY] Total memory: "); print_dec(total_memory_kb); 
    print(" KB ("); print_dec(total_pages); print(" pages)\n");
    
    // Initialize page descriptors
    if (init_page_descriptors(total_pages) < 0) {
        return -1;
    }
    
    // Add default zones based on memory size
    if (total_memory_kb >= 16 * 1024) {  // >= 16MB
        // DMA zone: 0-16MB
        buddy_add_zone(ZONE_DMA, 0, (16 * 1024 * 1024) / PAGE_SIZE);
        
        if (total_memory_kb > 16 * 1024) {
            // Normal zone: 16MB to end
            unsigned long normal_start = (16 * 1024 * 1024) / PAGE_SIZE;
            buddy_add_zone(ZONE_NORMAL, normal_start, total_pages);
        }
    } else {
        // Small system - single normal zone
        buddy_add_zone(ZONE_NORMAL, 0, total_pages);
    }
    
    // Initialize free pages in all zones
    init_zone_free_pages();
    
    buddy_state.initialized = true;
    
    print("[BUDDY] Initialization complete. Free pages: "); 
    print_dec(buddy_state.free_pages); print("\n");
    
    return 0;
}

// =============================================================================
// COMPATIBILITY INTERFACE
// =============================================================================

// Wrapper functions for compatibility with existing Forest OS MM

uint32 buddy_alloc_frame(void)
{
    page_t *page = alloc_page(GFP_KERNEL);
    if (!page) {
        return 0;
    }
    return (uint32)page_address(page);
}

uint32 buddy_alloc_frames(uint32 count)
{
    unsigned int order = 0;
    unsigned long pages = count;
    
    // Find order for this allocation
    while ((1UL << order) < pages) {
        order++;
    }
    
    if (order > BUDDY_MAX_ORDER) {
        return 0;
    }
    
    page_t *page = alloc_pages(GFP_KERNEL, order);
    if (!page) {
        return 0;
    }
    
    return (uint32)page_address(page);
}

void buddy_free_frame(uint32 addr)
{
    unsigned long pfn = addr >> PAGE_SHIFT;
    page_t *page = pfn_to_page(pfn);
    if (page) {
        free_page(page);
    }
}

void buddy_free_frames(uint32 addr, uint32 count)
{
    unsigned int order = 0;
    unsigned long pages = count;
    
    // Find order for this deallocation
    while ((1UL << order) < pages) {
        order++;
    }
    
    if (order > BUDDY_MAX_ORDER) {
        return;
    }
    
    unsigned long pfn = addr >> PAGE_SHIFT;
    page_t *page = pfn_to_page(pfn);
    if (page) {
        __free_pages(page, order);
    }
}