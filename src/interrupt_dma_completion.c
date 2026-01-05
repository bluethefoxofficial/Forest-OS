#include "interrupt_dma_completion.h"
#include <stdint.h>
#include <stdbool.h>

static bool interrupt_dma_completion_initialized = false;

int interrupt_dma_completion_init(void) {
    interrupt_dma_completion_initialized = true;
    return 0;
}

bool interrupt_dma_completion_is_initialized(void) {
    return interrupt_dma_completion_initialized;
}
