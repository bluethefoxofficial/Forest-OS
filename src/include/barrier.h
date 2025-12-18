#ifndef BARRIER_H
#define BARRIER_H

#include "types.h"
#include "atomic.h"
#include "semaphore.h"

typedef struct {
    atomic32_t count;
    atomic32_t generation;
    uint32 total_threads;
    mutex_t mutex;
    semaphore_t barrier_sem;
    const char* name;
} barrier_t;

#define BARRIER_INIT(thread_count, barrier_name) { \
    .count = ATOMIC32_INIT(0), \
    .generation = ATOMIC32_INIT(0), \
    .total_threads = thread_count, \
    .mutex = MUTEX_INIT("barrier_mutex"), \
    .barrier_sem = SEMAPHORE_INIT(0, thread_count, "barrier_sem"), \
    .name = barrier_name \
}

#define DEFINE_BARRIER(name, thread_count, barrier_name) \
    static barrier_t name = BARRIER_INIT(thread_count, barrier_name)

static inline void barrier_init(barrier_t* barrier, uint32 thread_count, const char* name) {
    atomic_store32(&barrier->count, 0);
    atomic_store32(&barrier->generation, 0);
    barrier->total_threads = thread_count;
    mutex_init(&barrier->mutex, "barrier_mutex");
    semaphore_init(&barrier->barrier_sem, 0, thread_count, "barrier_sem");
    barrier->name = name;
}

void barrier_wait(barrier_t* barrier);

typedef struct {
    atomic32_t arrived;
    atomic32_t generation;
    uint32 total_threads;
    spinlock_t lock;
    const char* name;
} spinbarrier_t;

#define SPINBARRIER_INIT(thread_count, barrier_name) { \
    .arrived = ATOMIC32_INIT(0), \
    .generation = ATOMIC32_INIT(0), \
    .total_threads = thread_count, \
    .lock = SPINLOCK_INIT("spinbarrier_lock"), \
    .name = barrier_name \
}

#define DEFINE_SPINBARRIER(name, thread_count, barrier_name) \
    static spinbarrier_t name = SPINBARRIER_INIT(thread_count, barrier_name)

static inline void spinbarrier_init(spinbarrier_t* barrier, uint32 thread_count, const char* name) {
    atomic_store32(&barrier->arrived, 0);
    atomic_store32(&barrier->generation, 0);
    barrier->total_threads = thread_count;
    spinlock_init(&barrier->lock, "spinbarrier_lock");
    barrier->name = name;
}

void spinbarrier_wait(spinbarrier_t* barrier);

typedef struct {
    atomic32_t count;
    mutex_t count_mutex;
    semaphore_t read_sem;
    semaphore_t write_sem;
    const char* name;
} rwlock_t;

#define RWLOCK_INIT(lock_name) { \
    .count = ATOMIC32_INIT(0), \
    .count_mutex = MUTEX_INIT("rwlock_count"), \
    .read_sem = SEMAPHORE_INIT(1, 1, "rwlock_read"), \
    .write_sem = SEMAPHORE_INIT(1, 1, "rwlock_write"), \
    .name = lock_name \
}

#define DEFINE_RWLOCK(name, lock_name) \
    static rwlock_t name = RWLOCK_INIT(lock_name)

static inline void rwlock_init(rwlock_t* lock, const char* name) {
    atomic_store32(&lock->count, 0);
    mutex_init(&lock->count_mutex, "rwlock_count");
    semaphore_init(&lock->read_sem, 1, 1, "rwlock_read");
    semaphore_init(&lock->write_sem, 1, 1, "rwlock_write");
    lock->name = name;
}

void rwlock_read_lock(rwlock_t* lock);
void rwlock_read_unlock(rwlock_t* lock);
void rwlock_write_lock(rwlock_t* lock);
void rwlock_write_unlock(rwlock_t* lock);

#endif // BARRIER_H