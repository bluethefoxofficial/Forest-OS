#include <stddef.h>

#ifdef FOREST_USE_HOST_LIBC
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#undef errno
#endif

#include "../../src/include/libc/errno.h"
#include "../../src/include/libc/unistd.h"
#include "../../src/include/types.h"
#include "../../src/include/syscall.h"

typedef int32 ssize_t;

static int normalize_errno_value(int raw_errno) {
    switch (raw_errno) {
        case EPERM: return EPERM;
        case ENOENT: return ENOENT;
        case ESRCH: return ESRCH;
        case EINTR: return EINTR;
        case EIO: return EIO;
        case ENXIO: return ENXIO;
        case E2BIG: return E2BIG;
        case ENOEXEC: return ENOEXEC;
        case EBADF: return EBADF;
        case ECHILD: return ECHILD;
        case EAGAIN: return EAGAIN;
        case ENOMEM: return ENOMEM;
        case EACCES: return EACCES;
        case EFAULT: return EFAULT;
        case EBUSY: return EBUSY;
        case EEXIST: return EEXIST;
        case EXDEV: return EXDEV;
        case ENODEV: return ENODEV;
        case ENOTDIR: return ENOTDIR;
        case EISDIR: return EISDIR;
        case EINVAL: return EINVAL;
        case ENFILE: return ENFILE;
        case EMFILE: return EMFILE;
        case ENOTTY: return ENOTTY;
        case EFBIG: return EFBIG;
        case ENOSPC: return ENOSPC;
        case ESPIPE: return ESPIPE;
        case EROFS: return EROFS;
        case EMLINK: return EMLINK;
        case EPIPE: return EPIPE;
        case EDOM: return EDOM;
        case ERANGE: return ERANGE;
        case ENOSYS: return ENOSYS;
        default: return EINVAL;
    }
}

static inline int assign_errno_and_fail(int raw_errno) {
    errno = normalize_errno_value(raw_errno);
    return -1;
}

#ifdef FOREST_USE_HOST_LIBC
static inline int handle_linux_result(long result) {
    if (result >= 0) {
        errno = 0;
        return (int)result;
    }
    return assign_errno_and_fail(errno);
}

static inline int handle_linux_stub(void) {
    return assign_errno_and_fail(ENOSYS);
}

static inline int linux_fake_time(void) {
    static int fake_now = 0;
    fake_now++;
    return fake_now;
}

static void populate_forest_uname(struct utsname *uts_buffer) {
    if (!uts_buffer) {
        return;
    }

    memset(uts_buffer, 0, sizeof(struct utsname));
    (void)snprintf(uts_buffer->sysname, sizeof(uts_buffer->sysname), "ForestOS");
    (void)snprintf(uts_buffer->nodename, sizeof(uts_buffer->nodename), "forest-node");
    (void)snprintf(uts_buffer->release, sizeof(uts_buffer->release), "0.2");
    (void)snprintf(uts_buffer->version, sizeof(uts_buffer->version), "nightly");
    (void)snprintf(uts_buffer->machine, sizeof(uts_buffer->machine), "i386");
}
#else
static inline int32 syscall0(int32 num) {
    int32 ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory");
    return ret;
}

static inline int32 syscall1(int32 num, int32 a1) {
    int32 ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1)
        : "memory");
    return ret;
}

static inline int32 syscall2(int32 num, int32 a1, int32 a2) {
    int32 ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2)
        : "memory");
    return ret;
}

static inline int32 syscall3(int32 num, int32 a1, int32 a2, int32 a3) {
    int32 ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory");
    return ret;
}

static inline int32 syscall4(int32 num, int32 a1, int32 a2, int32 a3, int32 a4) {
    int32 ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
        : "memory");
    return ret;
}

static inline int32 syscall5(int32 num, int32 a1, int32 a2, int32 a3, int32 a4, int32 a5) {
    int32 ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
        : "memory");
    return ret;
}

static inline int32 syscall6(int32 num, int32 a1, int32 a2, int32 a3,
                             int32 a4, int32 a5, int32 a6) {
    int32 ret;
    __asm__ __volatile__(
        "push %%ebp\n"
        "mov %7, %%ebp\n"
        "int $0x80\n"
        "pop %%ebp\n"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3),
          "S"(a4), "D"(a5), "g"(a6)
        : "memory");
    return ret;
}

static inline int32 handle_forest_result(int32 result) {
    if (result >= 0) {
        errno = 0;
        return result;
    }
    return assign_errno_and_fail(-result);
}
#endif

ssize_t write(int fd, const void *buf, size_t count) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::write(fd, buf, count));
#else
    return handle_forest_result(syscall3(SYS_WRITE, fd, (int32)buf, (int32)count));
#endif
}

ssize_t read(int fd, void *buf, size_t count) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::read(fd, buf, count));
#else
    return handle_forest_result(syscall3(SYS_READ, fd, (int32)buf, (int32)count));
#endif
}

int open(const char *pathname, int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pathname;
    (void)flags;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_OPEN, (int32)pathname, flags, 0));
#endif
}

int close(int fd) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    errno = 0;
    return 0;
#else
    return handle_forest_result(syscall1(SYS_CLOSE, fd));
#endif
}

int lseek(int fd, int offset, int whence) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    (void)offset;
    (void)whence;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_LSEEK, fd, offset, whence));
#endif
}

int getpid(void) {
#ifdef FOREST_USE_HOST_LIBC
    errno = 0;
    return 1;
#else
    return handle_forest_result(syscall0(SYS_GETPID));
#endif
}

int unlink(const char *pathname) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pathname;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_UNLINK, (int32)pathname));
#endif
}

int time(int *tloc) {
#ifdef FOREST_USE_HOST_LIBC
    int value = linux_fake_time();
    if (tloc) {
        *tloc = value;
    }
    errno = 0;
    return value;
#else
    int value = syscall1(SYS_TIME, (int32)tloc);
    if (value < 0) {
        return assign_errno_and_fail(-value);
    }
    if (tloc) {
        *tloc = value;
    }
    errno = 0;
    return value;
#endif
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::nanosleep(req, rem));
#else
    return handle_forest_result(syscall2(SYS_NANOSLEEP, (int32)req, (int32)rem));
#endif
}

int uname(struct utsname *uts_buffer) {
#ifdef FOREST_USE_HOST_LIBC
    if (!uts_buffer) {
        return assign_errno_and_fail(EINVAL);
    }
    populate_forest_uname(uts_buffer);
    errno = 0;
    return 0;
#else
    return handle_forest_result(syscall1(SYS_UNAME, (int32)uts_buffer));
#endif
}

int brk(void *addr) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::brk(addr));
#else
    return handle_forest_result(syscall1(SYS_BRK, (int32)addr));
#endif
}

int _exit(int status) {
#ifdef FOREST_USE_HOST_LIBC
    ::_exit(status);
    return 0;
#else
    return handle_forest_result(syscall1(SYS_EXIT, status));
#endif
}

int socket(int domain, int type, int protocol) {
#ifdef FOREST_USE_HOST_LIBC
    (void)domain;
    (void)type;
    (void)protocol;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_SOCKET, domain, type, protocol));
#endif
}

int bind(int fd, const void *addr, int addrlen) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    (void)addr;
    (void)addrlen;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_BIND, fd, (int32)addr, addrlen));
#endif
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const void *addr, int addrlen) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    (void)buf;
    (void)len;
    (void)flags;
    (void)addr;
    (void)addrlen;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall6(SYS_SENDTO, fd, (int32)buf, (int32)len, flags,
                    (int32)addr, addrlen));
#endif
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 void *addr, int *addrlen) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    (void)buf;
    (void)len;
    (void)flags;
    (void)addr;
    (void)addrlen;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall6(SYS_RECVFROM, fd, (int32)buf, (int32)len, flags,
                    (int32)addr, (int32)addrlen));
#endif
}

int netinfo(net_socket_info_t* buffer, int max_entries) {
#ifdef FOREST_USE_HOST_LIBC
    (void)buffer;
    (void)max_entries;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_NETINFO, (int32)buffer, max_entries));
#endif
}
