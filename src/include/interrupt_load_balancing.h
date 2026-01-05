#ifndef INTERRUPT_LOAD_BALANCING_H
#define INTERRUPT_LOAD_BALANCING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "interrupt_common_types.h"

#define MAX_CPU_COUNT 256

typedef enum {
    LOAD_BALANCE_SUCCESS = 0,
    LOAD_BALANCE_ERROR_INVALID_PARAMS,
    LOAD_BALANCE_ERROR_NOT_INITIALIZED,
    LOAD_BALANCE_ERROR_NO_SPACE,
    LOAD_BALANCE_ERROR_TIMER_FAILED,
    LOAD_BALANCE_ERROR_MIGRATION_FAILED,
    LOAD_BALANCE_ERROR_CPU_OFFLINE
} load_balance_error_t;

typedef enum {
    LOAD_BALANCE_ROUND_ROBIN = 0,
    LOAD_BALANCE_LOAD_BASED,
    LOAD_BALANCE_LATENCY_AWARE,
    LOAD_BALANCE_ADAPTIVE
} load_balance_algorithm_t;

typedef struct {
    load_balance_algorithm_t algorithm;
    bool enabled;
    uint32_t balance_interval_ms;
    uint32_t load_threshold;
    uint32_t migration_cooldown_ms;
    uint64_t latency_threshold_ns;
    bool avoid_cpu0;
    bool real_time_pinning;
} load_balance_config_t;

typedef struct {
    uint32_t initial_cpu;
    uint32_t priority;
    bool real_time;
    bool migratable;
} vector_balance_config_t;

typedef struct {
    uint64_t total_migrations;
    uint64_t successful_migrations;
    uint64_t failed_migrations;
    uint64_t load_balance_cycles;
    double success_rate;
    bool enabled;
    load_balance_algorithm_t algorithm;
    uint32_t managed_vectors;
    uint32_t active_cpus;
    uint32_t avg_cpu_load;
    double load_imbalance_ratio;
} load_balance_stats_t;

typedef struct {
    uint32_t cpu_id;
    uint32_t load_score;
    uint64_t interrupt_count;
    uint32_t active_vectors;
    uint64_t avg_latency_ns;
    bool online;
} cpu_load_stats_t;

load_balance_error_t interrupt_load_balance_init(const load_balance_config_t *config);

load_balance_error_t interrupt_load_balance_register_vector(uint8_t vector, 
                                                          const vector_balance_config_t *config);

load_balance_error_t interrupt_load_balance_set_algorithm(load_balance_algorithm_t algorithm);

load_balance_error_t interrupt_load_balance_enable(bool enable);

load_balance_error_t interrupt_load_balance_force_balance(void);

load_balance_error_t interrupt_load_balance_get_statistics(load_balance_stats_t *stats);

load_balance_error_t interrupt_load_balance_get_cpu_loads(cpu_load_stats_t *loads, size_t max_cpus);

bool interrupt_load_balance_is_enabled(void);

bool interrupt_load_balance_is_initialized(void);

size_t interrupt_load_balance_get_managed_vectors(void);

uint64_t interrupt_load_balance_get_total_migrations(void);

static inline const char* load_balance_error_to_string(load_balance_error_t error) {
    switch (error) {
        case LOAD_BALANCE_SUCCESS:
            return "Success";
        case LOAD_BALANCE_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case LOAD_BALANCE_ERROR_NOT_INITIALIZED:
            return "Load balancer not initialized";
        case LOAD_BALANCE_ERROR_NO_SPACE:
            return "No space for additional vectors";
        case LOAD_BALANCE_ERROR_TIMER_FAILED:
            return "Timer creation failed";
        case LOAD_BALANCE_ERROR_MIGRATION_FAILED:
            return "Interrupt migration failed";
        case LOAD_BALANCE_ERROR_CPU_OFFLINE:
            return "Target CPU is offline";
        default:
            return "Unknown load balance error";
    }
}

static inline const char* load_balance_algorithm_to_string(load_balance_algorithm_t algorithm) {
    switch (algorithm) {
        case LOAD_BALANCE_ROUND_ROBIN:
            return "Round Robin";
        case LOAD_BALANCE_LOAD_BASED:
            return "Load Based";
        case LOAD_BALANCE_LATENCY_AWARE:
            return "Latency Aware";
        case LOAD_BALANCE_ADAPTIVE:
            return "Adaptive";
        default:
            return "Unknown";
    }
}

static inline load_balance_config_t load_balance_default_config(void) {
    return (load_balance_config_t){
        .algorithm = LOAD_BALANCE_LOAD_BASED,
        .enabled = true,
        .balance_interval_ms = 100,
        .load_threshold = 20,
        .migration_cooldown_ms = 500,
        .latency_threshold_ns = 10000,
        .avoid_cpu0 = true,
        .real_time_pinning = true
    };
}

static inline load_balance_config_t load_balance_server_config(void) {
    return (load_balance_config_t){
        .algorithm = LOAD_BALANCE_ADAPTIVE,
        .enabled = true,
        .balance_interval_ms = 50,
        .load_threshold = 15,
        .migration_cooldown_ms = 200,
        .latency_threshold_ns = 5000,
        .avoid_cpu0 = false,
        .real_time_pinning = true
    };
}

static inline load_balance_config_t load_balance_realtime_config(void) {
    return (load_balance_config_t){
        .algorithm = LOAD_BALANCE_LATENCY_AWARE,
        .enabled = false,
        .balance_interval_ms = 1000,
        .load_threshold = 50,
        .migration_cooldown_ms = 2000,
        .latency_threshold_ns = 1000,
        .avoid_cpu0 = true,
        .real_time_pinning = true
    };
}

static inline vector_balance_config_t create_standard_vector_config(uint32_t initial_cpu) {
    return (vector_balance_config_t){
        .initial_cpu = initial_cpu,
        .priority = 1,
        .real_time = false,
        .migratable = true
    };
}

static inline vector_balance_config_t create_realtime_vector_config(uint32_t cpu) {
    return (vector_balance_config_t){
        .initial_cpu = cpu,
        .priority = 3,
        .real_time = true,
        .migratable = false
    };
}

static inline vector_balance_config_t create_network_vector_config(uint32_t initial_cpu) {
    return (vector_balance_config_t){
        .initial_cpu = initial_cpu,
        .priority = 2,
        .real_time = false,
        .migratable = true
    };
}

/* Timer and SMP types are now in interrupt_common_types.h */

static inline bool load_balance_should_migrate(uint32_t from_load, uint32_t to_load, uint32_t threshold) {
    return (from_load > to_load) && ((from_load - to_load) >= threshold);
}

static inline uint32_t load_balance_calculate_target_load(uint32_t total_load, uint32_t cpu_count) {
    return cpu_count > 0 ? total_load / cpu_count : 0;
}

static inline double load_balance_calculate_efficiency(uint64_t successful, uint64_t total) {
    return total > 0 ? (double)successful / (double)total : 0.0;
}

#define LOAD_BALANCE_HIGH_LOAD_THRESHOLD 80
#define LOAD_BALANCE_MEDIUM_LOAD_THRESHOLD 50
#define LOAD_BALANCE_LOW_LOAD_THRESHOLD 20

static inline load_balance_algorithm_t load_balance_recommend_algorithm(uint32_t cpu_count, 
                                                                       bool real_time_workload) {
    if (real_time_workload) {
        return LOAD_BALANCE_LATENCY_AWARE;
    }
    
    if (cpu_count <= 4) {
        return LOAD_BALANCE_ROUND_ROBIN;
    } else if (cpu_count <= 16) {
        return LOAD_BALANCE_LOAD_BASED;
    } else {
        return LOAD_BALANCE_ADAPTIVE;
    }
}

#endif // INTERRUPT_LOAD_BALANCING_H