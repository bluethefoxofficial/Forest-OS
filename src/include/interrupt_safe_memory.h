#ifndef INTERRUPT_SAFE_MEMORY_H
#define INTERRUPT_SAFE_MEMORY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    ISR_MEMORY_SUCCESS = 0,
    ISR_MEMORY_ERROR_INVALID_PARAMS,
    ISR_MEMORY_ERROR_NO_MEMORY,
    ISR_MEMORY_ERROR_NOT_INITIALIZED
} isr_memory_error_t;

isr_memory_error_t isr_memory_init(void);
void* isr_malloc(size_t size);
void isr_free(void *ptr);

#endif
