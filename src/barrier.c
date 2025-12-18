#include "include/barrier.h"
#include "include/task.h"

void barrier_wait(barrier_t* barrier) {
    mutex_lock(&barrier->mutex);
    
    uint32 current_generation = atomic_load32(&barrier->generation);
    uint32 current_count = atomic_increment32(&barrier->count);
    
    if (current_count == barrier->total_threads) {
        atomic_store32(&barrier->count, 0);
        atomic_increment32(&barrier->generation);
        
        for (uint32 i = 0; i < barrier->total_threads - 1; i++) {
            semaphore_post(&barrier->barrier_sem);
        }
        
        mutex_unlock(&barrier->mutex);
        return;
    }
    
    mutex_unlock(&barrier->mutex);
    
    while (atomic_load32(&barrier->generation) == current_generation) {
        semaphore_wait(&barrier->barrier_sem);
    }
}

void spinbarrier_wait(spinbarrier_t* barrier) {
    uint32 current_generation = atomic_load32(&barrier->generation);
    uint32 current_count;
    
    spinlock_acquire(&barrier->lock);
    current_count = atomic_increment32(&barrier->arrived);
    
    if (current_count == barrier->total_threads) {
        atomic_store32(&barrier->arrived, 0);
        atomic_increment32(&barrier->generation);
        spinlock_release(&barrier->lock);
        return;
    }
    
    spinlock_release(&barrier->lock);
    
    while (atomic_load32(&barrier->generation) == current_generation) {
        cpu_pause();
    }
}

void rwlock_read_lock(rwlock_t* lock) {
    semaphore_wait(&lock->read_sem);
    
    mutex_lock(&lock->count_mutex);
    uint32 readers = atomic_increment32(&lock->count);
    
    if (readers == 1) {
        semaphore_wait(&lock->write_sem);
    }
    
    mutex_unlock(&lock->count_mutex);
    semaphore_post(&lock->read_sem);
}

void rwlock_read_unlock(rwlock_t* lock) {
    mutex_lock(&lock->count_mutex);
    uint32 readers = atomic_decrement32(&lock->count);
    
    if (readers == 0) {
        semaphore_post(&lock->write_sem);
    }
    
    mutex_unlock(&lock->count_mutex);
}

void rwlock_write_lock(rwlock_t* lock) {
    semaphore_wait(&lock->write_sem);
}

void rwlock_write_unlock(rwlock_t* lock) {
    semaphore_post(&lock->write_sem);
}