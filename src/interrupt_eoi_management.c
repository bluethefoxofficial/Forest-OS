#include "interrupt_eoi_management.h"
#include <stdint.h>
#include <stdbool.h>

static bool interrupt_eoi_management_initialized = false;

int interrupt_eoi_management_init(void) {
    interrupt_eoi_management_initialized = true;
    return 0;
}

bool interrupt_eoi_management_is_initialized(void) {
    return interrupt_eoi_management_initialized;
}
