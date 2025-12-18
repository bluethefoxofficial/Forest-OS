#ifndef LIBC_STDBIT_H
#define LIBC_STDBIT_H

#define __STDC_VERSION_STDBIT_H__ 202311L

static inline int stdc_count_ones_uchar(unsigned char value) { return __builtin_popcount((unsigned int)value); }
static inline int stdc_count_ones_uint(unsigned int value) { return __builtin_popcount(value); }

#endif
