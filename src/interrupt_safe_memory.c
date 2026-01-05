#include "interrupt_safe_memory.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define ISR_POOL_SIZE (64 * 1024)
#define MAX_ISR_ALLOCATIONS 256

typedef struct {
    void *ptr;
    size_t size;
    bool in_use;
} isr_allocation_t;

static uint8_t isr_memory_pool[ISR_POOL_SIZE] __attribute__((aligned(16)));
static isr_allocation_t allocations[MAX_ISR_ALLOCATIONS];
static size_t pool_offset = 0;
static bool initialized = false;

isr_memory_error_t isr_memory_init(void) {
    memset(isr_memory_pool, 0, ISR_POOL_SIZE);
    memset(allocations, 0, sizeof(allocations));
    pool_offset = 0;
    initialized = true;
    return ISR_MEMORY_SUCCESS;
}

void* isr_malloc(size_t size) {
    if (!initialized || size == 0) return NULL;
    
    size = (size + 15) & ~15; // Align to 16 bytes
    
    if (pool_offset + size > ISR_POOL_SIZE) return NULL;
    
    void *ptr = &isr_memory_pool[pool_offset];
    pool_offset += size;
    
    for (size_t i = 0; i < MAX_ISR_ALLOCATIONS; i++) {
        if (!allocations[i].in_use) {
            allocations[i].ptr = ptr;
            allocations[i].size = size;
            allocations[i].in_use = true;
            break;
        }
    }
    
    return ptr;
}

void isr_free(void *ptr) {
    if (!ptr) return;
    
    for (size_t i = 0; i < MAX_ISR_ALLOCATIONS; i++) {
        if (allocations[i].ptr == ptr && allocations[i].in_use) {
            allocations[i].in_use = false;
            break;
        }
    }
}
