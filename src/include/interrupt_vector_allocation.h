#ifndef INTERRUPT_VECTOR_ALLOCATION_H
#define INTERRUPT_VECTOR_ALLOCATION_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    VECTOR_ALLOC_SUCCESS = 0,
    VECTOR_ALLOC_ERROR_INVALID_PARAMS,
    VECTOR_ALLOC_ERROR_NOT_INITIALIZED,
    VECTOR_ALLOC_ERROR_NO_VECTORS,
    VECTOR_ALLOC_ERROR_SYSTEM_VECTOR,
    VECTOR_ALLOC_ERROR_NOT_ALLOCATED
} vector_alloc_error_t;

typedef enum {
    VECTOR_PRIORITY_LOW = 0,
    VECTOR_PRIORITY_NORMAL,
    VECTOR_PRIORITY_HIGH,
    VECTOR_PRIORITY_CRITICAL
} vector_priority_t;

vector_alloc_error_t interrupt_vector_alloc_init(void);
vector_alloc_error_t interrupt_vector_allocate(vector_priority_t priority, 
                                              const char *name, uint8_t *vector);
vector_alloc_error_t interrupt_vector_free(uint8_t vector);

#endif
