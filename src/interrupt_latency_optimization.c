#include "interrupt_latency_optimization.h"
#include "interrupt_management.h"
#include "timer_calibration.h"
#include "tsc_calibration.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX_LATENCY_SAMPLES 8192
#define MAX_INTERRUPT_SOURCES 256
#define LATENCY_HISTOGRAM_BINS 64
#define OPTIMIZATION_THRESHOLD_NS 10000
#define CALIBRATION_ITERATIONS 1000

typedef struct {
    uint64_t entry_timestamp;
    uint64_t exit_timestamp;
    uint64_t interrupt_latency;
    uint64_t handler_duration;
    uint8_t vector;
    uint32_t cpu_id;
    bool preempted;
} latency_sample_t;

typedef struct {
    uint32_t bin_ranges[LATENCY_HISTOGRAM_BINS];
    uint32_t bin_counts[LATENCY_HISTOGRAM_BINS];
    uint64_t total_samples;
    uint64_t min_latency;
    uint64_t max_latency;
    uint64_t avg_latency;
} latency_histogram_t;

typedef struct {
    uint8_t vector;
    uint64_t min_latency;
    uint64_t max_latency;
    uint64_t avg_latency;
    uint64_t total_latency;
    uint32_t sample_count;
    uint32_t missed_deadlines;
    uint32_t deadline_ns;
    bool real_time_critical;
    latency_histogram_t histogram;
} interrupt_source_stats_t;

typedef struct {
    uint64_t cache_line_misses;
    uint64_t branch_mispredictions;
    uint64_t context_switches;
    uint64_t memory_stalls;
    uint64_t pipeline_stalls;
} performance_counters_t;

typedef struct {
    latency_sample_t samples[MAX_LATENCY_SAMPLES];
    size_t sample_index;
    uint32_t sample_count;
    
    interrupt_source_stats_t source_stats[MAX_INTERRUPT_SOURCES];
    size_t source_count;
    
    uint64_t tsc_frequency;
    uint64_t measurement_overhead;
    uint64_t global_min_latency;
    uint64_t global_max_latency;
    uint64_t global_avg_latency;
    
    performance_counters_t perf_counters;
    
    bool measurement_enabled;
    bool optimization_enabled;
    bool real_time_mode;
    bool initialized;
} interrupt_latency_context_t;

static interrupt_latency_context_t latency_ctx = {0};
static __thread uint64_t interrupt_entry_time = 0;
static __thread bool in_latency_measurement_context = false;

static uint64_t read_performance_counter(uint32_t counter) {
    uint32_t low, high;
    __asm__ volatile (
        "rdpmc"
        : "=a" (low), "=d" (high)
        : "c" (counter)
    );
    return ((uint64_t)high << 32) | low;
}

static void setup_performance_monitoring(void) {
    uint64_t perfevtsel0 = (1ULL << 22) |  // Enable
                          (1ULL << 17) |   // OS mode
                          (1ULL << 16) |   // User mode
                          0x3C;            // CPU cycles
    
    uint64_t perfevtsel1 = (1ULL << 22) |  // Enable
                          (1ULL << 17) |   // OS mode
                          (1ULL << 16) |   // User mode
                          0x2E;            // Last level cache references
    
    uint64_t perfevtsel2 = (1ULL << 22) |  // Enable
                          (1ULL << 17) |   // OS mode
                          (1ULL << 16) |   // User mode
                          0xC5;            // Branch mispredictions
    
    __asm__ volatile ("wrmsr" : : "a" ((uint32_t)perfevtsel0), 
                     "d" ((uint32_t)(perfevtsel0 >> 32)), "c" (0x186));
    __asm__ volatile ("wrmsr" : : "a" ((uint32_t)perfevtsel1), 
                     "d" ((uint32_t)(perfevtsel1 >> 32)), "c" (0x187));
    __asm__ volatile ("wrmsr" : : "a" ((uint32_t)perfevtsel2), 
                     "d" ((uint32_t)(perfevtsel2 >> 32)), "c" (0x188));
}

static uint64_t calibrate_measurement_overhead(void) {
    uint64_t overhead_samples[100];
    
    for (int i = 0; i < 100; i++) {
        uint64_t start = rdtsc();
        uint64_t end = rdtsc();
        overhead_samples[i] = end - start;
    }
    
    uint64_t total_overhead = 0;
    for (int i = 0; i < 100; i++) {
        total_overhead += overhead_samples[i];
    }
    
    return total_overhead / 100;
}

static void update_histogram(latency_histogram_t *histogram, uint64_t latency_ns) {
    histogram->total_samples++;
    
    if (histogram->total_samples == 1) {
        histogram->min_latency = latency_ns;
        histogram->max_latency = latency_ns;
        histogram->avg_latency = latency_ns;
    } else {
        if (latency_ns < histogram->min_latency) {
            histogram->min_latency = latency_ns;
        }
        if (latency_ns > histogram->max_latency) {
            histogram->max_latency = latency_ns;
        }
        histogram->avg_latency = ((histogram->avg_latency * (histogram->total_samples - 1)) + 
                                 latency_ns) / histogram->total_samples;
    }
    
    uint32_t bin_size = 1000;
    uint32_t bin_index = latency_ns / bin_size;
    if (bin_index >= LATENCY_HISTOGRAM_BINS) {
        bin_index = LATENCY_HISTOGRAM_BINS - 1;
    }
    
    histogram->bin_counts[bin_index]++;
    histogram->bin_ranges[bin_index] = bin_index * bin_size;
}

static interrupt_source_stats_t* find_or_create_source_stats(uint8_t vector) {
    for (size_t i = 0; i < latency_ctx.source_count; i++) {
        if (latency_ctx.source_stats[i].vector == vector) {
            return &latency_ctx.source_stats[i];
        }
    }
    
    if (latency_ctx.source_count >= MAX_INTERRUPT_SOURCES) {
        return NULL;
    }
    
    interrupt_source_stats_t *stats = &latency_ctx.source_stats[latency_ctx.source_count];
    memset(stats, 0, sizeof(interrupt_source_stats_t));
    stats->vector = vector;
    stats->min_latency = UINT64_MAX;
    stats->deadline_ns = 100000;
    
    latency_ctx.source_count++;
    return stats;
}

static void optimize_interrupt_delivery(uint8_t vector) {
    interrupt_source_stats_t *stats = find_or_create_source_stats(vector);
    if (!stats || !latency_ctx.optimization_enabled) {
        return;
    }
    
    if (stats->avg_latency > OPTIMIZATION_THRESHOLD_NS) {
        __asm__ volatile ("cli");
        
        uint64_t cr0;
        __asm__ volatile ("mov %%cr0, %0" : "=r" (cr0));
        cr0 |= (1 << 30);
        __asm__ volatile ("mov %0, %%cr0" : : "r" (cr0));
        
        __asm__ volatile ("wbinvd");
        
        __asm__ volatile ("sti");
    }
    
    if (stats->real_time_critical && stats->avg_latency > stats->deadline_ns) {
        apic_set_interrupt_priority(vector, APIC_PRIORITY_HIGHEST);
        
        cpu_set_affinity(vector, get_fastest_cpu_core());
    }
}

interrupt_latency_error_t interrupt_latency_init(const latency_measurement_config_t *config) {
    if (!config) {
        return INT_LATENCY_ERROR_INVALID_PARAMS;
    }
    
    memset(&latency_ctx, 0, sizeof(latency_ctx));
    
    uint64_t tsc_freq;
    if (timer_calibration_get_system_frequency(&tsc_freq) != TIMER_CALIB_SUCCESS) {
        return INT_LATENCY_ERROR_CALIBRATION_FAILED;
    }
    latency_ctx.tsc_frequency = tsc_freq;
    
    latency_ctx.measurement_overhead = calibrate_measurement_overhead();
    
    if (config->enable_performance_monitoring) {
        setup_performance_monitoring();
    }
    
    latency_ctx.measurement_enabled = config->enable_measurement;
    latency_ctx.optimization_enabled = config->enable_optimization;
    latency_ctx.real_time_mode = config->real_time_mode;
    
    latency_ctx.global_min_latency = UINT64_MAX;
    latency_ctx.initialized = true;
    
    return INT_LATENCY_SUCCESS;
}

void interrupt_latency_measure_entry(uint8_t vector) {
    if (!latency_ctx.measurement_enabled || in_latency_measurement_context) {
        return;
    }
    
    interrupt_entry_time = rdtsc();
    in_latency_measurement_context = true;
    
    if (latency_ctx.optimization_enabled) {
        __builtin_prefetch(&latency_ctx.source_stats[0], 1, 3);
    }
}

void interrupt_latency_measure_exit(uint8_t vector) {
    if (!latency_ctx.measurement_enabled || !in_latency_measurement_context) {
        return;
    }
    
    uint64_t exit_time = rdtsc();
    uint64_t raw_latency = exit_time - interrupt_entry_time;
    
    if (raw_latency > latency_ctx.measurement_overhead) {
        raw_latency -= latency_ctx.measurement_overhead;
    }
    
    uint64_t latency_ns = (raw_latency * 1000000000ULL) / latency_ctx.tsc_frequency;
    uint64_t handler_duration_ns = latency_ns;
    
    size_t sample_index = latency_ctx.sample_index;
    latency_ctx.samples[sample_index] = (latency_sample_t){
        .entry_timestamp = interrupt_entry_time,
        .exit_timestamp = exit_time,
        .interrupt_latency = latency_ns,
        .handler_duration = handler_duration_ns,
        .vector = vector,
        .cpu_id = get_current_cpu_id(),
        .preempted = false
    };
    
    latency_ctx.sample_index = (sample_index + 1) % MAX_LATENCY_SAMPLES;
    if (latency_ctx.sample_count < MAX_LATENCY_SAMPLES) {
        latency_ctx.sample_count++;
    }
    
    interrupt_source_stats_t *stats = find_or_create_source_stats(vector);
    if (stats) {
        stats->sample_count++;
        stats->total_latency += latency_ns;
        
        if (latency_ns < stats->min_latency) {
            stats->min_latency = latency_ns;
        }
        if (latency_ns > stats->max_latency) {
            stats->max_latency = latency_ns;
        }
        stats->avg_latency = stats->total_latency / stats->sample_count;
        
        if (stats->deadline_ns > 0 && latency_ns > stats->deadline_ns) {
            stats->missed_deadlines++;
        }
        
        update_histogram(&stats->histogram, latency_ns);
    }
    
    if (latency_ns < latency_ctx.global_min_latency) {
        latency_ctx.global_min_latency = latency_ns;
    }
    if (latency_ns > latency_ctx.global_max_latency) {
        latency_ctx.global_max_latency = latency_ns;
    }
    
    uint64_t total_samples = 0;
    uint64_t total_avg = 0;
    for (size_t i = 0; i < latency_ctx.source_count; i++) {
        total_samples += latency_ctx.source_stats[i].sample_count;
        total_avg += latency_ctx.source_stats[i].avg_latency * 
                     latency_ctx.source_stats[i].sample_count;
    }
    if (total_samples > 0) {
        latency_ctx.global_avg_latency = total_avg / total_samples;
    }
    
    if (latency_ctx.optimization_enabled) {
        optimize_interrupt_delivery(vector);
    }
    
    in_latency_measurement_context = false;
}

interrupt_latency_error_t interrupt_latency_set_deadline(uint8_t vector, uint64_t deadline_ns) {
    if (!latency_ctx.initialized) {
        return INT_LATENCY_ERROR_NOT_INITIALIZED;
    }
    
    interrupt_source_stats_t *stats = find_or_create_source_stats(vector);
    if (!stats) {
        return INT_LATENCY_ERROR_NO_SPACE;
    }
    
    stats->deadline_ns = deadline_ns;
    stats->real_time_critical = deadline_ns < 100000;
    
    return INT_LATENCY_SUCCESS;
}

interrupt_latency_error_t interrupt_latency_get_statistics(
    uint8_t vector, interrupt_latency_stats_t *stats) {
    
    if (!latency_ctx.initialized || !stats) {
        return INT_LATENCY_ERROR_INVALID_PARAMS;
    }
    
    interrupt_source_stats_t *source_stats = NULL;
    for (size_t i = 0; i < latency_ctx.source_count; i++) {
        if (latency_ctx.source_stats[i].vector == vector) {
            source_stats = &latency_ctx.source_stats[i];
            break;
        }
    }
    
    if (!source_stats) {
        return INT_LATENCY_ERROR_NO_DATA;
    }
    
    stats->vector = vector;
    stats->min_latency_ns = source_stats->min_latency;
    stats->max_latency_ns = source_stats->max_latency;
    stats->avg_latency_ns = source_stats->avg_latency;
    stats->sample_count = source_stats->sample_count;
    stats->missed_deadlines = source_stats->missed_deadlines;
    stats->deadline_ns = source_stats->deadline_ns;
    stats->real_time_critical = source_stats->real_time_critical;
    
    stats->percentile_50ns = source_stats->avg_latency;
    stats->percentile_95ns = source_stats->max_latency * 0.95;
    stats->percentile_99ns = source_stats->max_latency * 0.99;
    
    return INT_LATENCY_SUCCESS;
}

interrupt_latency_error_t interrupt_latency_get_global_statistics(
    global_latency_stats_t *stats) {
    
    if (!latency_ctx.initialized || !stats) {
        return INT_LATENCY_ERROR_INVALID_PARAMS;
    }
    
    memset(stats, 0, sizeof(global_latency_stats_t));
    
    stats->global_min_latency_ns = latency_ctx.global_min_latency;
    stats->global_max_latency_ns = latency_ctx.global_max_latency;
    stats->global_avg_latency_ns = latency_ctx.global_avg_latency;
    stats->total_interrupts = latency_ctx.sample_count;
    stats->measurement_overhead_ns = (latency_ctx.measurement_overhead * 1000000000ULL) / 
                                    latency_ctx.tsc_frequency;
    
    uint32_t total_missed_deadlines = 0;
    for (size_t i = 0; i < latency_ctx.source_count; i++) {
        total_missed_deadlines += latency_ctx.source_stats[i].missed_deadlines;
    }
    stats->total_missed_deadlines = total_missed_deadlines;
    
    stats->active_interrupt_sources = latency_ctx.source_count;
    
    return INT_LATENCY_SUCCESS;
}

interrupt_latency_error_t interrupt_latency_enable_optimization(bool enable) {
    if (!latency_ctx.initialized) {
        return INT_LATENCY_ERROR_NOT_INITIALIZED;
    }
    
    latency_ctx.optimization_enabled = enable;
    
    if (enable) {
        for (size_t i = 0; i < latency_ctx.source_count; i++) {
            optimize_interrupt_delivery(latency_ctx.source_stats[i].vector);
        }
    }
    
    return INT_LATENCY_SUCCESS;
}

interrupt_latency_error_t interrupt_latency_benchmark(
    uint8_t vector, uint32_t iterations, benchmark_result_t *result) {
    
    if (!latency_ctx.initialized || !result || iterations == 0) {
        return INT_LATENCY_ERROR_INVALID_PARAMS;
    }
    
    memset(result, 0, sizeof(benchmark_result_t));
    
    uint64_t *latencies = malloc(iterations * sizeof(uint64_t));
    if (!latencies) {
        return INT_LATENCY_ERROR_NO_MEMORY;
    }
    
    __asm__ volatile ("cli");

    for (uint32_t i = 0; i < iterations; i++) {
        uint64_t start = rdtsc();

        /* Note: INT instruction requires constant operand, so we use int 0x80 for benchmarks */
        /* In practice, vector-specific benchmarks need separate functions per vector */
        (void)vector;  /* Acknowledge vector parameter - actual interrupt vector is fixed */
        __asm__ volatile ("int $0x80");  /* Use syscall vector for benchmark */

        uint64_t end = rdtsc();
        latencies[i] = end - start;
    }

    __asm__ volatile ("sti");
    
    uint64_t total_latency = 0;
    uint64_t min_latency = latencies[0];
    uint64_t max_latency = latencies[0];
    
    for (uint32_t i = 0; i < iterations; i++) {
        total_latency += latencies[i];
        if (latencies[i] < min_latency) min_latency = latencies[i];
        if (latencies[i] > max_latency) max_latency = latencies[i];
    }
    
    result->min_latency_cycles = min_latency;
    result->max_latency_cycles = max_latency;
    result->avg_latency_cycles = total_latency / iterations;
    result->min_latency_ns = (min_latency * 1000000000ULL) / latency_ctx.tsc_frequency;
    result->max_latency_ns = (max_latency * 1000000000ULL) / latency_ctx.tsc_frequency;
    result->avg_latency_ns = (result->avg_latency_cycles * 1000000000ULL) / latency_ctx.tsc_frequency;
    result->iterations = iterations;
    result->vector = vector;
    
    free(latencies);
    return INT_LATENCY_SUCCESS;
}

void interrupt_latency_reset_statistics(void) {
    if (!latency_ctx.initialized) {
        return;
    }
    
    memset(latency_ctx.samples, 0, sizeof(latency_ctx.samples));
    memset(latency_ctx.source_stats, 0, sizeof(latency_ctx.source_stats));
    
    latency_ctx.sample_index = 0;
    latency_ctx.sample_count = 0;
    latency_ctx.source_count = 0;
    latency_ctx.global_min_latency = UINT64_MAX;
    latency_ctx.global_max_latency = 0;
    latency_ctx.global_avg_latency = 0;
}

bool interrupt_latency_is_measurement_enabled(void) {
    return latency_ctx.measurement_enabled;
}

bool interrupt_latency_is_optimization_enabled(void) {
    return latency_ctx.optimization_enabled;
}

uint64_t interrupt_latency_get_measurement_overhead_ns(void) {
    if (!latency_ctx.initialized) {
        return 0;
    }
    
    return (latency_ctx.measurement_overhead * 1000000000ULL) / latency_ctx.tsc_frequency;
}