#include "interrupt_memory_sync.h"
#include <stdint.h>
#include <stdbool.h>

static bool interrupt_memory_sync_initialized = false;

int interrupt_memory_sync_init(void) {
    interrupt_memory_sync_initialized = true;
    return 0;
}

bool interrupt_memory_sync_is_initialized(void) {
    return interrupt_memory_sync_initialized;
}
