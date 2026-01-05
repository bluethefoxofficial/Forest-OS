#ifndef ATOMIC_H
#define ATOMIC_H

#include "types.h"

typedef struct {
    volatile uint32 value;
} atomic32_t;

typedef struct {
    volatile uint8 value;
} atomic8_t;

typedef struct {
    volatile uint64 value;
} atomic64_t;

/* Common atomic type aliases */
typedef atomic32_t atomic_t;  /* Default atomic type for compatibility */

/* Atomic initialization macros */
#define ATOMIC32_INIT(val) { .value = (val) }
#define ATOMIC8_INIT(val) { .value = (val) }
#define ATOMIC64_INIT(val) { .value = (val) }
#define ATOMIC_INIT(val) ATOMIC32_INIT(val)

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

/* Compatibility functions for existing code */
static inline void atomic_set(atomic_t *ptr, uint32 value) {
    atomic_store32(ptr, value);
}

static inline uint32 atomic_read(const atomic_t *ptr) {
    return atomic_load32(ptr);
}

static inline void atomic_inc(atomic_t *ptr) {
    atomic_increment32(ptr);
}

static inline void atomic_dec(atomic_t *ptr) {
    atomic_decrement32(ptr);
}

/* Additional atomic operations for Linux compatibility */
static inline int atomic_dec_and_test(atomic_t *ptr) {
    return atomic_decrement32(ptr) == 0;
}

static inline int atomic_inc_return(atomic_t *ptr) {
    return atomic_fetch_add32(ptr, 1) + 1;
}

static inline int atomic_dec_return(atomic_t *ptr) {
    return atomic_fetch_sub32(ptr, 1) - 1;
}

static inline int atomic_add_return(int i, atomic_t *ptr) {
    return atomic_fetch_add32(ptr, i) + i;
}

static inline int atomic_sub_return(int i, atomic_t *ptr) {
    return atomic_fetch_sub32(ptr, (uint32)i) - i;
}

static inline void atomic_add(int i, atomic_t *ptr) {
    atomic_fetch_add32(ptr, i);
}

static inline void atomic_sub(int i, atomic_t *ptr) {
    atomic_fetch_sub32(ptr, (uint32)i);
}

static inline int atomic_sub_and_test(int i, atomic_t *ptr) {
    return atomic_sub_return(i, ptr) == 0;
}

static inline int atomic_inc_and_test(atomic_t *ptr) {
    return atomic_inc_return(ptr) == 0;
}

static inline int atomic_add_negative(int i, atomic_t *ptr) {
    return atomic_add_return(i, ptr) < 0;
}

static inline int atomic_cmpxchg(atomic_t *ptr, int old, int new) {
    int ret = old;
    atomic_compare_and_swap32(ptr, (uint32)old, (uint32)new);
    return ret;
}

/* 64-bit atomic operations (simplified for 32-bit systems) */
static inline void atomic64_set(atomic64_t *ptr, uint64 value) {
    __asm__ volatile (
        "movl %2, %0\n\t"
        "movl %3, %1"
        : "=m" (((volatile uint32*)ptr)[0]),
          "=m" (((volatile uint32*)ptr)[1])
        : "r" ((uint32)value),
          "r" ((uint32)(value >> 32))
        : "memory"
    );
}

static inline uint64 atomic64_read(const atomic64_t *ptr) {
    uint64 result;
    __asm__ volatile (
        "movl %1, %%eax\n\t"
        "movl %2, %%edx"
        : "=A" (result)
        : "m" (((const volatile uint32*)ptr)[0]),
          "m" (((const volatile uint32*)ptr)[1])
        : "memory"
    );
    return result;
}

static inline void atomic64_inc(atomic64_t *ptr) {
    __asm__ volatile (
        "lock; incl %0\n\t"
        "jnz 1f\n\t"
        "lock; incl %1\n"
        "1:"
        : "+m" (((volatile uint32*)ptr)[0]),
          "+m" (((volatile uint32*)ptr)[1])
        : 
        : "memory", "cc"
    );
}

#endif // ATOMIC_H
