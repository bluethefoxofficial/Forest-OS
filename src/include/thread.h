#ifndef THREAD_H
#define THREAD_H

/*
 * thread.h - Kernel Threading Support Stubs for Forest OS
 *
 * This provides stub declarations for kernel threading functions.
 * Full implementation to be added when the scheduler is complete.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Thread states */
typedef enum {
    THREAD_STATE_CREATED = 0,
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_BLOCKED,
    THREAD_STATE_TERMINATED
} thread_state_t;

/* Thread priority levels */
#define THREAD_PRIORITY_LOW      0
#define THREAD_PRIORITY_NORMAL   10
#define THREAD_PRIORITY_HIGH     20
#define THREAD_PRIORITY_REALTIME 30

/* Thread flags */
#define THREAD_FLAG_KERNEL      0x01
#define THREAD_FLAG_USER        0x02
#define THREAD_FLAG_JOINABLE    0x04
#define THREAD_FLAG_DETACHED    0x08

/* Forward declaration */
struct thread;

/* Thread entry point function */
typedef void *(*thread_entry_t)(void *arg);

/* Semaphore structure for thread synchronization */
struct semaphore {
    volatile int count;
    volatile int waiters;
    /* Additional fields for wait queue would go here */
};

/* Completion structure for thread synchronization */
struct completion {
    volatile unsigned int done;
    volatile int waiters;
};

/* Completion operations */
static inline void init_completion(struct completion *comp)
{
    if (comp) {
        comp->done = 0;
        comp->waiters = 0;
    }
}

static inline void complete(struct completion *comp)
{
    if (comp) {
        __sync_fetch_and_add(&comp->done, 1);
    }
}

static inline void complete_all(struct completion *comp)
{
    if (comp) {
        comp->done = (unsigned int)-1;
    }
}

static inline void wait_for_completion(struct completion *comp)
{
    if (!comp) return;
    while (!comp->done) {
        /* Busy wait - in real implementation, would block */
    }
}

static inline int wait_for_completion_timeout(struct completion *comp, uint32_t timeout_ms)
{
    /* Stub - just check once */
    (void)timeout_ms;
    if (!comp) return -1;
    return comp->done ? 0 : -1;
}

static inline void reinit_completion(struct completion *comp)
{
    if (comp) {
        comp->done = 0;
    }
}

/* CPU relaxation (pause hint for spin loops) */
static inline void cpu_relax(void)
{
    __asm__ volatile("pause" ::: "memory");
}

/* Thread structure (opaque to most code) */
struct thread {
    uint32_t tid;                   /* Thread ID */
    char name[64];                  /* Thread name */
    thread_state_t state;           /* Current state */
    uint8_t priority;               /* Thread priority */
    uint32_t flags;                 /* Thread flags */

    thread_entry_t entry;           /* Entry point function */
    void *arg;                      /* Entry argument */
    void *return_value;             /* Return value from thread */

    void *stack;                    /* Stack pointer */
    size_t stack_size;              /* Stack size */

    /* Context saved when not running */
    void *context;
};

/* Thread creation and management */
struct thread *thread_create(const char *name, thread_entry_t entry, void *arg);
int thread_start(struct thread *thread);
int thread_join(struct thread *thread, void **retval);
void thread_destroy(struct thread *thread);
void thread_exit(void *retval);
struct thread *thread_current(void);
uint32_t thread_get_tid(struct thread *thread);
void thread_yield(void);

/* Semaphore operations */
static inline void semaphore_init(struct semaphore *sem, int initial_count)
{
    if (sem) {
        sem->count = initial_count;
        sem->waiters = 0;
    }
}

static inline void semaphore_up(struct semaphore *sem)
{
    if (sem) {
        __sync_fetch_and_add(&sem->count, 1);
    }
}

static inline int semaphore_down(struct semaphore *sem)
{
    if (!sem) return -1;
    /* Simple spin-wait for now */
    while (__sync_fetch_and_sub(&sem->count, 1) <= 0) {
        __sync_fetch_and_add(&sem->count, 1);
        /* Busy wait - in real implementation, would block */
    }
    return 0;
}

static inline int semaphore_down_timeout(struct semaphore *sem, uint32_t timeout_ms)
{
    /* Stub - just try once and return */
    (void)timeout_ms;
    if (!sem) return -1;
    if (sem->count > 0) {
        __sync_fetch_and_sub(&sem->count, 1);
        return 0;
    }
    return -1;  /* Would timeout */
}

static inline int semaphore_trydown(struct semaphore *sem)
{
    if (!sem) return -1;
    if (sem->count > 0) {
        __sync_fetch_and_sub(&sem->count, 1);
        return 0;
    }
    return -1;
}

#endif /* THREAD_H */
