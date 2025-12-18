#ifndef LIBC_UNISTD_H
#define LIBC_UNISTD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "../types.h"
#include "net.h"
#include "time.h"
#include "sys/utsname.h"

typedef int32 ssize_t;

ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
int open(const char *pathname, int flags);
int close(int fd);
int lseek(int fd, int offset, int whence);
int getpid(void);
int nanosleep(const struct timespec *req, struct timespec *rem);
int uname(struct utsname *uts_buffer);
int brk(void *addr);
int time(int *tloc);
int _exit(int status);
int socket(int domain, int type, int protocol);
int bind(int fd, const void *addr, int addrlen);
ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const void *addr, int addrlen);
ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 void *addr, int *addrlen);
int netinfo(net_socket_info_t* buffer, int max_entries);

#ifdef __cplusplus
}
#endif

#endif
