#ifndef INTERRUPT_THROTTLING_H
#define INTERRUPT_THROTTLING_H

#include <stdint.h>
#include <stdbool.h>

int interrupt_throttling_init(void);
bool interrupt_throttling_is_initialized(void);

#endif
