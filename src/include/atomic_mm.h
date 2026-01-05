#ifndef ATOMIC_MM_H
#define ATOMIC_MM_H

/*
 * atomic_mm.h - Memory Management Atomic Operations for Forest OS
 *
 * This header provides memory management specific atomic operations
 * and builds upon the core atomic.h and spinlock.h headers.
 */

#include <stdint.h>
#include "atomic.h"
#include "spinlock.h"
#include "list.h"

// =============================================================================
// READ-WRITE SEMAPHORES (simplified)
// =============================================================================

#ifndef RW_SEMAPHORE_DEFINED
#define RW_SEMAPHORE_DEFINED

struct rw_semaphore {
    long count;
    spinlock_t wait_lock;
    struct list_head wait_list;
};

#define SPIN_LOCK_UNLOCKED_SIMPLE { .locked = ATOMIC8_INIT(0), .owner_cpu = 0, .name = NULL, .acquisition_count = 0, .saved_flags = 0 }

#define __RWSEM_INITIALIZER(name) \
    { 0, SPIN_LOCK_UNLOCKED_SIMPLE, LIST_HEAD_INIT((name).wait_list) }

#define DECLARE_RWSEM(name) \
    struct rw_semaphore name = __RWSEM_INITIALIZER(name)

static inline void init_rwsem(struct rw_semaphore *sem)
{
    sem->count = 0;
    spinlock_init(&sem->wait_lock, "rwsem");
    INIT_LIST_HEAD(&sem->wait_list);
}

// Simplified read/write lock (just uses spinlock for now)
static inline void down_read(struct rw_semaphore *sem)
{
    spinlock_acquire(&sem->wait_lock);
}

static inline void up_read(struct rw_semaphore *sem)
{
    spinlock_release(&sem->wait_lock);
}

static inline void down_write(struct rw_semaphore *sem)
{
    spinlock_acquire(&sem->wait_lock);
}

static inline void up_write(struct rw_semaphore *sem)
{
    spinlock_release(&sem->wait_lock);
}

#endif /* RW_SEMAPHORE_DEFINED */

// =============================================================================
// GFP (Get Free Pages) FLAGS
// =============================================================================

#ifndef GFP_T_DEFINED
#define GFP_T_DEFINED
typedef unsigned int gfp_t;
#endif

// Zone modifiers
#ifndef __GFP_DMA
#define __GFP_DMA        0x01   // Allocate from DMA zone
#define __GFP_NORMAL     0x02   // Allocate from normal zone
#define __GFP_HIGHMEM    0x04   // Allow highmem allocation

// Action modifiers
#define __GFP_WAIT       0x10   // Can wait for memory
#define __GFP_IO         0x20   // Can start I/O
#define __GFP_FS         0x40   // Can call into filesystem
#define __GFP_COLD       0x80   // Want cache-cold pages
#define __GFP_NOWARN     0x100  // Don't warn on failure
#define __GFP_REPEAT     0x200  // Retry the allocation
#define __GFP_NOFAIL     0x400  // Never fail allocation
#define __GFP_NORETRY    0x800  // Don't retry on failure
#define __GFP_ZERO       0x1000 // Zero the allocated memory
#endif

// Common combinations
#ifndef GFP_ATOMIC
#define GFP_ATOMIC      0
#define GFP_KERNEL      (__GFP_WAIT | __GFP_IO | __GFP_FS)
#define GFP_USER        (__GFP_WAIT | __GFP_IO | __GFP_FS | __GFP_HIGHMEM)
#define GFP_DMA         (__GFP_DMA)
#endif

// =============================================================================
// PAGE TABLE PROTECTION
// =============================================================================

#ifndef PGPROT_T_STRUCT_DEFINED
#define PGPROT_T_STRUCT_DEFINED
typedef struct { unsigned long pgprot; } pgprot_t;
#define pgprot_val(x)       ((x).pgprot)
#define __pgprot(x)         ((pgprot_t) { (x) })
#endif

#ifndef PAGE_NONE
#define PAGE_NONE       __pgprot(0x000)
#define PAGE_SHARED     __pgprot(0x003)  // Present + Writable
#define PAGE_COPY       __pgprot(0x001)  // Present only
#define PAGE_READONLY   __pgprot(0x001)  // Present only
#define PAGE_KERNEL     __pgprot(0x003)  // Present + Writable
#endif

// =============================================================================
// PAGE TABLE TYPES
// =============================================================================

#ifndef PTE_T_DEFINED
#define PTE_T_DEFINED
typedef unsigned long pte_t;
#endif

#ifndef PMD_T_DEFINED
#define PMD_T_DEFINED
typedef unsigned long pmd_t;
#endif

#ifndef PUD_T_DEFINED
#define PUD_T_DEFINED
typedef unsigned long pud_t;
#endif

#ifndef PGD_T_DEFINED
#define PGD_T_DEFINED
typedef unsigned long pgd_t;
#endif

#ifndef PGOFF_T_DEFINED
#define PGOFF_T_DEFINED
typedef unsigned long pgoff_t;
#endif

// Additional page flag definitions
#ifndef PG_PRIVATE
#define PG_PRIVATE      8   // Private flag for buddy allocator
#endif

// PTE manipulation
#ifndef pte_none
#define pte_none(pte)       (!(pte))
#define pte_present(pte)    ((pte) & 1)
#define pte_write(pte)      ((pte) & 2)
#define pte_dirty(pte)      ((pte) & 0x40)
#define pte_young(pte)      ((pte) & 0x20)

#define pte_wrprotect(pte)  ((pte) & ~2)
#define pte_mkwrite(pte)    ((pte) | 2)
#define pte_mkdirty(pte)    ((pte) | 0x40)
#define pte_mkclean(pte)    ((pte) & ~0x40)
#define pte_mkyoung(pte)    ((pte) | 0x20)
#define pte_mkold(pte)      ((pte) & ~0x20)

#define pte_page(pte)       pfn_to_page(pte_pfn(pte))
#define pte_pfn(pte)        ((pte) >> PAGE_SHIFT)
#define pfn_pte(pfn, prot)  (((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#endif

#endif // ATOMIC_MM_H