#ifndef LIBC_UCHAR_H
#define LIBC_UCHAR_H

#define __STDC_VERSION_UCHAR_H__ 202311L

typedef unsigned short char16_t;
typedef unsigned int char32_t;

typedef struct {
    unsigned short utf16;
} mbstate_t;

#endif
