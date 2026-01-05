#ifndef INTERRUPT_EOI_MANAGEMENT_H
#define INTERRUPT_EOI_MANAGEMENT_H

#include <stdint.h>
#include <stdbool.h>

int interrupt_eoi_management_init(void);
bool interrupt_eoi_management_is_initialized(void);

#endif
