#ifndef INTERRUPT_STACK_SWITCHING_H
#define INTERRUPT_STACK_SWITCHING_H

#include <stdint.h>
#include <stdbool.h>

int interrupt_stack_switching_init(void);
bool interrupt_stack_switching_is_initialized(void);

#endif
