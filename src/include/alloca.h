#ifndef ALLOCA_H_WRAPPER
#define ALLOCA_H_WRAPPER

#include <stddef.h>

/* Map alloca to compiler builtin so no external symbol is required. */
#define alloca __builtin_alloca

#endif /* ALLOCA_H_WRAPPER */
