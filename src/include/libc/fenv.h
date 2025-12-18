#ifndef LIBC_FENV_H
#define LIBC_FENV_H

#define __STDC_VERSION_FENV_H__ 202311L

typedef unsigned int fenv_t;
typedef unsigned int fexcept_t;

static inline int fegetenv(fenv_t *envp) {
    if (envp) {
        *envp = 0;
    }
    return 0;
}

static inline int fesetenv(const fenv_t *envp) {
    (void)envp;
    return 0;
}

#endif
