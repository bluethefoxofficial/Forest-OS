#include "uefi_boot_timer.h"
#include <stdint.h>
#include <stdbool.h>

static bool uefi_boot_timer_initialized = false;

int uefi_boot_timer_init(void) {
    uefi_boot_timer_initialized = true;
    return 0;
}

bool uefi_boot_timer_is_initialized(void) {
    return uefi_boot_timer_initialized;
}
