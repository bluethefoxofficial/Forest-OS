#include "include/atomic.h"
#include "include/spinlock.h"
#include "include/semaphore.h"
#include "include/barrier.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/string.h"

static void test_atomic_operations(void) {
    print("[SYNC_TEST] Testing atomic operations...\n");
    
    atomic32_t test_atomic = ATOMIC32_INIT(0);
    
    uint32 old_val = atomic_exchange32(&test_atomic, 42);
    if (old_val != 0) {
        print("  FAIL: atomic_exchange32 returned wrong old value\n");
        return;
    }
    
    uint32 current_val = atomic_load32(&test_atomic);
    if (current_val != 42) {
        print("  FAIL: atomic_load32 returned wrong value\n");
        return;
    }
    
    if (!atomic_compare_and_swap32(&test_atomic, 42, 100)) {
        print("  FAIL: atomic_compare_and_swap32 failed with correct expected value\n");
        return;
    }
    
    if (atomic_compare_and_swap32(&test_atomic, 42, 200)) {
        print("  FAIL: atomic_compare_and_swap32 succeeded with wrong expected value\n");
        return;
    }
    
    uint32 inc_result = atomic_increment32(&test_atomic);
    if (inc_result != 101) {
        print("  FAIL: atomic_increment32 returned wrong value: ");
        print(int_to_string(inc_result));
        print("\n");
        return;
    }
    
    uint32 dec_result = atomic_decrement32(&test_atomic);
    if (dec_result != 100) {
        print("  FAIL: atomic_decrement32 returned wrong value: ");
        print(int_to_string(dec_result));
        print("\n");
        return;
    }
    
    print("  PASS: All atomic operations working correctly\n");
}

static void test_atomic8_operations(void) {
    print("[SYNC_TEST] Testing 8-bit atomic operations...\n");
    
    atomic8_t test_lock = ATOMIC8_INIT(0);
    
    if (atomic_test_and_set8(&test_lock)) {
        print("  FAIL: atomic_test_and_set8 returned true on clear lock\n");
        return;
    }
    
    if (!atomic_test_and_set8(&test_lock)) {
        print("  FAIL: atomic_test_and_set8 returned false on already set lock\n");
        return;
    }
    
    atomic_clear8(&test_lock);
    
    if (atomic_load8(&test_lock) != 0) {
        print("  FAIL: atomic_clear8 did not clear the lock\n");
        return;
    }
    
    atomic_store8(&test_lock, 255);
    if (atomic_load8(&test_lock) != 255) {
        print("  FAIL: atomic_store8/load8 failed\n");
        return;
    }
    
    print("  PASS: All 8-bit atomic operations working correctly\n");
}

static spinlock_t test_spinlock = SPINLOCK_INIT("test_spinlock");
static uint32 shared_counter = 0;

static void test_spinlock_operations(void) {
    print("[SYNC_TEST] Testing spinlock operations...\n");
    
    if (spinlock_is_locked(&test_spinlock)) {
        print("  FAIL: New spinlock reports as locked\n");
        return;
    }
    
    if (!spinlock_try_acquire(&test_spinlock)) {
        print("  FAIL: spinlock_try_acquire failed on unlocked spinlock\n");
        return;
    }
    
    if (!spinlock_is_locked(&test_spinlock)) {
        print("  FAIL: Acquired spinlock reports as unlocked\n");
        return;
    }
    
    if (spinlock_try_acquire(&test_spinlock)) {
        print("  FAIL: spinlock_try_acquire succeeded on locked spinlock\n");
        return;
    }
    
    spinlock_release(&test_spinlock);
    
    if (spinlock_is_locked(&test_spinlock)) {
        print("  FAIL: Released spinlock reports as locked\n");
        return;
    }
    
    for (uint32 i = 0; i < 1000; i++) {
        spinlock_acquire(&test_spinlock);
        shared_counter++;
        spinlock_release(&test_spinlock);
    }
    
    if (shared_counter != 1000) {
        print("  FAIL: Spinlock did not protect shared counter: ");
        print(int_to_string(shared_counter));
        print("\n");
        return;
    }
    
    print("  PASS: All spinlock operations working correctly\n");
}

static semaphore_t test_semaphore = SEMAPHORE_INIT(3, 5, "test_semaphore");

static void test_semaphore_operations(void) {
    print("[SYNC_TEST] Testing semaphore operations...\n");
    
    uint32 initial_count = semaphore_get_count(&test_semaphore);
    if (initial_count != 3) {
        print("  FAIL: Initial semaphore count wrong: ");
        print(int_to_string(initial_count));
        print("\n");
        return;
    }
    
    if (!semaphore_try_wait(&test_semaphore)) {
        print("  FAIL: semaphore_try_wait failed with available count\n");
        return;
    }
    
    uint32 after_wait = semaphore_get_count(&test_semaphore);
    if (after_wait != 2) {
        print("  FAIL: Semaphore count after wait: ");
        print(int_to_string(after_wait));
        print("\n");
        return;
    }
    
    semaphore_post(&test_semaphore);
    
    uint32 after_post = semaphore_get_count(&test_semaphore);
    if (after_post != 3) {
        print("  FAIL: Semaphore count after post: ");
        print(int_to_string(after_post));
        print("\n");
        return;
    }
    
    for (uint32 i = 0; i < 3; i++) {
        if (!semaphore_try_wait(&test_semaphore)) {
            print("  FAIL: semaphore_try_wait failed when count should be available\n");
            return;
        }
    }
    
    if (semaphore_try_wait(&test_semaphore)) {
        print("  FAIL: semaphore_try_wait succeeded when count should be 0\n");
        return;
    }
    
    for (uint32 i = 0; i < 3; i++) {
        semaphore_post(&test_semaphore);
    }
    
    print("  PASS: All semaphore operations working correctly\n");
}

static mutex_t test_mutex = MUTEX_INIT("test_mutex");

static void test_mutex_operations(void) {
    print("[SYNC_TEST] Testing mutex operations...\n");
    
    if (mutex_is_locked(&test_mutex)) {
        print("  FAIL: New mutex reports as locked\n");
        return;
    }
    
    if (!mutex_try_lock(&test_mutex)) {
        print("  FAIL: mutex_try_lock failed on unlocked mutex\n");
        return;
    }
    
    if (!mutex_is_locked(&test_mutex)) {
        print("  FAIL: Acquired mutex reports as unlocked\n");
        return;
    }
    
    if (mutex_try_lock(&test_mutex)) {
        print("  FAIL: mutex_try_lock succeeded on locked mutex\n");
        return;
    }
    
    mutex_unlock(&test_mutex);
    
    if (mutex_is_locked(&test_mutex)) {
        print("  FAIL: Unlocked mutex reports as locked\n");
        return;
    }
    
    for (uint32 i = 0; i < 500; i++) {
        mutex_lock(&test_mutex);
        shared_counter++;
        mutex_unlock(&test_mutex);
    }
    
    print("  PASS: All mutex operations working correctly\n");
}

void sync_test_run_all(void) {
    print_colored("=== SYNCHRONIZATION PRIMITIVES TEST SUITE ===\n", 0x0E, 0x00);
    
    test_atomic_operations();
    test_atomic8_operations();
    test_spinlock_operations();
    test_semaphore_operations();
    test_mutex_operations();
    
    print_colored("=== SYNCHRONIZATION TESTS COMPLETED ===\n", 0x0A, 0x00);
}