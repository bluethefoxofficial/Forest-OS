#include <stddef.h>
#include "../../src/include/libc/unistd.h"
#include "../../src/include/types.h"
#include "../../src/include/syscall.h"

typedef int32 ssize_t;

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

ssize_t write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (int32)buf, (int32)count);
}

ssize_t read(int fd, void *buf, size_t count) {
    return syscall3(SYS_READ, fd, (int32)buf, (int32)count);
}

int open(const char *pathname, int flags) {
    return syscall3(SYS_OPEN, (int32)pathname, flags, 0);
}

int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

int lseek(int fd, int offset, int whence) {
    return syscall3(SYS_LSEEK, fd, offset, whence);
}

int getpid(void) {
    return syscall0(SYS_GETPID);
}

int time(int *tloc) {
    int value = syscall1(SYS_TIME, (int32)tloc);
    if (tloc) {
        *tloc = value;
    }
    return value;
}

int nanosleep(const void *req, void *rem) {
    return syscall2(SYS_NANOSLEEP, (int32)req, (int32)rem);
}

int uname(void *uts_buffer) {
    return syscall1(SYS_UNAME, (int32)uts_buffer);
}

int brk(void *addr) {
    return syscall1(SYS_BRK, (int32)addr);
}

int _exit(int status) {
    return syscall1(SYS_EXIT, status);
}

int socket(int domain, int type, int protocol) {
    return syscall3(SYS_SOCKET, domain, type, protocol);
}

int bind(int fd, const void *addr, int addrlen) {
    return syscall3(SYS_BIND, fd, (int32)addr, addrlen);
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const void *addr, int addrlen) {
    return syscall6(SYS_SENDTO, fd, (int32)buf, (int32)len, flags,
                    (int32)addr, addrlen);
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 void *addr, int *addrlen) {
    return syscall6(SYS_RECVFROM, fd, (int32)buf, (int32)len, flags,
                    (int32)addr, (int32)addrlen);
}
