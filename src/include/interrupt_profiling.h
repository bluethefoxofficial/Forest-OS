#ifndef INTERRUPT_PROFILING_H
#define INTERRUPT_PROFILING_H

#include <stdint.h>
#include <stdbool.h>

int interrupt_profiling_init(void);
bool interrupt_profiling_is_initialized(void);

#endif
