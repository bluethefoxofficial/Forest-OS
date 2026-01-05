#ifndef LIBC_MATH_H
#define LIBC_MATH_H

#define __STDC_VERSION_MATH_H__ 202311L

#define NAN (__builtin_nanf(""))
#define INFINITY (__builtin_inff())

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

#ifndef M_E
#define M_E 2.71828182845904523536
#endif

static inline double sin(double x) { return __builtin_sin(x); }
static inline double cos(double x) { return __builtin_cos(x); }
static inline double tan(double x) { return __builtin_tan(x); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline double pow(double x, double y) { return __builtin_pow(x, y); }
static inline double fabs(double x) { return __builtin_fabs(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline double ceil(double x) { return __builtin_ceil(x); }

static inline double fmod(double x, double y) {
    return x - (int)(x / y) * y;
}

static inline float sinf(float x) { return __builtin_sinf(x); }
static inline float cosf(float x) { return __builtin_cosf(x); }
static inline float sqrtf(float x) { return __builtin_sqrtf(x); }
static inline float fabsf(float x) { return __builtin_fabsf(x); }

#endif
