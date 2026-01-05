#include "interrupt_profiling.h"
#include <stdint.h>
#include <stdbool.h>

static bool interrupt_profiling_initialized = false;

int interrupt_profiling_init(void) {
    interrupt_profiling_initialized = true;
    return 0;
}

bool interrupt_profiling_is_initialized(void) {
    return interrupt_profiling_initialized;
}
