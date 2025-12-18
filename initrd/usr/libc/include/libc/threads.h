#ifndef LIBC_THREADS_H
#define LIBC_THREADS_H

typedef int thrd_t;
typedef int once_flag;

typedef int (*thrd_start_t)(void *);

enum {
    thrd_success,
    thrd_error,
    thrd_nomem,
    thrd_timedout,
    thrd_busy
};

static inline int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    (void)thr;
    (void)func;
    (void)arg;
    return thrd_error;
}

static inline void thrd_exit(int res) {
    (void)res;
}

static inline int thrd_join(thrd_t thr, int *res) {
    (void)thr;
    (void)res;
    return thrd_error;
}

#endif
