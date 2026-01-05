#ifndef INTERRUPT_REPLAY_MECHANISM_H
#define INTERRUPT_REPLAY_MECHANISM_H

#include <stdint.h>
#include <stdbool.h>

int interrupt_replay_mechanism_init(void);
bool interrupt_replay_mechanism_is_initialized(void);

#endif
