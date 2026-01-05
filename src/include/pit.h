#ifndef PIT_H
#define PIT_H

#include <stdint.h>
#include <stdbool.h>

/* PIT statistics structure - defined in interrupt.h */
struct pit_stats;

/* PIT initialization */
int pit_init_advanced(void);

/* PIT delay functions */
void pit_delay_ms(uint32_t milliseconds);
void pit_udelay(uint32_t microseconds);

/* PIT information */
uint32_t pit_get_frequency(void);
bool pit_is_available(void);
uint64_t pit_get_time_ns(void);

/* PIT calibration */
uint64_t pit_calibrate_timing_source(uint64_t (*read_counter)(void), uint32_t ms_duration);

/* PIT statistics */
void pit_get_stats(struct pit_stats *stats);

/* PIT system timer control */
int pit_enable_system_timer(void);
void pit_disable_system_timer(void);

/* PIT counter read - returns current system time in nanoseconds */
uint64_t pit_read_counter(void);

/* PIT configuration - set frequency in Hz */
void pit_configure(uint32_t frequency);

#endif /* PIT_H */
