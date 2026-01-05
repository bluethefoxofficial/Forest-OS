#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "types.h"
#include "atomic.h"

typedef struct {
    atomic8_t locked;
    uint32 owner_cpu;
    const char* name;
    uint32 acquisition_count;
    unsigned long saved_flags;
} spinlock_t;

#define SPINLOCK_INIT(lock_name) { \
    .locked = ATOMIC8_INIT(0), \
    .owner_cpu = 0, \
    .name = lock_name, \
    .acquisition_count = 0, \
    .saved_flags = 0 \
}

#define SPINLOCK_UNLOCKED SPINLOCK_INIT(NULL)
#define SPIN_LOCK_UNLOCKED SPINLOCK_UNLOCKED

#define DEFINE_SPINLOCK(name, lock_name) \
    static spinlock_t name = SPINLOCK_INIT(lock_name)

static inline void spinlock_init(spinlock_t* lock, const char* name) {
    atomic_store8(&lock->locked, 0);
    lock->owner_cpu = 0;
    lock->name = name;
    lock->acquisition_count = 0;
    lock->saved_flags = 0;
}

static inline unsigned long spinlock_irq_save(void) {
    unsigned long flags;
#if defined(__x86_64__)
    __asm__ volatile (
        "pushfq\n\t"
        "cli\n\t"
        "popq %0"
        : "=rm" (flags)
        :
        : "memory"
    );
#else
    __asm__ volatile (
        "pushfl\n\t"
        "cli\n\t"
        "popl %0"
        : "=rm" (flags)
        :
        : "memory"
    );
#endif
    return flags;
}

static inline void spinlock_irq_restore(unsigned long flags) {
#if defined(__x86_64__)
    __asm__ volatile (
        "pushq %0\n\t"
        "popfq"
        :
        : "rm" (flags)
        : "memory", "cc"
    );
#else
    __asm__ volatile (
        "pushl %0\n\t"
        "popfl"
        :
        : "rm" (flags)
        : "memory", "cc"
    );
#endif
}

static inline void spinlock_acquire(spinlock_t* lock) {
    unsigned long flags = spinlock_irq_save();
    uint32 spin_count = 0;
    const uint32 SPIN_YIELD_THRESHOLD = 100;
    
    while (atomic_test_and_set8(&lock->locked)) {
        spin_count++;
        
        if (spin_count >= SPIN_YIELD_THRESHOLD) {
            cpu_pause();
            spin_count = 0;
        }
    }
    
    memory_barrier();
    lock->owner_cpu = 0;
    lock->acquisition_count++;
    lock->saved_flags = flags;
}

static inline bool spinlock_try_acquire(spinlock_t* lock) {
    unsigned long flags = spinlock_irq_save();
    
    if (!atomic_test_and_set8(&lock->locked)) {
        memory_barrier();
        lock->owner_cpu = 0;
        lock->acquisition_count++;
        lock->saved_flags = flags;
        return true;
    }
    
    spinlock_irq_restore(flags);
    return false;
}

static inline void spinlock_release(spinlock_t* lock) {
    uint32 flags = lock->saved_flags;
    memory_barrier();
    lock->owner_cpu = 0;
    atomic_clear8(&lock->locked);
    spinlock_irq_restore(flags);
}

static inline bool spinlock_is_locked(const spinlock_t* lock) {
    return atomic_load8(&lock->locked) != 0;
}

/* Legacy compatibility macros */
#define spin_lock_irqsave(lock, flags) \
    do { \
        flags = spinlock_irq_save(); \
        spinlock_acquire(lock); \
    } while(0)

#define spin_unlock_irqrestore(lock, flags) \
    do { \
        spinlock_release(lock); \
        spinlock_irq_restore(flags); \
    } while(0)

/* Linux-style spin lock API compatibility */
#define spin_lock_init(lock) spinlock_init(lock, NULL)
#define spin_lock(lock) spinlock_acquire(lock)
#define spin_unlock(lock) spinlock_release(lock)

#endif // SPINLOCK_H
