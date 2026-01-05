#ifndef HPET_H
#define HPET_H

#include <stdint.h>
#include <stdbool.h>

/* struct hpet_stats is defined in interrupt.h - include it if needed */
struct hpet_stats;

/* HPET initialization and control */
int hpet_init_advanced(void);
bool hpet_is_available(void);

/* HPET time functions */
uint64_t hpet_get_time_ns(void);
uint64_t hpet_read_main_counter(void);

/* HPET configuration */
void hpet_configure_periodic(uint32_t frequency);

/* HPET statistics - struct hpet_stats is defined in interrupt.h */
void hpet_get_stats(struct hpet_stats *stats);

#endif /* HPET_H */
