#include <stddef.h>
#include <stdarg.h>

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
#include "../../src/include/libc/stdio.h"
#include "../../src/include/libc/unistd.h"
#include "../../src/include/libc/time.h"
#include "../../src/include/types.h"
#include "../../src/include/syscall.h"
#include "../../src/include/power.h"

// Additional types needed for extended syscalls
#ifndef ARCH_64BIT
#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_64BIT 1
#else
#define ARCH_64BIT 0
#endif
#endif

#if ARCH_64BIT
typedef long off_t;
typedef unsigned int mode_t;
typedef void (*sighandler_t)(int);
typedef unsigned long nfds_t;
#else
typedef int32 off_t;
typedef uint16 mode_t;
typedef void (*sighandler_t)(int);
typedef unsigned int nfds_t;
#endif

// Signal types (stubs since signals aren't implemented)
typedef struct {
    unsigned long __val[16];
} sigset_t;

#define SIG_ERR ((sighandler_t)-1)

// File descriptor sets (stubs for select)
typedef struct {
    unsigned long fds_bits[1024 / (8 * sizeof(unsigned long))];
} fd_set;

// Framebuffer info structure
struct fb_info {
    void* addr;
    uintptr_t phys_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t size;
    uint32_t format;
    uint32_t flags;
};

// I/O vector structure (for readv/writev stubs)
struct iovec {
    void  *iov_base;
    size_t iov_len;
};

// Poll structure (for poll stub)
struct pollfd {
    int fd;
    short events;
    short revents;
};

// Stat structures (for compatibility)
struct stat {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned long st_mode;
    unsigned long st_nlink;
    unsigned long st_uid;
    unsigned long st_gid;
    unsigned long st_rdev;
    long st_size;
    long st_blksize;
    long st_blocks;
    long st_atime;
    long st_mtime;
    long st_ctime;
};



struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

// Sigaction structure (stub)
struct sigaction {
    sighandler_t sa_handler;
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

#ifndef ARCH_64BIT
#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_64BIT 1
#else
#define ARCH_64BIT 0
#endif
#endif
typedef long sysret_t;
typedef unsigned long sysarg_t;

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
#if ARCH_64BIT
static inline sysret_t syscall0(sysarg_t num) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory");
    return ret;
}

static inline sysret_t syscall1(sysarg_t num, sysarg_t a1) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1)
        : "memory");
    return ret;
}

static inline sysret_t syscall2(sysarg_t num, sysarg_t a1, sysarg_t a2) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2)
        : "memory");
    return ret;
}

static inline sysret_t syscall3(sysarg_t num, sysarg_t a1, sysarg_t a2, sysarg_t a3) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory");
    return ret;
}

static inline sysret_t syscall4(sysarg_t num, sysarg_t a1, sysarg_t a2, sysarg_t a3, sysarg_t a4) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
        : "memory");
    return ret;
}

static inline sysret_t syscall5(sysarg_t num, sysarg_t a1, sysarg_t a2, sysarg_t a3, sysarg_t a4, sysarg_t a5) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
        : "memory");
    return ret;
}

static inline sysret_t syscall6(sysarg_t num, sysarg_t a1, sysarg_t a2, sysarg_t a3, sysarg_t a4, sysarg_t a5, sysarg_t a6) {
    sysret_t ret;
    __asm__ __volatile__(
        "push %%rbp\n"
        "mov %[arg6], %%rbp\n"
        "int $0x80\n"
        "pop %%rbp\n"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3),
          "S"(a4), "D"(a5), [arg6]"r"(a6)
        : "memory");
    return ret;
}
#else
static inline sysret_t syscall0(sysarg_t num) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory");
    return ret;
}

static inline sysret_t syscall1(sysarg_t num, sysarg_t a1) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1)
        : "memory");
    return ret;
}

static inline sysret_t syscall2(sysarg_t num, sysarg_t a1, sysarg_t a2) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2)
        : "memory");
    return ret;
}

static inline sysret_t syscall3(sysarg_t num, sysarg_t a1, sysarg_t a2, sysarg_t a3) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory");
    return ret;
}

static inline sysret_t syscall4(sysarg_t num, sysarg_t a1, sysarg_t a2, sysarg_t a3, sysarg_t a4) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
        : "memory");
    return ret;
}

static inline sysret_t syscall5(sysarg_t num, sysarg_t a1, sysarg_t a2, sysarg_t a3, sysarg_t a4, sysarg_t a5) {
    sysret_t ret;
    __asm__ __volatile__("int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
        : "memory");
    return ret;
}

static inline sysret_t syscall6(sysarg_t num, sysarg_t a1, sysarg_t a2, sysarg_t a3,
                             sysarg_t a4, sysarg_t a5, sysarg_t a6) {
    sysret_t ret;
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
#endif /* ARCH_64BIT */

static inline sysret_t handle_forest_result(sysret_t result) {
    if (result >= 0) {
        errno = 0;
        return result;
    }
    return assign_errno_and_fail((int)(-result));
}
#endif

ssize_t write(int fd, const void *buf, size_t count) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::write(fd, buf, count));
#else
    return handle_forest_result(syscall3(SYS_WRITE, fd, (sysarg_t)buf, (sysarg_t)count));
#endif
}

ssize_t read(int fd, void *buf, size_t count) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::read(fd, buf, count));
#else
    return handle_forest_result(syscall3(SYS_READ, fd, (sysarg_t)buf, (sysarg_t)count));
#endif
}

int open(const char *pathname, int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pathname;
    (void)flags;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_OPEN, (sysarg_t)pathname, flags, 0));
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
    return handle_forest_result(syscall1(SYS_UNLINK, (sysarg_t)pathname));
#endif
}

int mknod(const char *pathname, uint32_t mode, uint32_t dev) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pathname;
    (void)mode;
    (void)dev;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_MKNOD, (sysarg_t)pathname, (sysarg_t)mode, (sysarg_t)dev));
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
    int value = (int)syscall1(SYS_TIME, (sysarg_t)tloc);
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
    return handle_forest_result(syscall2(SYS_NANOSLEEP, (sysarg_t)req, (sysarg_t)rem));
#endif
}

unsigned int usleep(useconds_t useconds) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::usleep(useconds));
#else
    struct timespec req;
    req.tv_sec = useconds / 1000000;
    req.tv_nsec = (useconds % 1000000) * 1000;
    return nanosleep(&req, NULL);
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
    return handle_forest_result(syscall1(SYS_UNAME, (sysarg_t)uts_buffer));
#endif
}

int brk(void *addr) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::brk(addr));
#else
    return handle_forest_result(syscall1(SYS_BRK, (sysarg_t)addr));
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

int socketpair(int domain, int type, int protocol, int sv[2]) {
#ifdef FOREST_USE_HOST_LIBC
    (void)domain;
    (void)type;
    (void)protocol;
    (void)sv;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall4(SYS_SOCKETPAIR, domain, type, protocol, (sysarg_t)sv));
#endif
}

int bind(int fd, const void *addr, int addrlen) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    (void)addr;
    (void)addrlen;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_BIND, fd, (sysarg_t)addr, addrlen));
#endif
}

int getsockname(int sockfd, void *addr, int *addrlen) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)addr; (void)addrlen;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_GETSOCKNAME, sockfd, (sysarg_t)addr, (sysarg_t)addrlen));
#endif
}

int getpeername(int sockfd, void *addr, int *addrlen) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)addr; (void)addrlen;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_GETPEERNAME, sockfd, (sysarg_t)addr, (sysarg_t)addrlen));
#endif
}

ssize_t sendmsg(int sockfd, const void *msg, int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)msg; (void)flags;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_SENDMSG, sockfd, (sysarg_t)msg, flags));
#endif
}

ssize_t recvmsg(int sockfd, void *msg, int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)msg; (void)flags;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_RECVMSG, sockfd, (sysarg_t)msg, flags));
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
    return handle_forest_result(syscall6(SYS_SENDTO, fd, (sysarg_t)buf, (sysarg_t)len, flags,
                    (sysarg_t)addr, addrlen));
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
    return handle_forest_result(syscall6(SYS_RECVFROM, fd, (sysarg_t)buf, (sysarg_t)len, flags,
                    (sysarg_t)addr, (sysarg_t)addrlen));
#endif
}

int netinfo(net_socket_info_t* buffer, int max_entries) {
#ifdef FOREST_USE_HOST_LIBC
    (void)buffer;
    (void)max_entries;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_NETINFO, (sysarg_t)buffer, max_entries));
#endif
}

int poweroff(void) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_POWER, POWER_ACTION_SHUTDOWN));
#endif
}

int reboot(int howto) {
#ifdef FOREST_USE_HOST_LIBC
    (void)howto;
    return handle_linux_stub();
#else
    (void)howto;
    return handle_forest_result(syscall1(SYS_POWER, POWER_ACTION_REBOOT));
#endif
}

int user_syscall(int op, const char* user, const char* pass, const char* aux,
                 void* out, int max_entries) {
#ifdef FOREST_USE_HOST_LIBC
    (void)op; (void)user; (void)pass; (void)aux; (void)out; (void)max_entries;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall6(SYS_USERCTL,
                                         op,
                                         (sysarg_t)user,
                                         (sysarg_t)pass,
                                         (sysarg_t)aux,
                                         (sysarg_t)out,
                                         max_entries));
#endif
}

// Additional POSIX function implementations for Forest OS userspace
int stat(const char *path, struct stat *buf) {
#ifdef FOREST_USE_HOST_LIBC
    (void)path;
    (void)buf;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_STAT, (sysarg_t)path, (sysarg_t)buf));
#endif
}

int fstat(int fd, struct stat *buf) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    (void)buf;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_FSTAT, fd, (sysarg_t)buf));
#endif
}

int access(const char *path, int mode) {
#ifdef FOREST_USE_HOST_LIBC
    (void)path;
    (void)mode;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_ACCESS, (sysarg_t)path, mode));
#endif
}

int getuid(void) {
#ifdef FOREST_USE_HOST_LIBC
    return 0;
#else
    return handle_forest_result(syscall0(SYS_GETUID));
#endif
}

int getgid(void) {
#ifdef FOREST_USE_HOST_LIBC
    return 0;
#else
    return handle_forest_result(syscall0(SYS_GETGID));
#endif
}

int geteuid(void) {
#ifdef FOREST_USE_HOST_LIBC
    return 0;
#else
    return handle_forest_result(syscall0(SYS_GETEUID));
#endif
}

int getegid(void) {
#ifdef FOREST_USE_HOST_LIBC
    return 0;
#else
    return handle_forest_result(syscall0(SYS_GETEGID));
#endif
}

int getppid(void) {
#ifdef FOREST_USE_HOST_LIBC
    return 0;
#else
    return handle_forest_result(syscall0(SYS_GETPPID));
#endif
}



int ioctl(int fd, unsigned long request, ...) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    (void)request;
    return handle_linux_stub();
#else
    // For simplicity, assume one argument
    va_list args;
    va_start(args, request);
    sysarg_t arg = (sysarg_t)va_arg(args, unsigned long);
    va_end(args);
    return handle_forest_result(syscall3(SYS_IOCTL, fd, request, arg));
#endif
}

int fcntl(int fd, int cmd, ...) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    (void)cmd;
    return handle_linux_stub();
#else
    // For simplicity, assume one argument
    va_list args;
    va_start(args, cmd);
    sysarg_t arg = (sysarg_t)va_arg(args, unsigned long);
    va_end(args);
    return handle_forest_result(syscall3(SYS_FCNTL, fd, cmd, arg));
#endif
}

int dup(int fd) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_DUP, fd));
#endif
}

int dup2(int oldfd, int newfd) {
#ifdef FOREST_USE_HOST_LIBC
    (void)oldfd;
    (void)newfd;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_DUP2, oldfd, newfd));
#endif
}

int posix_openpt(int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)flags;
    return handle_linux_stub();
#else
    return open("/dev/ptmx", flags);
#endif
}

int grantpt(int fd) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    return handle_linux_stub();
#else
    // Kernel currently does not enforce ownership checks; consider granted.
    (void)fd;
    errno = 0;
    return 0;
#endif
}

int unlockpt(int fd) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    return handle_linux_stub();
#else
    int unlock = 0;
    return ioctl(fd, 0x40045431UL, &unlock);
#endif
}

char *ptsname(int fd) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    errno = ENOSYS;
    return NULL;
#else
    static char path_buf[32];
    int pty_num = -1;
    if (ioctl(fd, 0x80045430UL, &pty_num) < 0) {
        return NULL;
    }
    (void)snprintf(path_buf, sizeof(path_buf), "/dev/pts/%d", pty_num);
    errno = 0;
    return path_buf;
#endif
}

int isatty(int fd) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    return 0;
#else
    unsigned char termios_blob[60];
    if (ioctl(fd, 0x5401UL, termios_blob) == 0) {
        errno = 0;
        return 1;
    }
    errno = ENOTTY;
    return 0;
#endif
}

int tcgetattr(int fd, void *termios_p) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd; (void)termios_p;
    return handle_linux_stub();
#else
    if (!termios_p) {
        return assign_errno_and_fail(EINVAL);
    }
    return ioctl(fd, 0x5401UL, termios_p);
#endif
}

int tcsetattr(int fd, int optional_actions, const void *termios_p) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd; (void)optional_actions; (void)termios_p;
    return handle_linux_stub();
#else
    (void)optional_actions;
    if (!termios_p) {
        return assign_errno_and_fail(EINVAL);
    }
    return ioctl(fd, 0x5402UL, (unsigned long)termios_p);
#endif
}

// Memory management syscalls
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
#ifdef FOREST_USE_HOST_LIBC
    (void)addr; (void)length; (void)prot; (void)flags; (void)fd; (void)offset;
    errno = ENOSYS;
    return (void*)-1;
#else
    sysret_t result = syscall6(SYS_MMAP, (sysarg_t)addr, (sysarg_t)length, 
                              prot, flags, (sysarg_t)fd, (sysarg_t)offset);
    if (result < 0) {
        assign_errno_and_fail((int)(-result));
        return (void*)-1;
    }
    return (void*)result;
#endif
}

int munmap(void *addr, size_t length) {
#ifdef FOREST_USE_HOST_LIBC
    (void)addr; (void)length;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_MUNMAP, (sysarg_t)addr, (sysarg_t)length));
#endif
}

int mprotect(void *addr, size_t len, int prot) {
#ifdef FOREST_USE_HOST_LIBC
    (void)addr; (void)len; (void)prot;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_MPROTECT, (sysarg_t)addr, (sysarg_t)len, prot));
#endif
}

// Process management syscalls
int fork(void) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::fork());
#else
    return handle_forest_result(syscall0(SYS_FORK));
#endif
}

int spawn(const char *path, int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)path;
    (void)flags;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_SPAWN_TASK, (sysarg_t)path, (sysarg_t)flags));
#endif
}

int execve(const char *pathname, char *const argv[], char *const envp[]) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pathname; (void)argv; (void)envp;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_EXECVE, (sysarg_t)pathname, 
                                        (sysarg_t)argv, (sysarg_t)envp));
#endif
}

int waitpid(int pid, int *status, int options) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::waitpid(pid, status, options));
#else
    return handle_forest_result(syscall4(SYS_WAIT4, (sysarg_t)pid, (sysarg_t)status, (sysarg_t)options, 0));
#endif
}

int kill(int pid, int sig) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::kill(pid, sig));
#else
    // Only works on current task in Forest OS
    if (pid != getpid()) {
        return assign_errno_and_fail(ESRCH);
    }
    return handle_forest_result(syscall2(SYS_KILL, pid, sig));
#endif
}

int shmget(int key, size_t size, int shmflg) {
#ifdef FOREST_USE_HOST_LIBC
    (void)key; (void)size; (void)shmflg;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_SHMGET, (sysarg_t)key, (sysarg_t)size, (sysarg_t)shmflg));
#endif
}

void *shmat(int shmid, const void *shmaddr, int shmflg) {
#ifdef FOREST_USE_HOST_LIBC
    (void)shmid; (void)shmaddr; (void)shmflg;
    errno = ENOSYS;
    return (void*)-1;
#else
    sysret_t result = syscall3(SYS_SHMAT, (sysarg_t)shmid, (sysarg_t)shmaddr, (sysarg_t)shmflg);
    if (result < 0) {
        assign_errno_and_fail((int)(-result));
        return (void*)-1;
    }
    return (void*)result;
#endif
}

int shmctl(int shmid, int cmd, void *buf) {
#ifdef FOREST_USE_HOST_LIBC
    (void)shmid; (void)cmd; (void)buf;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_SHMCTL, (sysarg_t)shmid, (sysarg_t)cmd, (sysarg_t)buf));
#endif
}

int shmdt(const void *shmaddr) {
#ifdef FOREST_USE_HOST_LIBC
    (void)shmaddr;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_SHMDT, (sysarg_t)shmaddr));
#endif
}

// Directory operations
int chdir(const char *path) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::chdir(path));
#else
    return handle_forest_result(syscall1(SYS_CHDIR, (sysarg_t)path));
#endif
}

char *getcwd(char *buf, size_t size) {
#ifdef FOREST_USE_HOST_LIBC
    return ::getcwd(buf, size);
#else
    sysret_t result;
    if (!buf || size == 0) {
        assign_errno_and_fail(EINVAL);
        return NULL;
    }
    result = syscall2(SYS_GETCWD, (sysarg_t)buf, (sysarg_t)size);
    if (result < 0) {
        assign_errno_and_fail((int)(-result));
        return NULL;
    }
    errno = 0;
    return (char*)result;
#endif
}

int mkdir(const char *pathname, int mode) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pathname; (void)mode;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_MKDIR, (sysarg_t)pathname, mode));
#endif
}

int rmdir(const char *pathname) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pathname;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_RMDIR, (sysarg_t)pathname));
#endif
}

// File operations
int creat(const char *pathname, int mode) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pathname; (void)mode;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_CREAT, (sysarg_t)pathname, mode));
#endif
}

int link(const char *oldpath, const char *newpath) {
#ifdef FOREST_USE_HOST_LIBC
    (void)oldpath; (void)newpath;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_LINK, (sysarg_t)oldpath, (sysarg_t)newpath));
#endif
}

int symlink(const char *target, const char *linkpath) {
#ifdef FOREST_USE_HOST_LIBC
    (void)target; (void)linkpath;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_SYMLINK, (sysarg_t)target, (sysarg_t)linkpath));
#endif
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pathname; (void)buf; (void)bufsiz;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_READLINK, (sysarg_t)pathname, (sysarg_t)buf, (sysarg_t)bufsiz));
#endif
}

int rename(const char *oldpath, const char *newpath) {
#ifdef FOREST_USE_HOST_LIBC
    (void)oldpath; (void)newpath;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_RENAME, (sysarg_t)oldpath, (sysarg_t)newpath));
#endif
}

// File permission operations
int chmod(const char *pathname, int mode) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pathname; (void)mode;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_CHMOD, (sysarg_t)pathname, mode));
#endif
}

int fchmod(int fd, int mode) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd; (void)mode;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_FCHMOD, fd, mode));
#endif
}

int chown(const char *pathname, int owner, int group) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pathname; (void)owner; (void)group;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_CHOWN, (sysarg_t)pathname, owner, group));
#endif
}

int fchown(int fd, int owner, int group) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd; (void)owner; (void)group;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_FCHOWN, fd, owner, group));
#endif
}

mode_t umask(mode_t mask) {
#ifdef FOREST_USE_HOST_LIBC
    return ::umask(mask);
#else
    sysret_t result = syscall1(SYS_UMASK, mask);
    if (result < 0) {
        assign_errno_and_fail((int)(-result));
        return (mode_t)-1;
    }
    errno = 0;
    return (mode_t)result;
#endif
}

// File synchronization
int fsync(int fd) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_FSYNC, fd));
#endif
}

int fdatasync(int fd) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_FDATASYNC, fd));
#endif
}

int ftruncate(int fd, off_t length) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd; (void)length;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_FTRUNCATE, fd, (sysarg_t)length));
#endif
}

int truncate(const char *path, off_t length) {
#ifdef FOREST_USE_HOST_LIBC
    (void)path; (void)length;
    return handle_linux_stub();
#else
    (void)path; (void)length;
    return assign_errno_and_fail(ENOSYS);
#endif
}

// Directory reading (not implemented)
int getdents(int fd, void *dirp, unsigned int count) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd; (void)dirp; (void)count;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_GETDENTS, fd, (sysarg_t)dirp, count));
#endif
}

// Pipe operations
int pipe(int pipefd[2]) {
#ifdef FOREST_USE_HOST_LIBC
    (void)pipefd;
    return handle_linux_stub();
#else
    sysret_t result = syscall1(SYS_PIPE, (sysarg_t)pipefd);
    return handle_forest_result(result);
#endif
}

// Scheduling
int sched_yield(void) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::sched_yield());
#else
    return handle_forest_result(syscall0(SYS_SCHED_YIELD));
#endif
}

// Advanced time operations
int gettimeofday(struct timeval *tv, struct timezone *tz) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_result(::gettimeofday(tv, tz));
#else
    (void)tz; // Forest OS doesn't use timezone
    return handle_forest_result(syscall2(SYS_GETTIMEOFDAY, (sysarg_t)tv, 0));
#endif
}

int settimeofday(const struct timeval *tv, const struct timezone *tz) {
#ifdef FOREST_USE_HOST_LIBC
    (void)tv; (void)tz;
    return handle_linux_stub();
#else
    (void)tv; (void)tz;
    return assign_errno_and_fail(ENOSYS);
#endif
}

// Signal handling - moved to signal.c to avoid duplicate definitions
// int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
// #ifdef FOREST_USE_HOST_LIBC
//     (void)signum; (void)act; (void)oldact;
//     return handle_linux_stub();
// #else
//     (void)signum; (void)act; (void)oldact;
//     return assign_errno_and_fail(ENOSYS);
// #endif
// }
//
// sighandler_t signal(int signum, sighandler_t handler) {
// #ifdef FOREST_USE_HOST_LIBC
//     (void)signum; (void)handler;
//     return SIG_ERR;
// #else
//     (void)signum; (void)handler;
//     assign_errno_and_fail(ENOSYS);
//     return SIG_ERR;
// #endif
// }
//
// int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
// #ifdef FOREST_USE_HOST_LIBC
//     (void)how; (void)set; (void)oldset;
//     return handle_linux_stub();
// #else
//     (void)how; (void)set; (void)oldset;
//     return assign_errno_and_fail(ENOSYS);
// #endif
// }
//
// int pause(void) {
// #ifdef FOREST_USE_HOST_LIBC
//     return handle_linux_stub();
// #else
//     assign_errno_and_fail(ENOSYS);
//     return -1;
// #endif
// }

// I/O multiplexing (not implemented)
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
#ifdef FOREST_USE_HOST_LIBC
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds; (void)timeout;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall5(SYS_SELECT,
                                        (sysarg_t)nfds,
                                        (sysarg_t)readfds,
                                        (sysarg_t)writefds,
                                        (sysarg_t)exceptfds,
                                        (sysarg_t)timeout));
#endif
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fds; (void)nfds; (void)timeout;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_POLL,
                                        (sysarg_t)fds,
                                        (sysarg_t)nfds,
                                        (sysarg_t)timeout));
#endif
}

// Vector I/O (not implemented)
ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd; (void)iov; (void)iovcnt;
    return handle_linux_stub();
#else
    (void)fd; (void)iov; (void)iovcnt;
    return assign_errno_and_fail(ENOSYS);
#endif
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd; (void)iov; (void)iovcnt;
    return handle_linux_stub();
#else
    (void)fd; (void)iov; (void)iovcnt;
    return assign_errno_and_fail(ENOSYS);
#endif
}

// Positional I/O (not implemented)
off_t pread(int fd, void *buf, size_t count, off_t offset) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd; (void)buf; (void)count; (void)offset;
    return handle_linux_stub();
#else
    (void)fd; (void)buf; (void)count; (void)offset;
    return assign_errno_and_fail(ENOSYS);
#endif
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
#ifdef FOREST_USE_HOST_LIBC
    (void)fd; (void)buf; (void)count; (void)offset;
    return handle_linux_stub();
#else
    (void)fd; (void)buf; (void)count; (void)offset;
    return assign_errno_and_fail(ENOSYS);
#endif
}

// Forest OS specific framebuffer operations
void *mmap_fb(size_t *width, size_t *height, size_t *pitch) {
#ifdef FOREST_USE_HOST_LIBC
    (void)width; (void)height; (void)pitch;
    errno = ENOSYS;
    return NULL;
#else
    // First get framebuffer info
    struct fb_info info;
    sysret_t result = syscall1(SYS_GET_FB_INFO, (sysarg_t)&info);
    if (result < 0) {
        assign_errno_and_fail((int)(-result));
        return NULL;
    }
    
    // Map framebuffer
    void *fb = (void*)syscall0(SYS_MMAP_FB);
    if ((sysret_t)fb < 0) {
        assign_errno_and_fail((int)(-(sysret_t)fb));
        return NULL;
    }
    
    if (width) *width = info.width;
    if (height) *height = info.height;
    if (pitch) *pitch = info.pitch;
    
    errno = 0;
    return fb;
#endif
}

int munmap_fb(void *addr) {
#ifdef FOREST_USE_HOST_LIBC
    (void)addr;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_MUNMAP_FB, (sysarg_t)addr));
#endif
}

int get_fb_info(struct fb_info *info) {
#ifdef FOREST_USE_HOST_LIBC
    (void)info;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_GET_FB_INFO, (sysarg_t)info));
#endif
}

// ============================================================================
// Framebuffer Watcher Functions
// ============================================================================

int start_fb_watcher(void) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_stub();
#else
    return handle_forest_result(syscall0(SYS_START_FB_WATCHER));
#endif
}

int stop_fb_watcher(void) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_stub();
#else
    return handle_forest_result(syscall0(SYS_STOP_FB_WATCHER));
#endif
}

int fb_flush(void) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_stub();
#else
    return handle_forest_result(syscall0(SYS_FB_FLUSH));
#endif
}

// ============================================================================
// Input Event Functions (Direct syscall-based input)
// ============================================================================

// Input event structure (16 bytes, evdev compatible)
typedef struct {
    uint32 tv_sec;
    uint32 tv_usec;
    uint16 type;
    uint16 code;
    int32 value;
} __attribute__((packed)) input_event_t;

ssize_t read_kbd_event(void *event) {
#ifdef FOREST_USE_HOST_LIBC
    (void)event;
    return handle_linux_stub();
#else
    return syscall1(SYS_READ_KBD_EVENT, (sysarg_t)event);
#endif
}

ssize_t read_mouse_event(void *event) {
#ifdef FOREST_USE_HOST_LIBC
    (void)event;
    return handle_linux_stub();
#else
    return syscall1(SYS_READ_MOUSE_EVENT, (sysarg_t)event);
#endif
}

int poll_input(void) {
#ifdef FOREST_USE_HOST_LIBC
    return 0;
#else
    return (int)syscall0(SYS_POLL_INPUT);
#endif
}

// ============================================================================
// Additional Network Syscalls
// ============================================================================

int connect(int sockfd, const void *addr, int addrlen) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)addr; (void)addrlen;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_CONNECT, sockfd, (sysarg_t)addr, addrlen));
#endif
}

int shutdown(int sockfd, int how) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)how;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_SHUTDOWN, sockfd, how));
#endif
}

int listen(int sockfd, int backlog) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)backlog;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_LISTEN, sockfd, backlog));
#endif
}

int accept(int sockfd, void *addr, int *addrlen) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)addr; (void)addrlen;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_ACCEPT, sockfd, (sysarg_t)addr, (sysarg_t)addrlen));
#endif
}

int shutdown_socket(int sockfd, int how) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)how;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_SHUTDOWN, sockfd, how));
#endif
}

int openat(int dirfd, const char *pathname, int flags, int mode) {
#ifdef FOREST_USE_HOST_LIBC
    (void)dirfd; (void)pathname; (void)flags; (void)mode;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall4(SYS_OPENAT, dirfd, (sysarg_t)pathname, flags, mode));
#endif
}

int mkdirat(int dirfd, const char *pathname, int mode) {
#ifdef FOREST_USE_HOST_LIBC
    (void)dirfd; (void)pathname; (void)mode;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_MKDIRAT, dirfd, (sysarg_t)pathname, mode));
#endif
}

int unlinkat(int dirfd, const char *pathname, int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)dirfd; (void)pathname; (void)flags;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_UNLINKAT, dirfd, (sysarg_t)pathname, flags));
#endif
}

int fstatat(int dirfd, const char *pathname, void *buf, int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)dirfd; (void)pathname; (void)buf; (void)flags;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall4(SYS_NEWFSTATAT, dirfd, (sysarg_t)pathname, (sysarg_t)buf, flags));
#endif
}

int faccessat(int dirfd, const char *pathname, int mode, int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)dirfd; (void)pathname; (void)mode; (void)flags;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall4(SYS_FACCESSAT, dirfd, (sysarg_t)pathname, mode, flags));
#endif
}

int clone(int flags, void *stack, void *ptid, void *ctid, unsigned long tls) {
#ifdef FOREST_USE_HOST_LIBC
    (void)flags; (void)stack; (void)ptid; (void)ctid; (void)tls;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall5(SYS_CLONE, flags, (sysarg_t)stack, (sysarg_t)ptid, (sysarg_t)ctid, (sysarg_t)tls));
#endif
}

int futex(int *uaddr, int op, int val, const void *timeout, int *uaddr2, int val3) {
#ifdef FOREST_USE_HOST_LIBC
    (void)uaddr; (void)op; (void)val; (void)timeout; (void)uaddr2; (void)val3;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall6(SYS_FUTEX, (sysarg_t)uaddr, op, val, (sysarg_t)timeout, (sysarg_t)uaddr2, val3));
#endif
}

int setsockopt(int sockfd, int level, int optname, const void *optval, int optlen) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)level; (void)optname; (void)optval; (void)optlen;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall5(SYS_SETSOCKOPT, sockfd, level, optname, (sysarg_t)optval, optlen));
#endif
}

int getsockopt(int sockfd, int level, int optname, void *optval, int *optlen) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)level; (void)optname; (void)optval; (void)optlen;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall5(SYS_GETSOCKOPT, sockfd, level, optname, (sysarg_t)optval, (sysarg_t)optlen));
#endif
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)buf; (void)len; (void)flags;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall6(SYS_SENDTO, sockfd, (sysarg_t)buf, (sysarg_t)len, flags, 0, 0));
#endif
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)sockfd; (void)buf; (void)len; (void)flags;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall6(SYS_RECVFROM, sockfd, (sysarg_t)buf, (sysarg_t)len, flags, 0, 0));
#endif
}

// ============================================================================
// Additional Process Syscalls
// ============================================================================

int setuid(int uid) {
#ifdef FOREST_USE_HOST_LIBC
    (void)uid;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_SETUID, uid));
#endif
}

int setgid(int gid) {
#ifdef FOREST_USE_HOST_LIBC
    (void)gid;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_SETGID, gid));
#endif
}

int seteuid(int euid) {
#ifdef FOREST_USE_HOST_LIBC
    (void)euid;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_SETEUID, euid));
#endif
}

int setegid(int egid) {
#ifdef FOREST_USE_HOST_LIBC
    (void)egid;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_SETEGID, egid));
#endif
}

int gettid(void) {
#ifdef FOREST_USE_HOST_LIBC
    return 1;
#else
    return handle_forest_result(syscall0(SYS_GETTID));
#endif
}

// ============================================================================
// Clock/Time Syscalls
// ============================================================================

int clock_gettime(int clk_id, struct timespec *tp) {
#ifdef FOREST_USE_HOST_LIBC
    (void)clk_id; (void)tp;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_CLOCK_GETTIME, clk_id, (sysarg_t)tp));
#endif
}

int clock_settime(int clk_id, const struct timespec *tp) {
#ifdef FOREST_USE_HOST_LIBC
    (void)clk_id; (void)tp;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_CLOCK_SETTIME, clk_id, (sysarg_t)tp));
#endif
}

// ============================================================================
// Miscellaneous Syscalls
// ============================================================================

int sync(void) {
#ifdef FOREST_USE_HOST_LIBC
    return 0;
#else
    return handle_forest_result(syscall0(SYS_SYNC));
#endif
}

int mount(const char *source, const char *target, const char *fstype, 
          unsigned long flags, const void *data) {
#ifdef FOREST_USE_HOST_LIBC
    (void)source; (void)target; (void)fstype; (void)flags; (void)data;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall5(SYS_MOUNT, (sysarg_t)source, (sysarg_t)target,
                                        (sysarg_t)fstype, flags, (sysarg_t)data));
#endif
}

int umount(const char *target) {
#ifdef FOREST_USE_HOST_LIBC
    (void)target;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_UMOUNT2, (sysarg_t)target, 0));
#endif
}

int sethostname(const char *name, size_t len) {
#ifdef FOREST_USE_HOST_LIBC
    (void)name; (void)len;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_SETHOSTNAME, (sysarg_t)name, len));
#endif
}

int getrandom(void *buf, size_t buflen, unsigned int flags) {
#ifdef FOREST_USE_HOST_LIBC
    (void)buf; (void)buflen; (void)flags;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_GETRANDOM, (sysarg_t)buf, buflen, flags));
#endif
}

// ============================================================================
// Sound/Audio Syscalls (Phloem API support)
// ============================================================================

// Sound format structure for kernel communication
typedef struct {
    uint32 sample_rate;
    uint16 channels;
    uint16 bits_per_sample;
    int    signed_samples;
} sound_format_t;

int sound_play(const void *data, size_t length, const sound_format_t *format) {
#ifdef FOREST_USE_HOST_LIBC
    (void)data; (void)length; (void)format;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall3(SYS_SOUND_PLAY, (sysarg_t)data, length, (sysarg_t)format));
#endif
}

int sound_stop(void) {
#ifdef FOREST_USE_HOST_LIBC
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_SOUND_STOP, 0));
#endif
}

int sound_beep(uint32 frequency, uint32 duration_ms) {
#ifdef FOREST_USE_HOST_LIBC
    (void)frequency; (void)duration_ms;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall2(SYS_SOUND_BEEP, frequency, duration_ms));
#endif
}

int sound_set_volume(uint8 volume) {
#ifdef FOREST_USE_HOST_LIBC
    (void)volume;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_SOUND_SET_VOLUME, volume));
#endif
}

int sound_get_volume(void) {
#ifdef FOREST_USE_HOST_LIBC
    return 0;
#else
    return (int)syscall1(SYS_SOUND_GET_VOLUME, 0);
#endif
}

int sound_get_info(void *info) {
#ifdef FOREST_USE_HOST_LIBC
    (void)info;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_SOUND_GET_INFO, (sysarg_t)info));
#endif
}

int sound_get_caps(void *caps) {
#ifdef FOREST_USE_HOST_LIBC
    (void)caps;
    return handle_linux_stub();
#else
    return handle_forest_result(syscall1(SYS_SOUND_GET_CAPS, (sysarg_t)caps));
#endif
}
