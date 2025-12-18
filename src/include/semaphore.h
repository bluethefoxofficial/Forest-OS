#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "types.h"
#include "atomic.h"
#include "spinlock.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef struct task task_t;

typedef struct semaphore_wait_node {
    task_t* task;
    struct semaphore_wait_node* next;
} semaphore_wait_node_t;

typedef struct {
    atomic32_t count;
    spinlock_t wait_lock;
    semaphore_wait_node_t* wait_queue_head;
    semaphore_wait_node_t* wait_queue_tail;
    const char* name;
    uint32 max_count;
} semaphore_t;

#define SEMAPHORE_INIT(initial_count, max_val, sem_name) { \
    .count = ATOMIC32_INIT(initial_count), \
    .wait_lock = SPINLOCK_INIT("sem_wait_lock"), \
    .wait_queue_head = NULL, \
    .wait_queue_tail = NULL, \
    .name = sem_name, \
    .max_count = max_val \
}

#define DEFINE_SEMAPHORE(name, initial_count, max_val, sem_name) \
    static semaphore_t name = SEMAPHORE_INIT(initial_count, max_val, sem_name)

static inline void semaphore_init(semaphore_t* sem, uint32 initial_count, uint32 max_count, const char* name) {
    atomic_store32(&sem->count, initial_count);
    spinlock_init(&sem->wait_lock, "sem_wait_lock");
    sem->wait_queue_head = NULL;
    sem->wait_queue_tail = NULL;
    sem->name = name;
    sem->max_count = max_count;
}

void semaphore_wait(semaphore_t* sem);
bool semaphore_try_wait(semaphore_t* sem);
void semaphore_post(semaphore_t* sem);
uint32 semaphore_get_count(const semaphore_t* sem);

typedef struct {
    semaphore_t sem;
} mutex_t;

#define MUTEX_INIT(mutex_name) { \
    .sem = SEMAPHORE_INIT(1, 1, mutex_name) \
}

#define DEFINE_MUTEX(name, mutex_name) \
    static mutex_t name = MUTEX_INIT(mutex_name)

static inline void mutex_init(mutex_t* mutex, const char* name) {
    semaphore_init(&mutex->sem, 1, 1, name);
}

static inline void mutex_lock(mutex_t* mutex) {
    semaphore_wait(&mutex->sem);
}

static inline bool mutex_try_lock(mutex_t* mutex) {
    return semaphore_try_wait(&mutex->sem);
}

static inline void mutex_unlock(mutex_t* mutex) {
    semaphore_post(&mutex->sem);
}

static inline bool mutex_is_locked(const mutex_t* mutex) {
    return semaphore_get_count(&mutex->sem) == 0;
}

#endif // SEMAPHORE_H