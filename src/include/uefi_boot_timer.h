#ifndef UEFI_BOOT_TIMER_H
#define UEFI_BOOT_TIMER_H

#include <stdint.h>
#include <stdbool.h>

int uefi_boot_timer_init(void);
bool uefi_boot_timer_is_initialized(void);

#endif
