#include "include/mm.h"
#include "include/list.h"
#include "include/atomic_mm.h"
#include "include/memory.h"
#include "include/string.h"
#include "include/screen.h"
#include <stdarg.h>

// =============================================================================
// SLAB ALLOCATOR IMPLEMENTATION
// =============================================================================
// Linux-style SLAB allocator for efficient kernel object caching
// Provides object recycling, per-CPU caches, and debugging features
// =============================================================================

#define SLAB_MAGIC      0x53414C42  // "SLAB"
#define CACHE_NAMELEN   32

// Maximum number of cached objects per CPU
#define SLAB_LIMIT      16

// Slab descriptor
typedef struct slab {
    struct list_head list;         // List linkage
    unsigned long colouroff;       // Colour offset
    void *s_mem;                  // Start of objects
    unsigned int inuse;           // Number of active objects
    unsigned int free;            // Next free object
    unsigned short nodeid;        // Node ID
} slab_t;

// Per-CPU cache
struct kmem_cache_cpu {
    void **freelist;              // Array of free objects
    unsigned int avail;           // Available objects
    unsigned int limit;           // Maximum objects
    unsigned int touched;         // Recently used flag
    spinlock_t lock;              // Cache lock
};

// Cache node (simplified - single node)
struct kmem_cache_node {
    struct list_head slabs_partial; // Partially filled slabs
    struct list_head slabs_full;    // Full slabs
    struct list_head slabs_free;    // Empty slabs
    unsigned long free_objects;     // Total free objects
    unsigned int free_limit;        // Free slab limit
    spinlock_t list_lock;          // Node lock
};

// Global cache list
static LIST_HEAD(cache_list);
static spinlock_t cache_list_lock = SPIN_LOCK_UNLOCKED;

// Standard cache sizes
static size_t cache_sizes[] = {
    32, 64, 96, 128, 192, 256, 512, 1024, 2048, 4096, 8192, 0
};

// Standard caches
static kmem_cache_t *size_caches[sizeof(cache_sizes) / sizeof(size_t)];

// Cache for cache descriptors
static kmem_cache_t *cache_cache;

// SLAB allocator state
static struct {
    bool initialized;
    unsigned long total_caches;
    unsigned long total_slabs;
    unsigned long active_objects;
} slab_state = { .initialized = false };

// =============================================================================
// INTERNAL HELPER FUNCTIONS
// =============================================================================

// Calculate colour offset for cache alignment
static unsigned int cache_estimate(unsigned long gfporder, size_t size, 
                                  size_t align, int flags)
{
    unsigned long bytes = PAGE_SIZE << gfporder;
    unsigned long mgmt_size = sizeof(slab_t);
    
    // Account for slab management overhead
    if (!(flags & SLAB_POISON)) {
        mgmt_size = 0;  // Slab descriptor stored off-slab
    }
    
    unsigned long objects = (bytes - mgmt_size) / size;
    
    // Ensure alignment
    if (align) {
        objects = (bytes - mgmt_size) / (size + align - 1);
    }
    
    return objects;
}

// Calculate object offset in slab
static inline void *index_to_obj(kmem_cache_t *cache, slab_t *slab, unsigned int idx)
{
    return slab->s_mem + (cache->size * idx);
}

// Calculate object index from pointer
static inline unsigned int obj_to_index(kmem_cache_t *cache, slab_t *slab, void *obj)
{
    return ((char *)obj - (char *)slab->s_mem) / cache->size;
}

// Get free object from slab
static void *slab_get_obj(kmem_cache_t *cache, slab_t *slab)
{
    if (slab->inuse >= cache->objects_per_slab) {
        return NULL;
    }
    
    void *objp = index_to_obj(cache, slab, slab->free);
    
    // Update free list (simple array-based for now)
    if (slab->free + 1 < cache->objects_per_slab) {
        slab->free++;
    } else {
        slab->free = cache->objects_per_slab; // Mark as full
    }
    
    slab->inuse++;
    return objp;
}

// Return object to slab
static void slab_put_obj(kmem_cache_t *cache, slab_t *slab, void *objp)
{
    unsigned int objnr = obj_to_index(cache, slab, objp);
    
    if (objnr >= cache->objects_per_slab) {
        return; // Invalid object
    }
    
    // Simple free tracking - just decrement inuse
    if (slab->inuse > 0) {
        slab->inuse--;
        if (slab->inuse == 0) {
            slab->free = 0; // Reset free pointer
        }
    }
}

// Allocate a new slab
static slab_t *cache_grow(kmem_cache_t *cache, gfp_t flags)
{
    page_t *page;
    slab_t *slab;
    void *objp;
    
    // Allocate pages for the slab
    unsigned int order = 0; // Single page for simplicity
    page = alloc_pages(flags, order);
    if (!page) {
        return NULL;
    }
    
    // Get memory from page
    objp = page_address(page);
    if (!objp) {
        __free_pages(page, order);
        return NULL;
    }
    
    // Allocate slab descriptor
    slab = kmem_cache_alloc(cache_cache, flags);
    if (!slab) {
        __free_pages(page, order);
        return NULL;
    }
    
    // Initialize slab
    slab->colouroff = 0; // No coloring for simplicity
    slab->s_mem = objp;
    slab->inuse = 0;
    slab->free = 0;
    slab->nodeid = 0;
    
    // Set page properties
    page->slab_cache = cache;
    page->flags |= PG_SLAB;
    
    // Initialize object tracking
    if (cache->ctor) {
        for (unsigned int i = 0; i < cache->objects_per_slab; i++) {
            cache->ctor(index_to_obj(cache, slab, i));
        }
    }
    
    cache->num_slabs++;
    slab_state.total_slabs++;
    
    return slab;
}

// Destroy a slab
static void slab_destroy(kmem_cache_t *cache, slab_t *slab)
{
    if (!cache || !slab) {
        return;
    }
    
    // Get page from slab memory
    unsigned long pfn = ((unsigned long)slab->s_mem) >> PAGE_SHIFT;
    page_t *page = pfn_to_page(pfn);
    
    if (page) {
        // Clear page properties
        page->flags &= ~PG_SLAB;
        page->slab_cache = NULL;
        
        // Free page
        __free_pages(page, 0);
    }
    
    // Free slab descriptor
    kmem_cache_free(cache_cache, slab);
    
    cache->num_slabs--;
    slab_state.total_slabs--;
}

// =============================================================================
// PER-CPU CACHE MANAGEMENT
// =============================================================================

// Initialize per-CPU cache
static int cache_alloc_cpucache(kmem_cache_t *cache)
{
    cache->cpu_cache = kzalloc(sizeof(struct kmem_cache_cpu));
    if (!cache->cpu_cache) {
        return -1;
    }
    
    cache->cpu_cache->avail = 0;
    cache->cpu_cache->limit = SLAB_LIMIT;
    cache->cpu_cache->touched = 0;
    spin_lock_init(&cache->cpu_cache->lock);
    
    // Allocate freelist
    cache->cpu_cache->freelist = kzalloc(SLAB_LIMIT * sizeof(void *));
    if (!cache->cpu_cache->freelist) {
        kfree(cache->cpu_cache);
        cache->cpu_cache = NULL;
        return -1;
    }
    
    return 0;
}

// Free per-CPU cache
static void cache_free_cpucache(kmem_cache_t *cache)
{
    if (cache->cpu_cache) {
        if (cache->cpu_cache->freelist) {
            kfree(cache->cpu_cache->freelist);
        }
        kfree(cache->cpu_cache);
        cache->cpu_cache = NULL;
    }
}

// =============================================================================
// CACHE MANAGEMENT
// =============================================================================

// Create a new cache
kmem_cache_t *kmem_cache_create(const char *name, size_t size, size_t align,
                               unsigned long flags, void (*ctor)(void *))
{
    kmem_cache_t *cache;
    
    if (!slab_state.initialized || !name || size == 0) {
        return NULL;
    }
    
    if (size > SLAB_MAX_SIZE) {
        return NULL; // Too large for SLAB
    }
    
    // Allocate cache descriptor
    cache = kzalloc(sizeof(kmem_cache_t));
    if (!cache) {
        return NULL;
    }
    
    // Initialize cache properties
    strncpy((char *)cache->name, name, CACHE_NAMELEN - 1);
    cache->size = size;
    cache->align = align ? align : SLAB_ALIGN_SIZE;
    cache->flags = flags;
    cache->ctor = ctor;
    
    // Calculate object layout
    cache->object_size = size;
    cache->slab_size = PAGE_SIZE; // Single page slabs
    cache->objects_per_slab = cache_estimate(0, size, align, flags);
    
    if (cache->objects_per_slab == 0) {
        kfree(cache);
        return NULL;
    }
    
    // Initialize node
    cache->node = kzalloc(sizeof(struct kmem_cache_node));
    if (!cache->node) {
        kfree(cache);
        return NULL;
    }
    
    INIT_LIST_HEAD(&cache->node->slabs_partial);
    INIT_LIST_HEAD(&cache->node->slabs_full);
    INIT_LIST_HEAD(&cache->node->slabs_free);
    spin_lock_init(&cache->node->list_lock);
    cache->node->free_objects = 0;
    cache->node->free_limit = 2; // Keep max 2 free slabs
    
    // Setup per-CPU cache
    if (cache_alloc_cpucache(cache) < 0) {
        kfree(cache->node);
        kfree(cache);
        return NULL;
    }
    
    // Initialize statistics
    cache->num_allocations = 0;
    cache->num_frees = 0;
    cache->active_objs = 0;
    cache->num_slabs = 0;
    
    // Add to global cache list
    spin_lock(&cache_list_lock);
    list_add(&cache->list, &cache_list);
    slab_state.total_caches++;
    spin_unlock(&cache_list_lock);
    
    print("[SLAB] Created cache '"); print(name); 
    print("' size="); print_dec(size);
    print(" objects_per_slab="); print_dec(cache->objects_per_slab); print("\n");
    
    return cache;
}

// Destroy a cache
void kmem_cache_destroy(kmem_cache_t *cache)
{
    if (!cache) {
        return;
    }
    
    // Remove from global list
    spin_lock(&cache_list_lock);
    list_del(&cache->list);
    slab_state.total_caches--;
    spin_unlock(&cache_list_lock);
    
    // Destroy all slabs
    struct list_head *pos, *n;
    
    list_for_each_safe(pos, n, &cache->node->slabs_full) {
        slab_t *slab = list_entry(pos, slab_t, list);
        list_del(&slab->list);
        slab_destroy(cache, slab);
    }
    
    list_for_each_safe(pos, n, &cache->node->slabs_partial) {
        slab_t *slab = list_entry(pos, slab_t, list);
        list_del(&slab->list);
        slab_destroy(cache, slab);
    }
    
    list_for_each_safe(pos, n, &cache->node->slabs_free) {
        slab_t *slab = list_entry(pos, slab_t, list);
        list_del(&slab->list);
        slab_destroy(cache, slab);
    }
    
    // Free per-CPU cache
    cache_free_cpucache(cache);
    
    // Free node and cache
    kfree(cache->node);
    kfree(cache);
}

// =============================================================================
// OBJECT ALLOCATION AND DEALLOCATION
// =============================================================================

// Allocate object from cache
void *kmem_cache_alloc(kmem_cache_t *cache, gfp_t flags)
{
    if (!cache || !slab_state.initialized) {
        return NULL;
    }
    
    // Try per-CPU cache first
    if (cache->cpu_cache && cache->cpu_cache->avail > 0) {
        spin_lock(&cache->cpu_cache->lock);
        if (cache->cpu_cache->avail > 0) {
            void *obj = cache->cpu_cache->freelist[--cache->cpu_cache->avail];
            cache->cpu_cache->touched = 1;
            spin_unlock(&cache->cpu_cache->lock);
            
            cache->num_allocations++;
            cache->active_objs++;
            slab_state.active_objects++;
            return obj;
        }
        spin_unlock(&cache->cpu_cache->lock);
    }
    
    // Need to get object from slab
    spin_lock(&cache->node->list_lock);
    
    // Try partial slabs first
    if (!list_empty(&cache->node->slabs_partial)) {
        slab_t *slab = list_first_entry(&cache->node->slabs_partial, slab_t, list);
        void *obj = slab_get_obj(cache, slab);
        
        if (obj) {
            // Check if slab is now full
            if (slab->inuse >= cache->objects_per_slab) {
                list_move(&slab->list, &cache->node->slabs_full);
            }
            
            spin_unlock(&cache->node->list_lock);
            cache->num_allocations++;
            cache->active_objs++;
            slab_state.active_objects++;
            return obj;
        }
    }
    
    // Try free slabs
    if (!list_empty(&cache->node->slabs_free)) {
        slab_t *slab = list_first_entry(&cache->node->slabs_free, slab_t, list);
        void *obj = slab_get_obj(cache, slab);
        
        if (obj) {
            list_move(&slab->list, &cache->node->slabs_partial);
            spin_unlock(&cache->node->list_lock);
            cache->num_allocations++;
            cache->active_objs++;
            slab_state.active_objects++;
            return obj;
        }
    }
    
    spin_unlock(&cache->node->list_lock);
    
    // Need to grow cache
    slab_t *new_slab = cache_grow(cache, flags);
    if (!new_slab) {
        return NULL; // Out of memory
    }
    
    // Add new slab to partial list and allocate from it
    spin_lock(&cache->node->list_lock);
    list_add(&new_slab->list, &cache->node->slabs_partial);
    void *obj = slab_get_obj(cache, new_slab);
    spin_unlock(&cache->node->list_lock);
    
    if (obj) {
        cache->num_allocations++;
        cache->active_objs++;
        slab_state.active_objects++;
    }
    
    return obj;
}

// Free object to cache
void kmem_cache_free(kmem_cache_t *cache, void *obj)
{
    if (!cache || !obj || !slab_state.initialized) {
        return;
    }
    
    // Try to add to per-CPU cache
    if (cache->cpu_cache && cache->cpu_cache->avail < cache->cpu_cache->limit) {
        spin_lock(&cache->cpu_cache->lock);
        if (cache->cpu_cache->avail < cache->cpu_cache->limit) {
            cache->cpu_cache->freelist[cache->cpu_cache->avail++] = obj;
            spin_unlock(&cache->cpu_cache->lock);
            
            cache->num_frees++;
            cache->active_objs--;
            slab_state.active_objects--;
            return;
        }
        spin_unlock(&cache->cpu_cache->lock);
    }
    
    // Find slab containing this object
    unsigned long pfn = ((unsigned long)obj) >> PAGE_SHIFT;
    page_t *page = pfn_to_page(pfn);
    
    if (!page || !(page->flags & PG_SLAB) || page->slab_cache != cache) {
        print("[SLAB] Error: Invalid object 0x"); print_hex((uint32)obj); print("\n");
        return;
    }
    
    // Find slab descriptor (simple linear search for now)
    slab_t *slab = NULL;
    
    spin_lock(&cache->node->list_lock);
    
    // Check all slab lists
    struct list_head *lists[] = {
        &cache->node->slabs_full,
        &cache->node->slabs_partial,
        &cache->node->slabs_free
    };
    
    for (int i = 0; i < 3 && !slab; i++) {
        struct list_head *pos;
        list_for_each(pos, lists[i]) {
            slab_t *s = list_entry(pos, slab_t, list);
            if (obj >= s->s_mem && 
                obj < (char *)s->s_mem + cache->slab_size) {
                slab = s;
                break;
            }
        }
    }
    
    if (!slab) {
        spin_unlock(&cache->node->list_lock);
        print("[SLAB] Error: Could not find slab for object\n");
        return;
    }
    
    // Return object to slab
    unsigned int old_inuse = slab->inuse;
    slab_put_obj(cache, slab, obj);
    
    // Move slab between lists if needed
    if (old_inuse == cache->objects_per_slab && slab->inuse < cache->objects_per_slab) {
        // Was full, now partial
        list_move(&slab->list, &cache->node->slabs_partial);
    } else if (slab->inuse == 0) {
        // Now empty - move to free list
        list_move(&slab->list, &cache->node->slabs_free);
        
        // Consider destroying excess free slabs
        if (cache->node->free_objects > cache->node->free_limit) {
            list_del(&slab->list);
            spin_unlock(&cache->node->list_lock);
            slab_destroy(cache, slab);
            cache->num_frees++;
            cache->active_objs--;
            slab_state.active_objects--;
            return;
        }
    }
    
    spin_unlock(&cache->node->list_lock);
    
    cache->num_frees++;
    cache->active_objs--;
    slab_state.active_objects--;
}

// =============================================================================
// GENERAL PURPOSE ALLOCATORS
// =============================================================================

// Slab-based kmalloc implementation (Linux-compatible API with flags)
// Note: For simple single-argument kmalloc(), see kheap.c
void *slab_kmalloc(size_t size, gfp_t flags)
{
    if (!slab_state.initialized || size == 0) {
        return NULL;
    }
    
    // Find appropriate size cache
    for (int i = 0; cache_sizes[i] != 0; i++) {
        if (size <= cache_sizes[i]) {
            return kmem_cache_alloc(size_caches[i], flags);
        }
    }
    
    // Size too large for standard caches - use buddy allocator
    unsigned int order = 0;
    unsigned long pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
    
    while ((1UL << order) < pages) {
        order++;
    }
    
    page_t *page = alloc_pages(flags, order);
    if (!page) {
        return NULL;
    }
    
    return page_address(page);
}

// Slab-based zero-filled allocation
void *slab_kzalloc(size_t size, gfp_t flags)
{
    void *ptr = slab_kmalloc(size, flags);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

// Free general allocation
void kfree(void *ptr)
{
    if (!ptr || !slab_state.initialized) {
        return;
    }
    
    // Check if this is from a slab
    unsigned long pfn = ((unsigned long)ptr) >> PAGE_SHIFT;
    page_t *page = pfn_to_page(pfn);
    
    if (page && (page->flags & PG_SLAB) && page->slab_cache) {
        kmem_cache_free((kmem_cache_t *)page->slab_cache, ptr);
        return;
    }
    
    // Must be from buddy allocator
    if (page) {
        __free_pages(page, 0); // Assume single page for now
    }
}

// =============================================================================
// INITIALIZATION
// =============================================================================

// Forward declaration for helper function
static int slab_format_cache_name(char *str, size_t size, unsigned long val);

// Initialize standard size caches
static int init_size_caches(void)
{
    int i;
    for (i = 0; cache_sizes[i] != 0; i++) {
        char name[CACHE_NAMELEN];
        slab_format_cache_name(name, sizeof(name), cache_sizes[i]);

        size_caches[i] = kmem_cache_create(name, cache_sizes[i],
                                          SLAB_ALIGN_SIZE, 0, NULL);
        if (!size_caches[i]) {
            print("[SLAB] Failed to create size cache for ");
            print_dec(cache_sizes[i]); print("\n");
            return -1;
        }
    }

    print("[SLAB] Created "); print_dec(i); print(" standard size caches\n");
    return 0;
}

// Initialize SLAB allocator
int slab_init(void)
{
    print("[SLAB] Initializing SLAB allocator...\n");
    
    if (slab_state.initialized) {
        return 0;
    }
    
    // Initialize state
    slab_state.total_caches = 0;
    slab_state.total_slabs = 0;
    slab_state.active_objects = 0;
    
    // Create cache for cache descriptors (bootstrap)
    cache_cache = kzalloc(sizeof(kmem_cache_t));
    if (!cache_cache) {
        print("[SLAB] Failed to allocate cache_cache\n");
        return -1;
    }
    
    // Bootstrap cache_cache initialization
    strcpy((char *)cache_cache->name, "kmem_cache");
    cache_cache->size = sizeof(kmem_cache_t);
    cache_cache->align = SLAB_ALIGN_SIZE;
    cache_cache->flags = 0;
    cache_cache->object_size = sizeof(kmem_cache_t);
    cache_cache->slab_size = PAGE_SIZE;
    cache_cache->objects_per_slab = PAGE_SIZE / sizeof(kmem_cache_t);
    
    // Initialize node
    cache_cache->node = kzalloc(sizeof(struct kmem_cache_node));
    if (!cache_cache->node) {
        kfree(cache_cache);
        return -1;
    }
    
    INIT_LIST_HEAD(&cache_cache->node->slabs_partial);
    INIT_LIST_HEAD(&cache_cache->node->slabs_full);
    INIT_LIST_HEAD(&cache_cache->node->slabs_free);
    spin_lock_init(&cache_cache->node->list_lock);
    
    // Add to cache list
    list_add(&cache_cache->list, &cache_list);
    
    // Initialize standard size caches
    if (init_size_caches() < 0) {
        return -1;
    }
    
    slab_state.initialized = true;
    
    print("[SLAB] Initialization complete. "); 
    print_dec(slab_state.total_caches); print(" caches created\n");
    
    return 0;
}

// Helper function specifically for slab cache names (does not conflict with libc snprintf)
static int slab_format_cache_name(char *str, size_t size, unsigned long val)
{
    (void)size; // TODO: respect size limit
    strcpy(str, "kmalloc-");
    char num[16];
    int len = 0;
    if (val == 0) {
        num[len++] = '0';
    } else {
        while (val > 0) {
            num[len++] = '0' + (val % 10);
            val /= 10;
        }
    }

    // Reverse the number
    for (int i = 0; i < len; i++) {
        str[8 + i] = num[len - 1 - i];
    }
    str[8 + len] = '\0';

    return 8 + len;
}