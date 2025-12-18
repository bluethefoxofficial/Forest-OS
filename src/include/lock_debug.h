#ifndef LOCK_DEBUG_H
#define LOCK_DEBUG_H

#include "types.h"
#include "spinlock.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef LOCK_DEBUG_ENABLED
#define LOCK_DEBUG_ENABLED 1
#endif

#if LOCK_DEBUG_ENABLED

typedef struct lock_debug_info {
    const char* name;
    uint32 acquisition_count;
    uint32 contention_count;
    uint32 max_hold_time;
    uint32 total_hold_time;
    uint32 last_acquired_at;
    uint32 current_holder_cpu;
    bool currently_held;
    struct lock_debug_info* next;
} lock_debug_info_t;

void lock_debug_init(void);
void lock_debug_register_lock(const char* name, void* lock_ptr);
void lock_debug_record_acquire(const char* name, uint32 timestamp);
void lock_debug_record_release(const char* name, uint32 timestamp);
void lock_debug_record_contention(const char* name);
void lock_debug_print_stats(void);
void lock_debug_detect_deadlocks(void);

#define LOCK_DEBUG_REGISTER(name, lock) lock_debug_register_lock(name, &(lock))
#define LOCK_DEBUG_ACQUIRE(name) lock_debug_record_acquire(name, get_timestamp())
#define LOCK_DEBUG_RELEASE(name) lock_debug_record_release(name, get_timestamp())
#define LOCK_DEBUG_CONTENTION(name) lock_debug_record_contention(name)

static inline uint32 get_timestamp(void) {
    uint32 low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return low;
}

typedef struct debug_spinlock {
    spinlock_t lock;
    lock_debug_info_t debug_info;
} debug_spinlock_t;

#define DEBUG_SPINLOCK_INIT(lock_name) { \
    .lock = SPINLOCK_INIT(lock_name), \
    .debug_info = { \
        .name = lock_name, \
        .acquisition_count = 0, \
        .contention_count = 0, \
        .max_hold_time = 0, \
        .total_hold_time = 0, \
        .last_acquired_at = 0, \
        .current_holder_cpu = 0, \
        .currently_held = false, \
        .next = NULL \
    } \
}

#define DEFINE_DEBUG_SPINLOCK(name, lock_name) \
    static debug_spinlock_t name = DEBUG_SPINLOCK_INIT(lock_name)

static inline void debug_spinlock_init(debug_spinlock_t* lock, const char* name) {
    spinlock_init(&lock->lock, name);
    lock->debug_info.name = name;
    lock->debug_info.acquisition_count = 0;
    lock->debug_info.contention_count = 0;
    lock->debug_info.max_hold_time = 0;
    lock->debug_info.total_hold_time = 0;
    lock->debug_info.last_acquired_at = 0;
    lock->debug_info.current_holder_cpu = 0;
    lock->debug_info.currently_held = false;
    lock->debug_info.next = NULL;
    LOCK_DEBUG_REGISTER(name, lock);
}

static inline void debug_spinlock_acquire(debug_spinlock_t* lock) {
    uint32 start_time = get_timestamp();
    
    if (spinlock_is_locked(&lock->lock)) {
        LOCK_DEBUG_CONTENTION(lock->debug_info.name);
    }
    
    spinlock_acquire(&lock->lock);
    
    lock->debug_info.acquisition_count++;
    lock->debug_info.last_acquired_at = start_time;
    lock->debug_info.currently_held = true;
    
    LOCK_DEBUG_ACQUIRE(lock->debug_info.name);
}

static inline void debug_spinlock_release(debug_spinlock_t* lock) {
    uint32 release_time = get_timestamp();
    uint32 hold_time = release_time - lock->debug_info.last_acquired_at;
    
    lock->debug_info.total_hold_time += hold_time;
    if (hold_time > lock->debug_info.max_hold_time) {
        lock->debug_info.max_hold_time = hold_time;
    }
    lock->debug_info.currently_held = false;
    
    LOCK_DEBUG_RELEASE(lock->debug_info.name);
    spinlock_release(&lock->lock);
}

static inline bool debug_spinlock_try_acquire(debug_spinlock_t* lock) {
    uint32 start_time = get_timestamp();
    
    if (spinlock_try_acquire(&lock->lock)) {
        lock->debug_info.acquisition_count++;
        lock->debug_info.last_acquired_at = start_time;
        lock->debug_info.currently_held = true;
        LOCK_DEBUG_ACQUIRE(lock->debug_info.name);
        return true;
    }
    
    LOCK_DEBUG_CONTENTION(lock->debug_info.name);
    return false;
}

#else // LOCK_DEBUG_ENABLED

#define LOCK_DEBUG_REGISTER(name, lock) do {} while(0)
#define LOCK_DEBUG_ACQUIRE(name) do {} while(0)
#define LOCK_DEBUG_RELEASE(name) do {} while(0)
#define LOCK_DEBUG_CONTENTION(name) do {} while(0)

typedef spinlock_t debug_spinlock_t;

#define DEBUG_SPINLOCK_INIT(lock_name) SPINLOCK_INIT(lock_name)
#define DEFINE_DEBUG_SPINLOCK(name, lock_name) DEFINE_SPINLOCK(name, lock_name)

#define debug_spinlock_init(lock, name) spinlock_init(lock, name)
#define debug_spinlock_acquire(lock) spinlock_acquire(lock)
#define debug_spinlock_release(lock) spinlock_release(lock)
#define debug_spinlock_try_acquire(lock) spinlock_try_acquire(lock)

static inline void lock_debug_init(void) {}
static inline void lock_debug_print_stats(void) {}
static inline void lock_debug_detect_deadlocks(void) {}

#endif // LOCK_DEBUG_ENABLED

#endif // LOCK_DEBUG_H