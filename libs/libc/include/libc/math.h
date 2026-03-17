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

static inline double sin(double x) {
    // Reduce x to [-pi, pi] range
    const double PI = 3.141592653589793;
    const double TWO_PI = 6.283185307179586;
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;

    // Taylor series approximation: sin(x) ≈ x - x^3/6 + x^5/120
    double x2 = x * x;
    double x3 = x2 * x;
    double x5 = x3 * x2;
    return x - x3 / 6.0 + x5 / 120.0;
}

static inline double cos(double x) {
    // Reduce x to [-pi, pi] range
    const double PI = 3.141592653589793;
    const double TWO_PI = 6.283185307179586;
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;

    // Taylor series approximation: cos(x) ≈ 1 - x^2/2 + x^4/24
    double x2 = x * x;
    double x4 = x2 * x2;
    return 1.0 - x2 / 2.0 + x4 / 24.0;
}

static inline double tan(double x) { return sin(x) / cos(x); }

static inline double sqrt(double x) {
    if (x < 0.0) return __builtin_nan("");
    if (x == 0.0) return 0.0;

    // Newton-Raphson method for sqrt
    double guess = x / 2.0;
    for (int i = 0; i < 10; i++) {
        guess = (guess + x / guess) / 2.0;
    }
    return guess;
}

static inline double fabs(double x) { return x < 0.0 ? -x : x; }

static inline double floor(double x) {
    int ix = (int)x;
    return (x < 0.0 && x != (double)ix) ? ix - 1.0 : (double)ix;
}

static inline double ceil(double x) {
    int ix = (int)x;
    return (x > 0.0 && x != (double)ix) ? ix + 1.0 : (double)ix;
}

static inline double fmod(double x, double y) {
    // Safe implementation to prevent overflow and handle edge cases
    if (x != x || y != y) return __builtin_nan(""); // NaN check
    if (x == INFINITY || x == -INFINITY || y == 0.0) return __builtin_nan("");
    if (y == INFINITY || y == -INFINITY) return x;
    // Use truncating division
    double quotient = x / y;
    double truncated = __builtin_trunc(quotient);
    return x - truncated * y;
}

static inline float sinf(float x) {
    // Reduce x to [-pi, pi] range
    const float PI = 3.141592653589793f;
    const float TWO_PI = 6.283185307179586f;
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;

    // Taylor series approximation: sin(x) ≈ x - x^3/6 + x^5/120
    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    return x - x3 / 6.0f + x5 / 120.0f;
}
static inline float cosf(float x) {
    // Reduce x to [-pi, pi] range
    const float PI = 3.141592653589793f;
    const float TWO_PI = 6.283185307179586f;
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;

    // Taylor series approximation: cos(x) ≈ 1 - x^2/2 + x^4/24
    float x2 = x * x;
    float x4 = x2 * x2;
    return 1.0f - x2 / 2.0f + x4 / 24.0f;
}
static inline float sqrtf(float x) { return __builtin_sqrtf(x); }
static inline float fabsf(float x) { return x < 0.0f ? -x : x; }
static inline float floorf(float x) { return __builtin_floorf(x); }
static inline float ceilf(float x) { return __builtin_ceilf(x); }
static inline double pow(double x, double y) { return __builtin_pow(x, y); }
static inline float powf(float x, float y) { return __builtin_powf(x, y); }
static inline float fmodf(float x, float y) {
    // Safe implementation to prevent recursion and handle edge cases
    if (x != x || y != y) return __builtin_nanf(""); // NaN check
    if (x == INFINITY || x == -INFINITY || y == 0.0f) return __builtin_nanf("");
    if (y == INFINITY || y == -INFINITY) return x;
    // Use truncating division to avoid precision issues
    float quotient = x / y;
    float truncated = __builtin_truncf(quotient);
    return x - truncated * y;
}

static inline double log(double x) {
    if (x <= 0.0) return __builtin_nan("");
    // Taylor series for ln(1+x) around 1
    double y = (x - 1.0) / (x + 1.0);
    double y2 = y * y;
    double result = 0.0;
    double term = y;
    for (int n = 1; n < 20; n += 2) {
        result += term / n;
        term *= y2;
    }
    return 2.0 * result;
}
static inline float logf(float x) { return __builtin_logf(x); }
static inline double log10(double x) { return log(x) / log(10.0); }
static inline float log10f(float x) { return logf(x) / logf(10.0f); }
static inline double exp(double x) {
    // Taylor series: e^x ≈ 1 + x + x^2/2! + x^3/3! + ...
    double result = 1.0;
    double term = 1.0;
    for (int n = 1; n < 20; n++) {
        term *= x / n;
        result += term;
    }
    return result;
}
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

static inline double atan(double x) {
    // Taylor series: atan(x) ≈ x - x^3/3 + x^5/5 - x^7/7 + ...
    double result = x;
    double term = x;
    double x2 = x * x;
    int sign = -1;
    for (int n = 3; n < 20; n += 2) {
        term *= x2 * sign;
        result += term / n;
        sign = -sign;
    }
    return result;
}
static inline double atan2(double y, double x) {
    const double PI = 3.141592653589793;
    if (x > 0) return atan(y/x);
    if (x < 0 && y >= 0) return atan(y/x) + PI;
    if (x < 0 && y < 0) return atan(y/x) - PI;
    if (x == 0 && y > 0) return PI/2;
    if (x == 0 && y < 0) return -PI/2;
    return 0.0;
}
static inline double asin(double x);
static inline double acos(double x) {
    if (x < -1.0 || x > 1.0) return __builtin_nan("");
    return 1.5707963267948966 - asin(x);
}
static inline double asin(double x) {
    if (x < -1.0 || x > 1.0) return __builtin_nan("");
    // Taylor series: asin(x) ≈ x + x^3/6 + 3*x^5/40 + 5*x^7/112 + ...
    double result = x;
    double term = x;
    double x2 = x * x;
    for (int n = 3; n < 15; n += 2) {
        term *= x2 * (n-2.0) / n;
        result += term;
    }
    return result;
}

#endif
