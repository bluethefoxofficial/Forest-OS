#ifndef LIBC_ASSERT_H
#define LIBC_ASSERT_H

#define __STDC_VERSION_ASSERT_H__ 202311L

#ifdef NDEBUG
#define assert(ignore) ((void)0)
#else
#define assert(expr) ((expr) ? (void)0 : __builtin_trap())
#endif

#endif
