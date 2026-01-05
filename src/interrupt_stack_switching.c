#include "interrupt_stack_switching.h"
#include <stdint.h>
#include <stdbool.h>

static bool interrupt_stack_switching_initialized = false;

int interrupt_stack_switching_init(void) {
    interrupt_stack_switching_initialized = true;
    return 0;
}

bool interrupt_stack_switching_is_initialized(void) {
    return interrupt_stack_switching_initialized;
}
