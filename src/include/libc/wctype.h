#ifndef LIBC_WCTYPE_H
#define LIBC_WCTYPE_H

#include <libc/wchar.h>

typedef unsigned long wctype_t;

typedef struct {
    wctype_t type;
} wctrans_t;

static inline int iswalpha(wint_t wc) {
    return (wc >= L'A' && wc <= L'Z') || (wc >= L'a' && wc <= L'z');
}

#endif
