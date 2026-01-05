#ifndef INTERRUPT_COMMON_TYPES_H
#define INTERRUPT_COMMON_TYPES_H

/*
 * Shared types for interrupt subsystem
 * This header consolidates types used across multiple interrupt-related headers
 * to avoid duplicate definitions and type conflicts.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Timer types - used by timer_abstraction, interrupt_coalescing, interrupt_load_balancing
 */
#ifndef TIMER_ERROR_T_DEFINED
#define TIMER_ERROR_T_DEFINED
typedef enum {
    TIMER_SUCCESS = 0,
    TIMER_ERROR_FAILED,
    TIMER_ERROR_INVALID_PARAMS,
    TIMER_ERROR_NOT_INITIALIZED,
    TIMER_ERROR_NO_RESOURCES
} timer_error_t;
#endif

#ifndef TIMER_MODE_T_DEFINED
#define TIMER_MODE_T_DEFINED
typedef enum {
    TIMER_MODE_PERIODIC = 0,
    TIMER_MODE_ONE_SHOT = 1
} timer_mode_t;
#endif

#ifndef TIMER_HANDLE_T_DEFINED
#define TIMER_HANDLE_T_DEFINED
typedef uint32_t timer_handle_t;
#endif

#ifndef TIMER_CONFIG_T_DEFINED
#define TIMER_CONFIG_T_DEFINED
typedef struct timer_config {
    timer_mode_t mode;
    uint32_t frequency_hz;
    void (*callback)(void *data);
    void *callback_data;
} timer_config_t;
#endif

/*
 * SMP interrupt types - used by smp_interrupt_distribution, interrupt_load_balancing
 */
typedef enum {
    SMP_INT_SUCCESS = 0,
    SMP_INT_ERROR_INVALID_PARAMS,
    SMP_INT_ERROR_NOT_INITIALIZED,
    SMP_INT_ERROR_NO_SPACE,
    SMP_INT_ERROR_VECTOR_NOT_FOUND,
    SMP_INT_ERROR_CPU_OFFLINE,
    SMP_INT_ERROR_CANNOT_DISABLE_BSP,
    SMP_INT_ERROR_ACPI_FAILED,
    SMP_INT_ERROR_NO_CPUS,
    SMP_INT_ERROR_ROUTING_FAILED
} smp_interrupt_error_t;

typedef enum {
    DIST_MODE_FIXED = 0,
    DIST_MODE_ROUND_ROBIN,
    DIST_MODE_LOAD_BALANCED,
    DIST_MODE_LOWEST_PRIORITY,
    DIST_MODE_REAL_TIME_DEDICATED,
    DIST_MODE_NUMA_AWARE,
    DIST_MODE_BROADCAST
} interrupt_distribution_mode_t;

/* Priority levels for SMP distribution - distinct from interrupt.h priority macros */
typedef enum {
    SMP_PRIORITY_LOW = 0,
    SMP_PRIORITY_NORMAL = 1,
    SMP_PRIORITY_HIGH = 2,
    SMP_PRIORITY_CRITICAL = 3
} smp_priority_level_t;

typedef enum {
    CACHE_L1_ONLY = 1,
    CACHE_L2_SHARED = 2,
    CACHE_L3_SHARED = 3
} cache_level_t;

typedef struct {
    bool supports_hyperthreading;
    bool supports_virtualization;
    bool supports_x2apic;
    bool supports_tsc_deadline;
    cache_level_t cache_level;
    uint32_t cache_size_kb;
} cpu_feature_set_t;

typedef struct {
    uint64_t cpu_mask;
    uint32_t preferred_cpu;
    smp_priority_level_t priority;
    bool real_time_critical;
    bool numa_local_only;
} interrupt_affinity_t;

typedef struct {
    bool enable_load_balancing;
    bool enable_numa_awareness;
    bool avoid_hyperthreading;
    bool prefer_physical_cores;
    uint32_t load_balance_interval_ms;
    uint32_t migration_threshold;
} smp_distribution_config_t;

typedef struct {
    uint32_t total_cpus;
    uint32_t online_cpus;
    uint64_t total_interrupts_distributed;
    uint64_t load_balance_operations;
    uint64_t migration_operations;
    uint32_t registered_vectors;
    uint64_t min_cpu_load;
    uint64_t max_cpu_load;
    uint64_t avg_cpu_load;
    double load_balance_ratio;
} smp_interrupt_stats_t;

typedef struct {
    uint32_t cpu_id;
    uint32_t local_apic_id;
    bool online;
    bool enabled;
    uint64_t interrupt_count;
    uint32_t current_load;
    uint32_t assigned_vectors;
    uint64_t total_latency_ns;
    cpu_feature_set_t features;
} cpu_interrupt_info_t;

/*
 * Coalescing types - used by interrupt_coalescing
 */
typedef void (*coalescing_handler_t)(void *context);

typedef enum {
    INT_SUCCESS = 0,
    INT_ERROR_NOT_FOUND,
    INT_ERROR_INVALID_PARAMS,
    INT_ERROR_NO_SPACE
} interrupt_error_t;

/*
 * Common function declarations
 */
extern uint64_t rdtsc(void);
extern uint64_t timer_get_frequency(void);

/* Timer abstraction functions */
extern timer_error_t timer_abstraction_create_timer(const timer_config_t *config, timer_handle_t *handle);
extern timer_error_t timer_abstraction_destroy_timer(timer_handle_t handle);
extern timer_error_t timer_abstraction_start_timer(timer_handle_t handle);
extern timer_error_t timer_abstraction_stop_timer(timer_handle_t handle);
extern timer_error_t timer_abstraction_reset_timer(timer_handle_t handle);
extern timer_error_t timer_abstraction_get_frequency(uint64_t *frequency);

/* SMP interrupt functions */
extern smp_interrupt_error_t smp_interrupt_init_config(const smp_distribution_config_t *config);
extern smp_interrupt_error_t smp_interrupt_set_affinity(uint8_t vector, const interrupt_affinity_t *affinity);
extern smp_interrupt_error_t smp_interrupt_get_statistics(smp_interrupt_stats_t *stats);
extern smp_interrupt_error_t smp_interrupt_get_cpu_info(uint32_t cpu_id, cpu_interrupt_info_t *info);
extern uint32_t smp_interrupt_get_cpu_count(void);
extern interrupt_affinity_t create_cpu_affinity(uint32_t cpu_id);

#endif /* INTERRUPT_COMMON_TYPES_H */
