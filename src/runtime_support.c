#include "include/types.h"
#include "include/atomic.h"

// 64-bit unsigned division and modulo runtime support functions
// These are required by uACPI for 64-bit math operations

uint64 __udivdi3(uint64 num, uint64 den) {
    if (den == 0) {
        return 0;
    }
    
    uint64 quot = 0, qbit = 1;
    
    if (num < den) {
        return 0;
    }
    
    // Left-shift the divisor until it's bigger than dividend
    while ((int64)den >= 0 && den < num) {
        den <<= 1;
        qbit <<= 1;
    }
    
    // Do division by repeatedly subtracting shifted divisor
    while (qbit) {
        if (den <= num) {
            num -= den;
            quot += qbit;
        }
        den >>= 1;
        qbit >>= 1;
    }
    
    return quot;
}

uint64 __umoddi3(uint64 num, uint64 den) {
    if (den == 0) {
        return num;
    }
    return num - (den * __udivdi3(num, den));
}

// Atomic operations support functions
// These provide thread-safe atomic operations for uACPI
// Note: These work with raw pointers, not atomic32_t structures

uint32 __atomic_fetch_add_4(volatile uint32* ptr, uint32 val, int memorder) {
    (void)memorder; // Forest OS doesn't support different memory ordering
    
    uint32 old_value;
    __asm__ volatile (
        "lock xaddl %0, %1"
        : "=r" (old_value), "=m" (*ptr)
        : "0" (val), "m" (*ptr)
        : "memory"
    );
    return old_value;
}

uint32 __atomic_fetch_sub_4(volatile uint32* ptr, uint32 val, int memorder) {
    (void)memorder;
    return __atomic_fetch_add_4(ptr, -(int32)val, memorder);
}

bool __atomic_compare_exchange_4(volatile uint32* ptr, uint32* expected, uint32 desired, bool weak, int success_memorder, int failure_memorder) {
    (void)weak;
    (void)success_memorder;
    (void)failure_memorder;
    
    uint32 old_val = *expected;
    uint8 success;
    
    __asm__ volatile (
        "lock cmpxchgl %3, %1\n\t"
        "sete %0"
        : "=q" (success), "=m" (*ptr), "+a" (old_val)
        : "r" (desired), "m" (*ptr)
        : "memory"
    );
    
    if (!success) {
        *expected = old_val;
    }
    
    return success != 0;
}