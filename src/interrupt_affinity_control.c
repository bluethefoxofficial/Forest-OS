#include "interrupt_affinity_control.h"
#include <stdint.h>
#include <stdbool.h>

static bool interrupt_affinity_control_initialized = false;

int interrupt_affinity_control_init(void) {
    interrupt_affinity_control_initialized = true;
    return 0;
}

bool interrupt_affinity_control_is_initialized(void) {
    return interrupt_affinity_control_initialized;
}
