#ifndef LIBC_STDLIB_H
#define LIBC_STDLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "../types.h"

typedef struct {
    int quot;
    int rem;
} div_t;

// Memory management
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t num, size_t size);
void *realloc(void *ptr, size_t size);

// String conversion
int atoi(const char *str);
long atol(const char *str);
double atof(const char *str);
char *itoa(int value, char *str, int base);

// Process control
void exit(int status);
void abort(void);
int system(const char *command);

// Random number generation
int rand(void);
void srand(unsigned int seed);

// Searching and sorting
void *bsearch(const void *key, const void *base, size_t num, size_t size,
              int (*compare)(const void *, const void *));
void qsort(void *base, size_t num, size_t size,
           int (*compare)(const void *, const void *));

// Math
int abs(int n);
long labs(long n);
div_t div(int numer, int denom);

#ifdef __cplusplus
}
#endif

#endif
