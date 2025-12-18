#ifndef LIBC_WCHAR_H
#define LIBC_WCHAR_H

#define __STDC_VERSION_WCHAR_H__ 202311L

#include <libc/stddef.h>

typedef struct {
    unsigned int count;
} mbstate_t;

typedef long wint_t;

typedef long wctrans_t;

typedef unsigned long wctype_t;

#endif
