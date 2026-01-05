#include "interrupt_mask_primitives.h"
#include <stdint.h>
#include <stdbool.h>

static bool interrupt_mask_primitives_initialized = false;

int interrupt_mask_primitives_init(void) {
    interrupt_mask_primitives_initialized = true;
    return 0;
}

bool interrupt_mask_primitives_is_initialized(void) {
    return interrupt_mask_primitives_initialized;
}
