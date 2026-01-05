#ifndef INTERRUPT_MEMORY_SYNC_H
#define INTERRUPT_MEMORY_SYNC_H

#include <stdint.h>
#include <stdbool.h>

int interrupt_memory_sync_init(void);
bool interrupt_memory_sync_is_initialized(void);

#endif
