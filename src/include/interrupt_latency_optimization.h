#ifndef INTERRUPT_LATENCY_OPTIMIZATION_H
#define INTERRUPT_LATENCY_OPTIMIZATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    INT_LATENCY_SUCCESS = 0,
    INT_LATENCY_ERROR_INVALID_PARAMS,
    INT_LATENCY_ERROR_NOT_INITIALIZED,
    INT_LATENCY_ERROR_CALIBRATION_FAILED,
    INT_LATENCY_ERROR_NO_SPACE,
    INT_LATENCY_ERROR_NO_DATA,
    INT_LATENCY_ERROR_NO_MEMORY,
    INT_LATENCY_ERROR_MEASUREMENT_DISABLED,
    INT_LATENCY_ERROR_OPTIMIZATION_FAILED
} interrupt_latency_error_t;

typedef struct {
    bool enable_measurement;
    bool enable_optimization;
    bool enable_performance_monitoring;
    bool real_time_mode;
    uint32_t sample_buffer_size;
    uint64_t measurement_interval_us;
    uint32_t optimization_threshold_ns;
} latency_measurement_config_t;

typedef struct {
    uint8_t vector;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    uint64_t avg_latency_ns;
    uint64_t percentile_50ns;
    uint64_t percentile_95ns;
    uint64_t percentile_99ns;
    uint32_t sample_count;
    uint32_t missed_deadlines;
    uint64_t deadline_ns;
    bool real_time_critical;
} interrupt_latency_stats_t;

typedef struct {
    uint64_t global_min_latency_ns;
    uint64_t global_max_latency_ns;
    uint64_t global_avg_latency_ns;
    uint64_t measurement_overhead_ns;
    uint64_t total_interrupts;
    uint32_t total_missed_deadlines;
    uint32_t active_interrupt_sources;
} global_latency_stats_t;

typedef struct {
    uint8_t vector;
    uint32_t iterations;
    uint64_t min_latency_cycles;
    uint64_t max_latency_cycles;
    uint64_t avg_latency_cycles;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    uint64_t avg_latency_ns;
} benchmark_result_t;

typedef struct {
    uint64_t total_cache_misses;
    uint64_t total_branch_mispredictions;
    uint64_t total_context_switches;
    uint64_t total_memory_stalls;
    uint64_t interrupt_context_switches;
} performance_analysis_t;

interrupt_latency_error_t interrupt_latency_init(const latency_measurement_config_t *config);

void interrupt_latency_measure_entry(uint8_t vector);

void interrupt_latency_measure_exit(uint8_t vector);

interrupt_latency_error_t interrupt_latency_set_deadline(uint8_t vector, uint64_t deadline_ns);

interrupt_latency_error_t interrupt_latency_get_statistics(
    uint8_t vector, interrupt_latency_stats_t *stats);

interrupt_latency_error_t interrupt_latency_get_global_statistics(
    global_latency_stats_t *stats);

interrupt_latency_error_t interrupt_latency_enable_optimization(bool enable);

interrupt_latency_error_t interrupt_latency_benchmark(
    uint8_t vector, uint32_t iterations, benchmark_result_t *result);

void interrupt_latency_reset_statistics(void);

bool interrupt_latency_is_measurement_enabled(void);

bool interrupt_latency_is_optimization_enabled(void);

uint64_t interrupt_latency_get_measurement_overhead_ns(void);

static inline const char* interrupt_latency_error_to_string(interrupt_latency_error_t error) {
    switch (error) {
        case INT_LATENCY_SUCCESS:
            return "Success";
        case INT_LATENCY_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case INT_LATENCY_ERROR_NOT_INITIALIZED:
            return "Interrupt latency measurement not initialized";
        case INT_LATENCY_ERROR_CALIBRATION_FAILED:
            return "Timer calibration failed";
        case INT_LATENCY_ERROR_NO_SPACE:
            return "No space for additional interrupt sources";
        case INT_LATENCY_ERROR_NO_DATA:
            return "No measurement data available";
        case INT_LATENCY_ERROR_NO_MEMORY:
            return "Insufficient memory";
        case INT_LATENCY_ERROR_MEASUREMENT_DISABLED:
            return "Latency measurement is disabled";
        case INT_LATENCY_ERROR_OPTIMIZATION_FAILED:
            return "Interrupt optimization failed";
        default:
            return "Unknown interrupt latency error";
    }
}

static inline latency_measurement_config_t interrupt_latency_default_config(void) {
    return (latency_measurement_config_t){
        .enable_measurement = true,
        .enable_optimization = false,
        .enable_performance_monitoring = false,
        .real_time_mode = false,
        .sample_buffer_size = 8192,
        .measurement_interval_us = 1000,
        .optimization_threshold_ns = 10000
    };
}

static inline latency_measurement_config_t interrupt_latency_realtime_config(void) {
    return (latency_measurement_config_t){
        .enable_measurement = true,
        .enable_optimization = true,
        .enable_performance_monitoring = true,
        .real_time_mode = true,
        .sample_buffer_size = 16384,
        .measurement_interval_us = 100,
        .optimization_threshold_ns = 5000
    };
}

static inline latency_measurement_config_t interrupt_latency_performance_config(void) {
    return (latency_measurement_config_t){
        .enable_measurement = true,
        .enable_optimization = true,
        .enable_performance_monitoring = true,
        .real_time_mode = false,
        .sample_buffer_size = 32768,
        .measurement_interval_us = 10000,
        .optimization_threshold_ns = 50000
    };
}

typedef enum {
    APIC_PRIORITY_LOWEST = 0x10,
    APIC_PRIORITY_LOW = 0x40,
    APIC_PRIORITY_MEDIUM = 0x80,
    APIC_PRIORITY_HIGH = 0xC0,
    APIC_PRIORITY_HIGHEST = 0xF0
} apic_priority_t;

extern uint64_t rdtsc(void);
extern uint32_t get_current_cpu_id(void);
extern uint32_t get_fastest_cpu_core(void);
extern void apic_set_interrupt_priority(uint8_t vector, apic_priority_t priority);
extern void cpu_set_affinity(uint8_t vector, uint32_t cpu_id);
extern void* malloc(size_t size);
extern void free(void* ptr);

#define INTERRUPT_LATENCY_MEASURE_ENTRY(vector) \
    do { \
        if (interrupt_latency_is_measurement_enabled()) { \
            interrupt_latency_measure_entry(vector); \
        } \
    } while(0)

#define INTERRUPT_LATENCY_MEASURE_EXIT(vector) \
    do { \
        if (interrupt_latency_is_measurement_enabled()) { \
            interrupt_latency_measure_exit(vector); \
        } \
    } while(0)

typedef struct {
    bool is_critical;
    uint64_t deadline_ns;
    uint8_t priority_level;
    uint32_t cpu_affinity_mask;
} interrupt_qos_profile_t;

static inline interrupt_qos_profile_t create_realtime_qos_profile(uint64_t deadline_ns) {
    return (interrupt_qos_profile_t){
        .is_critical = true,
        .deadline_ns = deadline_ns,
        .priority_level = APIC_PRIORITY_HIGHEST,
        .cpu_affinity_mask = 0x1  // Pin to CPU 0
    };
}

static inline interrupt_qos_profile_t create_standard_qos_profile(void) {
    return (interrupt_qos_profile_t){
        .is_critical = false,
        .deadline_ns = 100000,  // 100μs
        .priority_level = APIC_PRIORITY_MEDIUM,
        .cpu_affinity_mask = 0xFFFFFFFF  // Any CPU
    };
}

#endif // INTERRUPT_LATENCY_OPTIMIZATION_H