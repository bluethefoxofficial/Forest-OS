#ifndef LIBC_UNISTD_H
#define LIBC_UNISTD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "../../../libs/forestcore/include/types.h"
#include "net.h"
#include "time.h"
#include "sys/utsname.h"

#ifndef ARCH_64BIT
#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_64BIT 1
#else
#define ARCH_64BIT 0
#endif
#endif

#if ARCH_64BIT
typedef long ssize_t;
typedef long useconds_t;
#else
typedef int32 ssize_t;
typedef uint32 useconds_t;
#endif

ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
int open(const char *pathname, int flags);
int close(int fd);
int lseek(int fd, int offset, int whence);
int getpid(void);
int unlink(const char *pathname);
int nanosleep(const struct timespec *req, struct timespec *rem);
int uname(struct utsname *uts_buffer);
int brk(void *addr);
int time(int *tloc);
int _exit(int status);
int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int sv[2]);
int bind(int fd, const void *addr, int addrlen);
int connect(int sockfd, const void *addr, int addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, void *addr, int *addrlen);
int shutdown(int sockfd, int how);
int getsockname(int sockfd, void *addr, int *addrlen);
int getpeername(int sockfd, void *addr, int *addrlen);
ssize_t sendmsg(int sockfd, const void *msg, int flags);
ssize_t recvmsg(int sockfd, void *msg, int flags);
ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const void *addr, int addrlen);
ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 void *addr, int *addrlen);
int netinfo(net_socket_info_t* buffer, int max_entries);
int poweroff(void);
int reboot(int howto);
int user_syscall(int op, const char* user, const char* pass, const char* aux,
                 void* out, int max_entries);
int posix_openpt(int flags);
int grantpt(int fd);
int unlockpt(int fd);
char *ptsname(int fd);
int isatty(int fd);
int tcgetattr(int fd, void *termios_p);
int tcsetattr(int fd, int optional_actions, const void *termios_p);
int shmget(int key, size_t size, int shmflg);
void *shmat(int shmid, const void *shmaddr, int shmflg);
int shmctl(int shmid, int cmd, void *buf);
int shmdt(const void *shmaddr);
int openat(int dirfd, const char *pathname, int flags, int mode);
int mkdirat(int dirfd, const char *pathname, int mode);
int unlinkat(int dirfd, const char *pathname, int flags);
int fstatat(int dirfd, const char *pathname, void *buf, int flags);
int faccessat(int dirfd, const char *pathname, int mode, int flags);
int clone(int flags, void *stack, void *ptid, void *ctid, unsigned long tls);
int futex(int *uaddr, int op, int val, const void *timeout, int *uaddr2, int val3);

#ifdef __cplusplus
}
#endif

#endif
