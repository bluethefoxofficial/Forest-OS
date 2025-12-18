#ifndef LIBC_STDINT_H
#define LIBC_STDINT_H

#define __STDC_VERSION_STDINT_H__ 202311L

#include "../types.h"

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int int16_t;
typedef unsigned short int uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef signed long long int int64_t;
typedef unsigned long long int uint64_t;

typedef int32_t intptr_t;
typedef uint32_t uintptr_t;
typedef int64_t intmax_t;
typedef uint64_t uintmax_t;

typedef uint32_t size_t;
typedef int32_t ptrdiff_t;

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  0xFF
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 0xFFFF
#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  2147483647
#define UINT32_MAX 0xFFFFFFFFU
#define INT64_MIN  (-9223372036854775807LL - 1)
#define INT64_MAX  9223372036854775807LL
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL

#endif
