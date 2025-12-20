#ifndef LIBC_TIME_H
#define LIBC_TIME_H

#define __STDC_VERSION_TIME_H__ 202311L

#ifdef __cplusplus
extern "C" {
#endif

#include "../types.h"

#ifdef FOREST_USE_HOST_LIBC
#include <time.h>
#else
struct timespec {
    uint32 tv_sec;
    uint32 tv_nsec;
};
#endif

#ifdef __cplusplus
}
#endif

#endif
