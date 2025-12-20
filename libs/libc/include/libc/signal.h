#ifndef LIBC_SIGNAL_H
#define LIBC_SIGNAL_H

typedef int sig_atomic_t;

typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)

#define SIGINT 2
#define SIGTERM 15
#define SIGKILL 9

static inline sighandler_t signal(int sig, sighandler_t handler) {
    (void)sig;
    return handler;
}

static inline int raise(int sig) {
    (void)sig;
    return 0;
}

#endif
