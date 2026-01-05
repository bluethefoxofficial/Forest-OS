#ifndef SMP_INTERRUPT_DISTRIBUTION_H
#define SMP_INTERRUPT_DISTRIBUTION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "interrupt_common_types.h"

#define MAX_CPU_COUNT 256

/*
 * Note: smp_interrupt_init() is declared in interrupt.h with signature void(void)
 * For the config-based initialization, use smp_interrupt_distribution_init() instead.
 */
smp_interrupt_error_t smp_interrupt_distribution_init(const smp_distribution_config_t *config);

smp_interrupt_error_t smp_interrupt_register_vector(
    uint8_t vector, 
    interrupt_distribution_mode_t mode,
    const interrupt_affinity_t *affinity);

/* These are declared in interrupt_common_types.h:
 * smp_interrupt_set_affinity, smp_interrupt_get_statistics, smp_interrupt_get_cpu_info
 */

smp_interrupt_error_t smp_interrupt_distribute(uint8_t vector);

smp_interrupt_error_t smp_interrupt_enable_cpu(uint32_t cpu_id);

smp_interrupt_error_t smp_interrupt_disable_cpu(uint32_t cpu_id);

void smp_interrupt_enable_load_balancing(bool enable);

void smp_interrupt_reset_statistics(void);

bool smp_interrupt_is_initialized(void);

bool smp_interrupt_is_load_balancing_enabled(void);

uint32_t smp_interrupt_get_cpu_count(void);

uint32_t smp_interrupt_get_online_cpu_count(void);

static inline const char* smp_interrupt_error_to_string(smp_interrupt_error_t error) {
    switch (error) {
        case SMP_INT_SUCCESS:
            return "Success";
        case SMP_INT_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case SMP_INT_ERROR_NOT_INITIALIZED:
            return "SMP interrupt distribution not initialized";
        case SMP_INT_ERROR_NO_SPACE:
            return "No space for additional interrupt vectors";
        case SMP_INT_ERROR_VECTOR_NOT_FOUND:
            return "Interrupt vector not found";
        case SMP_INT_ERROR_CPU_OFFLINE:
            return "Target CPU is offline";
        case SMP_INT_ERROR_CANNOT_DISABLE_BSP:
            return "Cannot disable bootstrap processor";
        case SMP_INT_ERROR_ACPI_FAILED:
            return "ACPI initialization failed";
        case SMP_INT_ERROR_NO_CPUS:
            return "No CPUs available";
        case SMP_INT_ERROR_ROUTING_FAILED:
            return "Interrupt routing failed";
        default:
            return "Unknown SMP interrupt error";
    }
}

static inline const char* interrupt_distribution_mode_to_string(
    interrupt_distribution_mode_t mode) {
    switch (mode) {
        case DIST_MODE_FIXED:
            return "Fixed";
        case DIST_MODE_ROUND_ROBIN:
            return "Round Robin";
        case DIST_MODE_LOAD_BALANCED:
            return "Load Balanced";
        case DIST_MODE_LOWEST_PRIORITY:
            return "Lowest Priority";
        case DIST_MODE_REAL_TIME_DEDICATED:
            return "Real-time Dedicated";
        case DIST_MODE_NUMA_AWARE:
            return "NUMA Aware";
        case DIST_MODE_BROADCAST:
            return "Broadcast";
        default:
            return "Unknown";
    }
}

static inline smp_distribution_config_t smp_interrupt_default_config(void) {
    return (smp_distribution_config_t){
        .enable_load_balancing = true,
        .enable_numa_awareness = false,
        .avoid_hyperthreading = false,
        .prefer_physical_cores = true,
        .load_balance_interval_ms = 100,
        .migration_threshold = 10
    };
}

static inline smp_distribution_config_t smp_interrupt_realtime_config(void) {
    return (smp_distribution_config_t){
        .enable_load_balancing = false,
        .enable_numa_awareness = true,
        .avoid_hyperthreading = true,
        .prefer_physical_cores = true,
        .load_balance_interval_ms = 10,
        .migration_threshold = 5
    };
}

static inline smp_distribution_config_t smp_interrupt_performance_config(void) {
    return (smp_distribution_config_t){
        .enable_load_balancing = true,
        .enable_numa_awareness = true,
        .avoid_hyperthreading = false,
        .prefer_physical_cores = false,
        .load_balance_interval_ms = 50,
        .migration_threshold = 20
    };
}

/* create_cpu_affinity is declared in interrupt_common_types.h */

static inline interrupt_affinity_t smp_create_mask_affinity(uint64_t cpu_mask) {
    return (interrupt_affinity_t){
        .cpu_mask = cpu_mask,
        .preferred_cpu = __builtin_ctzll(cpu_mask),
        .priority = SMP_PRIORITY_NORMAL,
        .real_time_critical = false,
        .numa_local_only = false
    };
}

static inline interrupt_affinity_t smp_create_realtime_affinity(uint32_t cpu_id) {
    return (interrupt_affinity_t){
        .cpu_mask = 1ULL << cpu_id,
        .preferred_cpu = cpu_id,
        .priority = SMP_PRIORITY_CRITICAL,
        .real_time_critical = true,
        .numa_local_only = true
    };
}

/* rdtsc is declared in interrupt_common_types.h */
extern cpu_feature_set_t detect_cpu_features(uint32_t cpu_id);
extern uint32_t select_numa_optimal_cpu(uint8_t vector);
/* local_apic_enable/disable are declared in local_apic.h with different signatures */
extern void io_apic_configure_entry_extended(uint8_t io_apic_id, uint8_t pin,
                                            uint8_t vector, uint32_t target_apic_id,
                                            bool active_low, bool level_triggered);

#endif // SMP_INTERRUPT_DISTRIBUTION_H