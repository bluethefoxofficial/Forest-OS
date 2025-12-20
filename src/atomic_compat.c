#include "include/types.h"

int __sync_fetch_and_add_4(volatile int* ptr, int value) {
    int old;
    __asm__ volatile(
        "lock xaddl %0, %1"
        : "=r"(old), "+m"(*ptr)
        : "0"(value)
        : "memory");
    return old;
}

int __sync_fetch_and_sub_4(volatile int* ptr, int value) {
    return __sync_fetch_and_add_4(ptr, -value);
}
