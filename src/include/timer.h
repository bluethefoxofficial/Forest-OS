#ifndef TIMER_H
#define TIMER_H

#include "types.h"

/*
 * Timer management for Forest OS
 * Provides timer interrupt handling and tick counting
 */

// Initialize the timer with specified frequency (Hz)
bool timer_init(uint32 frequency);

// Get current timer tick count
uint32 timer_get_ticks(void);

// Disable timer IRQs and handlers (used for shutdown)
void timer_shutdown(void);

#endif // TIMER_H
