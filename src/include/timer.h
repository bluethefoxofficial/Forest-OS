#ifndef TIMER_H
#define TIMER_H

#include "types.h"
#include <stdint.h>

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

// Additional timer functions
void timer_sleep_ms(uint32_t milliseconds);
uint64_t timer_get_frequency(void);
uint64_t rdtsc(void);
uint64_t get_system_timer_ticks(void);

// Timer source structure is defined in interrupt.h

#endif // TIMER_H
