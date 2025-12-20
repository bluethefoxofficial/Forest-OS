#ifndef LIBC_STDATOMIC_H
#define LIBC_STDATOMIC_H

#define __STDC_VERSION_STDATOMIC_H__ 202311L

typedef _Atomic _Bool atomic_bool;
typedef _Atomic unsigned int atomic_uint;

#define ATOMIC_FLAG_INIT 0

typedef struct {
    atomic_bool _Value;
} atomic_flag;

static inline void atomic_flag_clear(atomic_flag *obj) {
    obj->_Value = 0;
}

static inline void atomic_flag_test_and_set(atomic_flag *obj) {
    obj->_Value = 1;
}

#endif
