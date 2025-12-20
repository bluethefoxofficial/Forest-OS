#ifndef LIBC_STDDEF_H
#define LIBC_STDDEF_H

#define __STDC_VERSION_STDDEF_H__ 202311L

typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __SIZE_TYPE__ size_t;
typedef __WCHAR_TYPE__ wchar_t;

typedef long double max_align_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
