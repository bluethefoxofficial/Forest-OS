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
static inline float floorf(float x) { return __builtin_floorf(x); }
static inline float ceilf(float x) { return __builtin_ceilf(x); }
static inline float powf(float x, float y) { return __builtin_powf(x, y); }

static inline double log(double x) { return __builtin_log(x); }
static inline float logf(float x) { return __builtin_logf(x); }
static inline double log10(double x) { return __builtin_log10(x); }
static inline float log10f(float x) { return __builtin_log10f(x); }
static inline double exp(double x) { return __builtin_exp(x); }
static inline float expf(float x) { return __builtin_expf(x); }

static inline double ldexp(double x, int exp) {
    return x * __builtin_pow(2.0, (double)exp);
}
static inline float ldexpf(float x, int exp) {
    return x * __builtin_powf(2.0f, (float)exp);
}

static inline double frexp(double x, int* exp) {
    if (x == 0.0) { *exp = 0; return 0.0; }
    int e = 0;
    double abs_x = fabs(x);
    while (abs_x >= 1.0) { abs_x /= 2.0; e++; }
    while (abs_x < 0.5) { abs_x *= 2.0; e--; }
    *exp = e;
    return (x < 0) ? -abs_x : abs_x;
}

static inline double atan(double x) { return __builtin_atan(x); }
static inline double atan2(double y, double x) { return __builtin_atan2(y, x); }
static inline double acos(double x) { return __builtin_acos(x); }
static inline double asin(double x) { return __builtin_asin(x); }

#endif
