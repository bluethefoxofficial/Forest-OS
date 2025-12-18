#ifndef ATOMIC_H
#define ATOMIC_H

#include "types.h"

typedef struct {
    volatile uint32 value;
} atomic32_t;

typedef struct {
    volatile uint8 value;
} atomic8_t;

static inline void memory_barrier(void) {
    __asm__ volatile ("" ::: "memory");
}

static inline void cpu_pause(void) {
    __asm__ volatile ("pause" ::: "memory");
}

static inline uint32 atomic_load32(const atomic32_t* ptr) {
    uint32 result;
    __asm__ volatile (
        "movl %1, %0"
        : "=r" (result)
        : "m" (ptr->value)
        : "memory"
    );
    return result;
}

static inline void atomic_store32(atomic32_t* ptr, uint32 value) {
    __asm__ volatile (
        "movl %1, %0"
        : "=m" (ptr->value)
        : "r" (value)
        : "memory"
    );
}

static inline uint32 atomic_exchange32(atomic32_t* ptr, uint32 new_value) {
    uint32 old_value;
    __asm__ volatile (
        "xchgl %0, %1"
        : "=r" (old_value), "=m" (ptr->value)
        : "0" (new_value), "m" (ptr->value)
        : "memory"
    );
    return old_value;
}

static inline bool atomic_compare_and_swap32(atomic32_t* ptr, uint32 expected, uint32 desired) {
    uint8 success;
    __asm__ volatile (
        "lock cmpxchgl %2, %1\n\t"
        "sete %0"
        : "=a" (success), "=m" (ptr->value)
        : "r" (desired), "a" (expected), "m" (ptr->value)
        : "memory"
    );
    return success != 0;
}

static inline uint32 atomic_fetch_add32(atomic32_t* ptr, uint32 value) {
    uint32 old_value;
    __asm__ volatile (
        "lock xaddl %0, %1"
        : "=r" (old_value), "=m" (ptr->value)
        : "0" (value), "m" (ptr->value)
        : "memory"
    );
    return old_value;
}

static inline uint32 atomic_fetch_sub32(atomic32_t* ptr, uint32 value) {
    return atomic_fetch_add32(ptr, -(int32)value);
}

static inline uint32 atomic_increment32(atomic32_t* ptr) {
    return atomic_fetch_add32(ptr, 1) + 1;
}

static inline uint32 atomic_decrement32(atomic32_t* ptr) {
    return atomic_fetch_sub32(ptr, 1) - 1;
}

static inline bool atomic_test_and_set8(atomic8_t* ptr) {
    uint8 old_value;
    __asm__ volatile (
        "xchgb %0, %1"
        : "=r" (old_value), "=m" (ptr->value)
        : "0" (1), "m" (ptr->value)
        : "memory"
    );
    return old_value != 0;
}

static inline void atomic_clear8(atomic8_t* ptr) {
    __asm__ volatile (
        "movb $0, %0"
        : "=m" (ptr->value)
        :
        : "memory"
    );
}

static inline uint8 atomic_load8(const atomic8_t* ptr) {
    uint8 result;
    __asm__ volatile (
        "movb %1, %0"
        : "=r" (result)
        : "m" (ptr->value)
        : "memory"
    );
    return result;
}

static inline void atomic_store8(atomic8_t* ptr, uint8 value) {
    __asm__ volatile (
        "movb %1, %0"
        : "=m" (ptr->value)
        : "r" (value)
        : "memory"
    );
}

#define ATOMIC32_INIT(val) { .value = (val) }
#define ATOMIC8_INIT(val) { .value = (val) }

#endif // ATOMIC_H