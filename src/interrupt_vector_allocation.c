#include "interrupt_vector_allocation.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define VECTOR_POOL_SIZE 256
#define USER_VECTOR_START 32
#define USER_VECTOR_END 255

typedef struct {
    bool allocated[VECTOR_POOL_SIZE];
    char names[VECTOR_POOL_SIZE][32];
    vector_priority_t priorities[VECTOR_POOL_SIZE];
    uint32_t allocation_count;
    bool initialized;
} vector_allocation_context_t;

static vector_allocation_context_t vec_ctx = {0};

vector_alloc_error_t interrupt_vector_alloc_init(void) {
    memset(&vec_ctx, 0, sizeof(vec_ctx));
    
    // Mark system vectors as allocated
    for (uint8_t i = 0; i < USER_VECTOR_START; i++) {
        vec_ctx.allocated[i] = true;
    }
    
    vec_ctx.initialized = true;
    return VECTOR_ALLOC_SUCCESS;
}

vector_alloc_error_t interrupt_vector_allocate(vector_priority_t priority, 
                                              const char *name, uint8_t *vector) {
    if (!vec_ctx.initialized || !vector) return VECTOR_ALLOC_ERROR_INVALID_PARAMS;
    
    for (uint8_t i = USER_VECTOR_START; i <= USER_VECTOR_END; i++) {
        if (!vec_ctx.allocated[i]) {
            vec_ctx.allocated[i] = true;
            vec_ctx.priorities[i] = priority;
            if (name) {
                strncpy(vec_ctx.names[i], name, 31);
                vec_ctx.names[i][31] = '\0';
            }
            vec_ctx.allocation_count++;
            *vector = i;
            return VECTOR_ALLOC_SUCCESS;
        }
    }
    
    return VECTOR_ALLOC_ERROR_NO_VECTORS;
}

vector_alloc_error_t interrupt_vector_free(uint8_t vector) {
    if (!vec_ctx.initialized) return VECTOR_ALLOC_ERROR_NOT_INITIALIZED;
    if (vector < USER_VECTOR_START) return VECTOR_ALLOC_ERROR_SYSTEM_VECTOR;
    
    if (vec_ctx.allocated[vector]) {
        vec_ctx.allocated[vector] = false;
        memset(vec_ctx.names[vector], 0, 32);
        vec_ctx.allocation_count--;
        return VECTOR_ALLOC_SUCCESS;
    }
    
    return VECTOR_ALLOC_ERROR_NOT_ALLOCATED;
}
