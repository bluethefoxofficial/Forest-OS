#ifndef TIME_H
#define TIME_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Time management for Forest OS
 * Provides timing functions, delays, and time tracking
 */

typedef struct {
    uint32_t seconds;
    uint32_t nanoseconds;
} timespec_t;

typedef struct {
    uint8_t second;   // 0-59
    uint8_t minute;   // 0-59
    uint8_t hour;     // 0-23
    uint8_t day;      // 1-31
    uint8_t month;    // 1-12
    uint8_t year;     // Years since 1900
} rtc_time_t;

// Time conversion functions
uint64_t time_get_ticks(void);
uint64_t time_get_uptime_ms(void);
void time_delay_ms(uint32_t milliseconds);
void time_delay_us(uint32_t microseconds);

// RTC (Real Time Clock) functions
int rtc_read_time(rtc_time_t *time);
int rtc_set_time(const rtc_time_t *time);
bool rtc_is_available(void);

// High-precision timing
uint64_t time_rdtsc(void);
uint64_t time_get_cpu_frequency(void);

// Time format conversion
uint64_t timespec_to_ms(const timespec_t *ts);
void ms_to_timespec(uint64_t ms, timespec_t *ts);

// Timer initialization
int time_init(void);

#endif // TIME_H