#ifndef LIBC_TIME_H
#define LIBC_TIME_H

#define __STDC_VERSION_TIME_H__ 202311L

#ifdef __cplusplus
extern "C" {
#endif

#include "../types.h"

#if defined(__linux__)
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
