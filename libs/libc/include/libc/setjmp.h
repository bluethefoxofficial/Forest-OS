#ifndef LIBC_SETJMP_H
#define LIBC_SETJMP_H

#define __STDC_VERSION_SETJMP_H__ 202311L

typedef int jmp_buf[8];

typedef void (*__jmp_cleanup_fn)(int);

static inline int setjmp(jmp_buf env) {
    (void)env;
    return 0;
}

static inline void longjmp(jmp_buf env, int val) {
    (void)env;
    (void)val;
    __builtin_trap();
}

#endif
