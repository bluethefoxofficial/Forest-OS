#include "uefi_runtime_interrupt.h"
#include <stdint.h>
#include <stdbool.h>

static bool uefi_runtime_interrupt_initialized = false;

int uefi_runtime_interrupt_init(void) {
    uefi_runtime_interrupt_initialized = true;
    return 0;
}

bool uefi_runtime_interrupt_is_initialized(void) {
    return uefi_runtime_interrupt_initialized;
}
