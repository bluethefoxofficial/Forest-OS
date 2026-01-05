#include "interrupt_replay_mechanism.h"
#include <stdint.h>
#include <stdbool.h>

static bool interrupt_replay_mechanism_initialized = false;

int interrupt_replay_mechanism_init(void) {
    interrupt_replay_mechanism_initialized = true;
    return 0;
}

bool interrupt_replay_mechanism_is_initialized(void) {
    return interrupt_replay_mechanism_initialized;
}
