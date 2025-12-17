#include <stddef.h>

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static inline int get_host_errno(void) {
    return errno;
}
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
        default: return EINVAL;
    }
}

static inline int assign_errno_and_fail(int raw_errno) {
    errno = normalize_errno_value(raw_errno);
    return -1;
}

#ifdef __linux__
static inline int handle_linux_result(long result) {
    if (result >= 0) {
        errno = 0;
        return (int)result;
    }
    return assign_errno_and_fail(get_host_errno());
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
#ifdef __linux__
    return handle_linux_result(::write(fd, buf, count));
#else
    return handle_forest_result(syscall3(SYS_WRITE, fd, (int32)buf, (int32)count));
#endif
}

ssize_t read(int fd, void *buf, size_t count) {
#ifdef __linux__
    return handle_linux_result(::read(fd, buf, count));
#else
    return handle_forest_result(syscall3(SYS_READ, fd, (int32)buf, (int32)count));
#endif
}

int open(const char *pathname, int flags) {
#ifdef __linux__
    return handle_linux_result(::open(pathname, flags, 0));
#else
    return handle_forest_result(syscall3(SYS_OPEN, (int32)pathname, flags, 0));
#endif
}

int close(int fd) {
#ifdef __linux__
    return handle_linux_result(::close(fd));
#else
    return handle_forest_result(syscall1(SYS_CLOSE, fd));
#endif
}

int lseek(int fd, int offset, int whence) {
#ifdef __linux__
    return handle_linux_result((int)::lseek(fd, offset, whence));
#else
    return handle_forest_result(syscall3(SYS_LSEEK, fd, offset, whence));
#endif
}

int getpid(void) {
#ifdef __linux__
    return handle_linux_result(::getpid());
#else
    return handle_forest_result(syscall0(SYS_GETPID));
#endif
}

int time(int *tloc) {
#ifdef __linux__
    time_t value = ::time(NULL);
    if (value == (time_t)-1) {
        return assign_errno_and_fail(get_host_errno());
    }
    if (tloc) {
        *tloc = (int)value;
    }
    errno = 0;
    return (int)value;
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

int nanosleep(const void *req, void *rem) {
#ifdef __linux__
    return handle_linux_result(::nanosleep((const struct timespec *)req,
                                           (struct timespec *)rem));
#else
    return handle_forest_result(syscall2(SYS_NANOSLEEP, (int32)req, (int32)rem));
#endif
}

int uname(void *uts_buffer) {
#ifdef __linux__
    return handle_linux_result(::uname((struct utsname *)uts_buffer));
#else
    return handle_forest_result(syscall1(SYS_UNAME, (int32)uts_buffer));
#endif
}

int brk(void *addr) {
#ifdef __linux__
    return handle_linux_result(::brk(addr));
#else
    return handle_forest_result(syscall1(SYS_BRK, (int32)addr));
#endif
}

int _exit(int status) {
#ifdef __linux__
    ::_exit(status);
    return 0;
#else
    return handle_forest_result(syscall1(SYS_EXIT, status));
#endif
}

int socket(int domain, int type, int protocol) {
#ifdef __linux__
    return handle_linux_result(::socket(domain, type, protocol));
#else
    return handle_forest_result(syscall3(SYS_SOCKET, domain, type, protocol));
#endif
}

int bind(int fd, const void *addr, int addrlen) {
#ifdef __linux__
    return handle_linux_result(::bind(fd, (const struct sockaddr *)addr,
                                      (socklen_t)addrlen));
#else
    return handle_forest_result(syscall3(SYS_BIND, fd, (int32)addr, addrlen));
#endif
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const void *addr, int addrlen) {
#ifdef __linux__
    return handle_linux_result(::sendto(fd, buf, len, flags,
                                        (const struct sockaddr *)addr,
                                        (socklen_t)addrlen));
#else
    return handle_forest_result(syscall6(SYS_SENDTO, fd, (int32)buf, (int32)len, flags,
                    (int32)addr, addrlen));
#endif
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 void *addr, int *addrlen) {
#ifdef __linux__
    socklen_t sock_len = (addrlen) ? (socklen_t)(*addrlen) : 0;
    ssize_t result = ::recvfrom(fd, buf, len, flags,
                                (struct sockaddr *)addr,
                                addrlen ? &sock_len : NULL);
    if (result < 0) {
        return assign_errno_and_fail(get_host_errno());
    }
    if (addrlen) {
        *addrlen = (int)sock_len;
    }
    errno = 0;
    return result;
#else
    return handle_forest_result(syscall6(SYS_RECVFROM, fd, (int32)buf, (int32)len, flags,
                    (int32)addr, (int32)addrlen));
#endif
}
