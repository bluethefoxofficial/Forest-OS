#ifndef INTERRUPT_AFFINITY_CONTROL_H
#define INTERRUPT_AFFINITY_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

int interrupt_affinity_control_init(void);
bool interrupt_affinity_control_is_initialized(void);

#endif
