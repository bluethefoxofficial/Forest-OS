#ifndef TIMER_ABSTRACTION_H
#define TIMER_ABSTRACTION_H

#include <stdint.h>
#include <stdbool.h>
#include "interrupt_common_types.h"

/* Types and basic timer functions are now declared in interrupt_common_types.h:
 * - timer_error_t
 * - timer_mode_t
 * - timer_handle_t
 * - timer_config_t
 * - timer_abstraction_create_timer
 * - timer_abstraction_destroy_timer
 * - timer_abstraction_start_timer
 * - timer_abstraction_stop_timer
 * - timer_abstraction_reset_timer
 * - timer_abstraction_get_frequency
 * - timer_get_frequency
 * - rdtsc
 */

#endif // TIMER_ABSTRACTION_H