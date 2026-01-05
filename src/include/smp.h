#ifndef SMP_H
#define SMP_H

#include "types.h"
#include <stdbool.h>

#define SMP_MAX_CPUS 32

typedef struct {
    uint32 acpi_id;
    uint32 apic_id;
    bool enabled;
    bool bsp;
    bool online;
} smp_cpu_info_t;

typedef struct {
    smp_cpu_info_t cpus[SMP_MAX_CPUS];
    uint32 cpu_count;
    uint32 online_cpus;
    uint32 bsp_index;
    uint32 bsp_apic_id;
    uint32 lapic_base;
    bool initialized;
} smp_state_t;

bool smp_init(void);
uint32 smp_get_cpu_count(void);
bool smp_has_smp(void);
const smp_cpu_info_t* smp_get_cpu(uint32 index);
const smp_state_t* smp_get_state(void);
uint32 smp_get_lapic_base(void);
uint32 smp_get_bsp_index(void);
void smp_mark_cpu_online(uint32 apic_id);

#endif // SMP_H
