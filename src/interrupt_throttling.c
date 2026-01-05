#include "interrupt_throttling.h"
#include <stdint.h>
#include <stdbool.h>

static bool interrupt_throttling_initialized = false;

int interrupt_throttling_init(void) {
    interrupt_throttling_initialized = true;
    return 0;
}

bool interrupt_throttling_is_initialized(void) {
    return interrupt_throttling_initialized;
}
