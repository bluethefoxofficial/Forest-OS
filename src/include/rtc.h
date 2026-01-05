#ifndef RTC_H
#define RTC_H

#include <stdint.h>
#include <stdbool.h>

/* struct rtc_time and rtc_stats are defined in interrupt.h */
struct rtc_time;
struct rtc_stats;

/* RTC initialization */
int rtc_interrupt_init(void);

/* RTC interrupt control */
int rtc_enable_periodic_interrupts(uint8_t rate);
void rtc_disable_periodic_interrupts(void);
int rtc_enable_update_interrupts(void);
void rtc_disable_update_interrupts(void);

/* RTC information */
bool rtc_is_available(void);
void rtc_get_stats(struct rtc_stats *stats);

/* RTC time reading - returns time in microseconds since epoch */
uint64_t rtc_read_time_us(void);

#endif /* RTC_H */
