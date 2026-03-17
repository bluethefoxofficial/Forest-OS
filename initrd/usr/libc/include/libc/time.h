#ifndef LIBC_TIME_H
#define LIBC_TIME_H

#define __STDC_VERSION_TIME_H__ 202311L

#ifdef __cplusplus
extern "C" {
#endif

#include "../../../libs/forestcore/include/types.h"

#ifdef FOREST_USE_HOST_LIBC
#include <time.h>
#else
#ifndef TIME_STRUCTURES_DEFINED
#define TIME_STRUCTURES_DEFINED
struct timeval {
    uint32 tv_sec;
    uint32 tv_usec;
};

struct timespec {
    uint32 tv_sec;
    uint32 tv_nsec;
};
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
