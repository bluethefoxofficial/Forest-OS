#ifndef LIBC_MATH_H
#define LIBC_MATH_H

#define __STDC_VERSION_MATH_H__ 202311L

#define NAN (__builtin_nanf(""))
#define INFINITY (__builtin_inff())

static inline double sin(double x) { return __builtin_sin(x); }
static inline double cos(double x) { return __builtin_cos(x); }
static inline double tan(double x) { return __builtin_tan(x); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline double pow(double x, double y) { return __builtin_pow(x, y); }
static inline double fabs(double x) { return __builtin_fabs(x); }

#endif
