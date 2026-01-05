// =============================================================================
// PAGE CACHE AND UNIFIED PAGE MODEL - FOREST OS v3.0
// =============================================================================
// Linux-inspired page cache implementation providing unified page management
// Handles file-backed pages, anonymous memory, and cached file data
// =============================================================================

#include "include/mm.h"
#include "include/atomic_mm.h"
#include "include/list.h"
#include "include/interrupt.h"
#include "include/system.h"
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// PAGE CACHE CONSTANTS AND STRUCTURES
// =============================================================================

// Address space flags
#define AS_EIO          0x00000001  // I/O error on this mapping
#define AS_ENOSPC       0x00000002  // No space on device
#define AS_MM_FAULTED   0x00000004  // MM fault occurred  
#define AS_UNMAPPED     0x00000008  // Address space is unmapped

// Page cache hash table size (must be power of 2)
#define PAGE_CACHE_HASH_SIZE    1024
#define PAGE_CACHE_HASH_MASK    (PAGE_CACHE_HASH_SIZE - 1)

// Read-ahead constants
#define DEFAULT_READAHEAD_SIZE  32      // Default read-ahead window (pages)
#define MAX_READAHEAD_SIZE      256     // Maximum read-ahead window
#define MIN_READAHEAD_SIZE      4       // Minimum read-ahead window

// =============================================================================
// GLOBAL PAGE CACHE STATE
// =============================================================================

// Page cache hash table for fast lookups
static struct list_head page_cache_hash[PAGE_CACHE_HASH_SIZE];
static spinlock_t page_cache_hash_locks[PAGE_CACHE_HASH_SIZE];

// Global page cache statistics (struct page_cache_stats defined in mm.h)
static struct page_cache_stats page_cache_stats = {0};

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

static unsigned long page_cache_hash_fn(struct address_space *mapping, pgoff_t offset);
static page_t *__page_cache_lookup(struct address_space *mapping, pgoff_t offset);
static int __page_cache_insert(struct address_space *mapping, page_t *page);
static void __page_cache_remove(page_t *page);
static int page_cache_readahead(struct address_space *mapping, pgoff_t offset, 
                               unsigned long nr_pages);

// =============================================================================
// ADDRESS SPACE OPERATIONS
// =============================================================================

/**
 * address_space_init - Initialize an address space
 * @mapping: Address space to initialize
 * @ops: Address space operations
 * @private_data: Private data for the address space
 */
int address_space_init(struct address_space *mapping,
                      const struct address_space_operations *ops,
                      void *private_data)
{
    if (!mapping) {
        return -1;
    }
    
    // Initialize address space
    INIT_LIST_HEAD(&mapping->clean_pages);
    INIT_LIST_HEAD(&mapping->dirty_pages);
    INIT_LIST_HEAD(&mapping->locked_pages);
    
    atomic_set(&mapping->nr_pages, 0);
    mapping->flags = 0;
    mapping->ops = ops;
    mapping->private_data = private_data;
    
    spin_lock_init(&mapping->lock);
    init_rwsem(&mapping->rwsem);
    
    // Initialize read-ahead state
    mapping->readahead.size = DEFAULT_READAHEAD_SIZE;
    mapping->readahead.start = 0;
    mapping->readahead.ahead_start = 0;
    mapping->readahead.ahead_size = 0;
    
    return 0;
}

/**
 * address_space_destroy - Clean up an address space
 * @mapping: Address space to destroy
 */
void address_space_destroy(struct address_space *mapping)
{
    struct list_head *pos, *tmp;
    page_t *page;
    
    if (!mapping) {
        return;
    }
    
    down_write(&mapping->rwsem);
    
    // Remove all clean pages
    list_for_each_safe(pos, tmp, &mapping->clean_pages) {
        page = list_entry(pos, page_t, lru);
        __page_cache_remove(page);
        free_page(page);
    }
    
    // Remove all dirty pages (write them out first)
    list_for_each_safe(pos, tmp, &mapping->dirty_pages) {
        page = list_entry(pos, page_t, lru);
        // TODO: Write out dirty pages before removal
        __page_cache_remove(page);
        free_page(page);
    }
    
    // Remove all locked pages
    list_for_each_safe(pos, tmp, &mapping->locked_pages) {
        page = list_entry(pos, page_t, lru);
        __page_cache_remove(page);
        free_page(page);
    }
    
    up_write(&mapping->rwsem);
}

// =============================================================================
// PAGE CACHE HASH TABLE MANAGEMENT
// =============================================================================

/**
 * page_cache_hash_fn - Hash function for page cache
 * @mapping: Address space
 * @offset: Page offset
 */
static unsigned long page_cache_hash_fn(struct address_space *mapping, pgoff_t offset)
{
    unsigned long hash;
    
    // Simple hash combining mapping address and offset
    hash = (unsigned long)mapping ^ offset;
    hash ^= hash >> 16;
    hash ^= hash >> 8;
    
    return hash & PAGE_CACHE_HASH_MASK;
}

/**
 * page_cache_init - Initialize the global page cache
 */
int page_cache_init(void)
{
    int i;
    
    // Initialize hash table
    for (i = 0; i < PAGE_CACHE_HASH_SIZE; i++) {
        INIT_LIST_HEAD(&page_cache_hash[i]);
        spin_lock_init(&page_cache_hash_locks[i]);
    }
    
    // Initialize statistics
    memset(&page_cache_stats, 0, sizeof(page_cache_stats));
    
    return 0;
}

// =============================================================================
// PAGE CACHE LOOKUP AND INSERTION
// =============================================================================

/**
 * page_cache_lookup - Look up a page in the cache
 * @mapping: Address space
 * @offset: Page offset
 *
 * Returns the page if found, NULL otherwise.
 */
page_t *page_cache_lookup(struct address_space *mapping, pgoff_t offset)
{
    page_t *page;
    
    if (!mapping) {
        page_cache_stats.cache_misses++;
        return NULL;
    }
    
    down_read(&mapping->rwsem);
    page = __page_cache_lookup(mapping, offset);
    
    if (page) {
        // Mark page as referenced
        page->flags |= PG_REFERENCED;
        page_cache_stats.cache_hits++;
        
        // Increment reference count
        atomic_inc(&page->refcount);
    } else {
        page_cache_stats.cache_misses++;
    }
    
    up_read(&mapping->rwsem);
    return page;
}

/**
 * __page_cache_lookup - Internal lookup function (no locking)
 * @mapping: Address space
 * @offset: Page offset
 */
static page_t *__page_cache_lookup(struct address_space *mapping, pgoff_t offset)
{
    unsigned long hash;
    struct list_head *head, *pos;
    page_t *page;
    
    hash = page_cache_hash_fn(mapping, offset);
    head = &page_cache_hash[hash];
    
    spin_lock(&page_cache_hash_locks[hash]);
    
    list_for_each(pos, head) {
        page = list_entry(pos, page_t, lru);
        
        if (page->mapping == mapping && page->index == offset) {
            spin_unlock(&page_cache_hash_locks[hash]);
            return page;
        }
    }
    
    spin_unlock(&page_cache_hash_locks[hash]);
    return NULL;
}

/**
 * page_cache_insert - Insert a page into the cache
 * @mapping: Address space
 * @page: Page to insert
 * @offset: Page offset
 */
int page_cache_insert(struct address_space *mapping, page_t *page, pgoff_t offset)
{
    int result;
    
    if (!mapping || !page) {
        return -1;
    }
    
    // Set up page metadata
    page->mapping = mapping;
    page->index = offset;
    page->flags |= PG_UPTODATE;
    
    down_write(&mapping->rwsem);
    
    // Check if page already exists
    if (__page_cache_lookup(mapping, offset)) {
        up_write(&mapping->rwsem);
        return -1; // Page already in cache
    }
    
    result = __page_cache_insert(mapping, page);
    if (result == 0) {
        atomic_inc(&mapping->nr_pages);
        page_cache_stats.total_pages++;
        
        // Add to appropriate list
        if (page->flags & PG_DIRTY) {
            list_add_tail(&page->lru, &mapping->dirty_pages);
            page_cache_stats.dirty_pages++;
        } else {
            list_add_tail(&page->lru, &mapping->clean_pages);
            page_cache_stats.clean_pages++;
        }
    }
    
    up_write(&mapping->rwsem);
    return result;
}

/**
 * __page_cache_insert - Internal insertion function (no locking)
 * @mapping: Address space
 * @page: Page to insert
 */
static int __page_cache_insert(struct address_space *mapping, page_t *page)
{
    unsigned long hash;
    struct list_head *head;
    
    hash = page_cache_hash_fn(mapping, page->index);
    head = &page_cache_hash[hash];
    
    spin_lock(&page_cache_hash_locks[hash]);
    list_add(&page->lru, head);
    spin_unlock(&page_cache_hash_locks[hash]);
    
    return 0;
}

// =============================================================================
// PAGE CACHE REMOVAL
// =============================================================================

/**
 * page_cache_remove - Remove a page from the cache
 * @page: Page to remove
 */
void page_cache_remove(page_t *page)
{
    struct address_space *mapping;
    
    if (!page || !page->mapping) {
        return;
    }
    
    mapping = page->mapping;
    
    down_write(&mapping->rwsem);
    __page_cache_remove(page);
    
    atomic_dec(&mapping->nr_pages);
    page_cache_stats.total_pages--;
    
    if (page->flags & PG_DIRTY) {
        page_cache_stats.dirty_pages--;
    } else {
        page_cache_stats.clean_pages--;
    }
    
    up_write(&mapping->rwsem);
}

/**
 * __page_cache_remove - Internal removal function (no locking)
 * @page: Page to remove
 */
static void __page_cache_remove(page_t *page)
{
    unsigned long hash;
    
    // Remove from hash table
    hash = page_cache_hash_fn(page->mapping, page->index);
    spin_lock(&page_cache_hash_locks[hash]);
    list_del(&page->lru);
    spin_unlock(&page_cache_hash_locks[hash]);
    
    // Clear page metadata
    page->mapping = NULL;
    page->index = 0;
}

// =============================================================================
// READ-AHEAD IMPLEMENTATION
// =============================================================================

/**
 * page_cache_readahead - Perform read-ahead
 * @mapping: Address space
 * @offset: Starting offset for read-ahead
 * @nr_pages: Number of pages to read ahead
 */
static int page_cache_readahead(struct address_space *mapping, pgoff_t offset, 
                               unsigned long nr_pages)
{
    page_t *page;
    unsigned long i;
    int result = 0;
    
    if (!mapping || !mapping->ops || !mapping->ops->readpage) {
        return -1;
    }
    
    // Limit read-ahead size
    if (nr_pages > MAX_READAHEAD_SIZE) {
        nr_pages = MAX_READAHEAD_SIZE;
    }
    
    for (i = 0; i < nr_pages; i++) {
        pgoff_t page_offset = offset + i;
        
        // Skip if page already in cache
        if (__page_cache_lookup(mapping, page_offset)) {
            continue;
        }
        
        // Allocate page for read-ahead
        page = alloc_page(GFP_KERNEL);
        if (!page) {
            page_cache_stats.allocation_failures++;
            break;
        }
        
        // Insert into cache
        if (page_cache_insert(mapping, page, page_offset) != 0) {
            free_page(page);
            continue;
        }
        
        // Initiate async read
        if (mapping->ops->readpage(page) != 0) {
            page_cache_remove(page);
            free_page(page);
            continue;
        }
        
        page_cache_stats.readahead_pages++;
        result++;
    }
    
    // Update read-ahead window
    mapping->readahead.start = offset;
    mapping->readahead.size = nr_pages;
    
    return result;
}

/**
 * page_cache_sync_readahead - Synchronous read-ahead
 * @mapping: Address space
 * @ra: Read-ahead state
 * @offset: Page offset being accessed
 */
void page_cache_sync_readahead(struct address_space *mapping,
                              struct file_ra_state *ra, pgoff_t offset)
{
    unsigned long readahead_size;
    
    if (!mapping || !ra) {
        return;
    }
    
    // Calculate read-ahead size based on access pattern
    readahead_size = ra->size;
    
    // Sequential access - increase read-ahead
    if (offset == ra->start + ra->size) {
        readahead_size *= 2;
        if (readahead_size > MAX_READAHEAD_SIZE) {
            readahead_size = MAX_READAHEAD_SIZE;
        }
    }
    
    // Random access - reduce read-ahead
    if (offset < ra->start || offset > ra->start + ra->size * 2) {
        readahead_size = MIN_READAHEAD_SIZE;
    }
    
    // Perform read-ahead
    page_cache_readahead(mapping, offset + 1, readahead_size);
    
    // Update read-ahead state
    ra->start = offset;
    ra->size = readahead_size;
}

// =============================================================================
// FILE MAPPING SUPPORT
// =============================================================================

/**
 * page_cache_read - Read a page from cache or storage
 * @mapping: Address space
 * @offset: Page offset
 * @gfp_mask: GFP flags for allocation
 */
page_t *page_cache_read(struct address_space *mapping, pgoff_t offset, gfp_t gfp_mask)
{
    page_t *page;
    
    // Try cache lookup first
    page = page_cache_lookup(mapping, offset);
    if (page) {
        return page;
    }
    
    // Page not in cache - allocate new page
    page = alloc_page(gfp_mask);
    if (!page) {
        page_cache_stats.allocation_failures++;
        return NULL;
    }
    
    // Insert into cache
    if (page_cache_insert(mapping, page, offset) != 0) {
        free_page(page);
        return NULL;
    }
    
    // Read data from storage
    if (mapping->ops && mapping->ops->readpage) {
        if (mapping->ops->readpage(page) != 0) {
            page_cache_remove(page);
            free_page(page);
            return NULL;
        }
    } else {
        // No read operation - zero-fill the page
        void *kaddr = page_address(page);
        if (kaddr) {
            memset(kaddr, 0, PAGE_SIZE);
        }
        page->flags |= PG_UPTODATE;
    }
    
    // Trigger read-ahead
    page_cache_sync_readahead(mapping, &mapping->readahead, offset);
    
    return page;
}

/**
 * page_cache_write - Mark a page as dirty for write-out
 * @page: Page to mark dirty
 */
int page_cache_write(page_t *page)
{
    struct address_space *mapping;
    
    if (!page || !page->mapping) {
        return -1;
    }
    
    mapping = page->mapping;
    
    down_write(&mapping->rwsem);
    
    // Move from clean to dirty list if necessary
    if (!(page->flags & PG_DIRTY)) {
        list_del(&page->lru);
        list_add_tail(&page->lru, &mapping->dirty_pages);
        
        page->flags |= PG_DIRTY;
        page_cache_stats.clean_pages--;
        page_cache_stats.dirty_pages++;
    }
    
    up_write(&mapping->rwsem);
    return 0;
}

// =============================================================================
// STATISTICS AND DEBUGGING
// =============================================================================

/**
 * page_cache_get_stats - Get page cache statistics
 */
struct page_cache_stats *page_cache_get_stats(void)
{
    return &page_cache_stats;
}

/**
 * page_cache_print_stats - Print page cache statistics  
 */
void page_cache_print_stats(void)
{
    // TODO: Implement page cache statistics printing
    // This would be useful for performance monitoring
}