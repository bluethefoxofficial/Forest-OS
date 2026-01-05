#ifndef UEFI_RUNTIME_INTERRUPT_H
#define UEFI_RUNTIME_INTERRUPT_H

#include <stdint.h>
#include <stdbool.h>

int uefi_runtime_interrupt_init(void);
bool uefi_runtime_interrupt_is_initialized(void);

#endif
