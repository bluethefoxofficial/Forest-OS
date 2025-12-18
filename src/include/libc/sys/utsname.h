#ifndef LIBC_SYS_UTSNAME_H
#define LIBC_SYS_UTSNAME_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FOREST_USE_HOST_LIBC
#include <sys/utsname.h>
typedef struct utsname utsname_t;
#else
struct utsname {
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[32];
    char machine[32];
};

typedef struct utsname utsname_t;
#endif

#ifdef __cplusplus
}
#endif

#endif
