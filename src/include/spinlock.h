#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "types.h" // For uint32, bool

// Simple spinlock structure
typedef struct {
    volatile uint32 lock;
    uint32 saved_flags; // Preserve interrupt state for the owner
} spinlock_t;

// Initialize a spinlock
static inline void spinlock_init(spinlock_t* lock) {
    lock->lock = 0;
    lock->saved_flags = 0;
}

// Acquire a spinlock: save current IF state and disable interrupts to avoid deadlocks
static inline void spinlock_acquire(spinlock_t* lock) {
    uint32 flags;
    __asm__ __volatile__("pushf\n\tpop %0\n\tcli" : "=r"(flags) :: "memory");

    while (__sync_lock_test_and_set(&lock->lock, 1)) {
        __asm__ __volatile__("pause");
    }

    lock->saved_flags = flags;
}

// Release a spinlock and restore interrupt state
static inline void spinlock_release(spinlock_t* lock) {
    __sync_lock_release(&lock->lock);
    __asm__ __volatile__("push %0\n\tpopf" :: "r"(lock->saved_flags) : "memory", "cc");
}

#endif // SPINLOCK_H
