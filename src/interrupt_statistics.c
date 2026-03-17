/*
 * interrupt_statistics.c - Comprehensive Interrupt Statistics and Debugging Framework for Forest OS
 * 
 * This module provides:
 * - Real-time interrupt statistics collection and reporting
 * - Performance monitoring and latency analysis
 * - Interrupt tracing and debugging capabilities
 * - Historical data collection and trend analysis
 * - Per-CPU, per-vector, and system-wide metrics
 * - Integration with all interrupt subsystems
 * - Export capabilities for analysis tools
 * 
 * The framework collects detailed metrics on interrupt behavior to support
 * performance optimization, debugging, and capacity planning.
 * 
 * Copyright (c) 2024 Forest OS Project
 */

#include "interrupt.h"
#include "memory.h"
#include "smp.h"
#include "debug.h"
#include "time.h"
#include "atomic.h"
#include <string.h>

/* Statistics collection levels */
typedef enum {
    STATS_LEVEL_BASIC,      /* Basic counters only */
    STATS_LEVEL_DETAILED,   /* Detailed timing and latency */
    STATS_LEVEL_FULL,       /* Full tracing and analysis */
    STATS_LEVEL_DEBUG       /* Everything including debug traces */
} stats_level_t;

/* Interrupt event types for tracing */
typedef enum {
    INT_EVENT_ENTRY,        /* Interrupt entry */
    INT_EVENT_EXIT,         /* Interrupt exit */
    INT_EVENT_HANDLER_START, /* Handler execution start */
    INT_EVENT_HANDLER_END,   /* Handler execution end */
    INT_EVENT_NESTED,       /* Nested interrupt */
    INT_EVENT_SPURIOUS,     /* Spurious interrupt */
    INT_EVENT_ERROR,        /* Interrupt error */
    INT_EVENT_THROTTLED,    /* Interrupt throttled */
    INT_EVENT_COALESCED     /* Interrupt coalesced */
} int_event_type_t;

/* Latency histogram buckets */
#define LATENCY_BUCKETS 20
static const uint64_t latency_bucket_bounds[LATENCY_BUCKETS] = {
    100,     /* < 100ns */
    500,     /* < 500ns */
    1000,    /* < 1µs */
    2000,    /* < 2µs */
    5000,    /* < 5µs */
    10000,   /* < 10µs */
    20000,   /* < 20µs */
    50000,   /* < 50µs */
    100000,  /* < 100µs */
    200000,  /* < 200µs */
    500000,  /* < 500µs */
    1000000, /* < 1ms */
    2000000, /* < 2ms */
    5000000, /* < 5ms */
    10000000,/* < 10ms */
    20000000,/* < 20ms */
    50000000,/* < 50ms */
    100000000,/* < 100ms */
    200000000,/* < 200ms */
    UINT64_MAX /* >= 200ms */
};

/* Per-vector statistics */
struct interrupt_vector_stats {
    /* Basic counters */
    atomic64_t count;               /* Total interrupt count */
    atomic64_t handled;             /* Successfully handled */
    atomic64_t unhandled;           /* Unhandled interrupts */
    atomic64_t spurious;            /* Spurious interrupts */
    atomic64_t errors;              /* Error interrupts */
    atomic64_t nested;              /* Nested interrupts */
    atomic64_t throttled;           /* Throttled interrupts */
    atomic64_t coalesced;           /* Coalesced interrupts */
    
    /* Timing statistics */
    atomic64_t total_time_ns;       /* Total time spent in interrupt */
    atomic64_t min_latency_ns;      /* Minimum latency */
    atomic64_t max_latency_ns;      /* Maximum latency */
    atomic64_t handler_time_ns;     /* Total time in handler */
    uint64_t last_timestamp;        /* Last interrupt timestamp */
    
    /* Latency histogram */
    atomic64_t latency_histogram[LATENCY_BUCKETS];
    
    /* Rate tracking */
    uint32_t rate_per_second;       /* Current rate */
    uint32_t peak_rate;             /* Peak rate observed */
    uint64_t rate_window_start;     /* Rate measurement window start */
    uint32_t rate_window_count;     /* Count in current window */
    
    /* Quality metrics */
    uint32_t reliability_score;     /* 0-100 reliability score */
    uint32_t consecutive_errors;    /* Consecutive error count */
    bool blacklisted;               /* Vector blacklisted due to errors */
    
    /* Names and identification */
    char name[32];                  /* Vector name */
    char handler_name[32];          /* Handler name */
} __attribute__((aligned(64)));     /* Cache line aligned */

/* Per-CPU interrupt statistics */
struct cpu_interrupt_stats {
    /* Per-CPU counters */
    atomic64_t total_interrupts;    /* Total interrupts on this CPU */
    atomic64_t nested_interrupts;   /* Nested interrupt count */
    atomic64_t context_switches;    /* Interrupt context switches */
    atomic64_t preemptions;         /* Preemption count */
    atomic64_t ipi_sent;            /* IPIs sent from this CPU */
    atomic64_t ipi_received;        /* IPIs received on this CPU */
    
    /* CPU timing */
    uint64_t interrupt_time_ns;     /* Total time in interrupts */
    uint64_t max_interrupt_time_ns; /* Maximum single interrupt time */
    uint64_t last_interrupt_entry;  /* Last interrupt entry time */
    uint32_t current_nesting_level; /* Current nesting level */
    uint32_t max_nesting_level;     /* Maximum nesting level reached */
    
    /* Load metrics */
    uint32_t interrupt_load_percent; /* Percentage of time in interrupts */
    uint64_t load_measurement_start; /* Load measurement period start */
    uint64_t load_interrupt_time;    /* Interrupt time in measurement period */
    
    /* Error tracking */
    atomic64_t double_faults;       /* Double fault count */
    atomic64_t nmi_count;           /* NMI count */
    atomic64_t machine_checks;      /* Machine check count */
    
    /* Hardware-specific */
    atomic64_t pic_interrupts;      /* PIC interrupts */
    atomic64_t apic_interrupts;     /* APIC interrupts */
    atomic64_t msi_interrupts;      /* MSI interrupts */
    atomic64_t msix_interrupts;     /* MSI-X interrupts */
} __attribute__((aligned(64)));

/* System-wide interrupt statistics */
struct system_interrupt_stats {
    /* Global counters */
    atomic64_t total_interrupts;    /* System-wide interrupt count */
    atomic64_t total_time_ns;       /* Total time spent in interrupts */
    atomic64_t spurious_total;      /* Total spurious interrupts */
    atomic64_t error_total;         /* Total error interrupts */
    
    /* System timing */
    uint64_t boot_time;             /* Boot time timestamp */
    uint64_t uptime_ns;             /* System uptime in nanoseconds */
    uint32_t avg_interrupt_rate;    /* Average interrupt rate */
    uint32_t peak_interrupt_rate;   /* Peak interrupt rate */
    
    /* Load and performance */
    uint32_t system_interrupt_load; /* System interrupt load percentage */
    uint32_t busiest_cpu;           /* CPU with highest interrupt load */
    uint32_t quietest_cpu;          /* CPU with lowest interrupt load */
    
    /* Quality metrics */
    uint32_t system_reliability;    /* Overall system reliability score */
    uint64_t last_major_event;      /* Last major interrupt event */
    uint32_t blacklisted_vectors;   /* Number of blacklisted vectors */
};

/* Interrupt trace entry */
struct interrupt_trace_entry {
    uint64_t timestamp;             /* Event timestamp */
    uint32_t cpu;                   /* CPU number */
    uint32_t vector;                /* Interrupt vector */
    int_event_type_t event_type;    /* Event type */
    uint32_t nesting_level;         /* Nesting level at time of event */
    uint64_t duration_ns;           /* Duration (for exit events) */
    uint64_t context_data;          /* Additional context data */
} __attribute__((packed));

/* Trace buffer management */
struct interrupt_trace {
    struct interrupt_trace_entry *entries;
    uint32_t size;                  /* Buffer size in entries */
    atomic_t head;                  /* Head pointer */
    atomic_t tail;                  /* Tail pointer */
    atomic64_t dropped_entries;     /* Number of dropped entries */
    bool circular;                  /* Circular buffer mode */
    bool enabled;                   /* Tracing enabled */
    spinlock_t lock;                /* Protects buffer operations */
};

/* Main statistics management structure */
struct interrupt_stats_manager {
    /* Per-vector statistics */
    struct interrupt_vector_stats vector_stats[256];
    
    /* Per-CPU statistics */
    struct cpu_interrupt_stats cpu_stats[NR_CPUS];
    
    /* System-wide statistics */
    struct system_interrupt_stats system_stats;
    
    /* Tracing */
    struct interrupt_trace trace;
    
    /* Configuration */
    struct {
        stats_level_t level;
        bool enabled;
        bool auto_blacklist;
        uint32_t rate_window_ms;
        uint32_t reliability_threshold;
        uint32_t error_threshold;
        bool export_enabled;
    } config;
    
    /* Export and analysis */
    struct {
        uint64_t last_export_time;
        uint32_t export_interval_ms;
        char export_buffer[4096];
        bool export_pending;
    } export;
    
    bool initialized;
    spinlock_t lock;
};

static struct interrupt_stats_manager stats_mgr = {0};

/* Forward declarations */
static void stats_update_vector_timing(int vector, uint64_t start_time, uint64_t end_time);
static void stats_update_cpu_load(int cpu);
static void stats_update_system_metrics(void);
static void stats_trace_event(int vector, int_event_type_t event_type, uint64_t duration);
static void stats_check_reliability(int vector);
static void stats_update_latency_histogram(int vector, uint64_t latency_ns);
static void stats_periodic_update(void);
static uint32_t stats_calculate_reliability_score(struct interrupt_vector_stats *vstats);

/* ===========================
 * INITIALIZATION
 * =========================== */

/**
 * Initialize interrupt statistics framework
 */
int interrupt_statistics_init(void)
{
    int i;
    
    if (stats_mgr.initialized) {
        return 0;
    }
    
    memset(&stats_mgr, 0, sizeof(stats_mgr));
    spinlock_init(&stats_mgr.lock, "interrupt_stats");
    spinlock_init(&stats_mgr.trace.lock, "interrupt_trace");
    
    /* Initialize per-vector statistics */
    for (i = 0; i < 256; i++) {
        struct interrupt_vector_stats *vstats = &stats_mgr.vector_stats[i];
        
        atomic64_set(&vstats->count, 0);
        atomic64_set(&vstats->handled, 0);
        atomic64_set(&vstats->unhandled, 0);
        atomic64_set(&vstats->spurious, 0);
        atomic64_set(&vstats->errors, 0);
        atomic64_set(&vstats->nested, 0);
        atomic64_set(&vstats->throttled, 0);
        atomic64_set(&vstats->coalesced, 0);
        
        atomic64_set(&vstats->total_time_ns, 0);
        atomic64_set(&vstats->min_latency_ns, UINT64_MAX);
        atomic64_set(&vstats->max_latency_ns, 0);
        atomic64_set(&vstats->handler_time_ns, 0);
        
        for (int j = 0; j < LATENCY_BUCKETS; j++) {
            atomic64_set(&vstats->latency_histogram[j], 0);
        }
        
        vstats->reliability_score = 100;
        vstats->rate_window_start = get_system_time_ns();
        
        snprintf(vstats->name, sizeof(vstats->name), "Vector%d", i);
        snprintf(vstats->handler_name, sizeof(vstats->handler_name), "Unknown");
    }
    
    /* Initialize per-CPU statistics */
    for (i = 0; i < NR_CPUS; i++) {
        struct cpu_interrupt_stats *cstats = &stats_mgr.cpu_stats[i];
        
        atomic64_set(&cstats->total_interrupts, 0);
        atomic64_set(&cstats->nested_interrupts, 0);
        atomic64_set(&cstats->context_switches, 0);
        atomic64_set(&cstats->preemptions, 0);
        atomic64_set(&cstats->ipi_sent, 0);
        atomic64_set(&cstats->ipi_received, 0);
        atomic64_set(&cstats->double_faults, 0);
        atomic64_set(&cstats->nmi_count, 0);
        atomic64_set(&cstats->machine_checks, 0);
        atomic64_set(&cstats->pic_interrupts, 0);
        atomic64_set(&cstats->apic_interrupts, 0);
        atomic64_set(&cstats->msi_interrupts, 0);
        atomic64_set(&cstats->msix_interrupts, 0);
        
        cstats->load_measurement_start = get_system_time_ns();
    }
    
    /* Initialize system statistics */
    atomic64_set(&stats_mgr.system_stats.total_interrupts, 0);
    atomic64_set(&stats_mgr.system_stats.total_time_ns, 0);
    atomic64_set(&stats_mgr.system_stats.spurious_total, 0);
    atomic64_set(&stats_mgr.system_stats.error_total, 0);
    
    stats_mgr.system_stats.boot_time = get_system_time_ns();
    stats_mgr.system_stats.system_reliability = 100;
    
    /* Initialize trace buffer */
    stats_mgr.trace.size = 4096; /* 4K trace entries */
    stats_mgr.trace.entries = (struct interrupt_trace_entry *)
        kmalloc(stats_mgr.trace.size * sizeof(struct interrupt_trace_entry));
    
    if (!stats_mgr.trace.entries) {
        debug_printf("Warning: Could not allocate interrupt trace buffer\n");
        stats_mgr.trace.size = 0;
    } else {
        memset(stats_mgr.trace.entries, 0, 
               stats_mgr.trace.size * sizeof(struct interrupt_trace_entry));
        atomic_set(&stats_mgr.trace.head, 0);
        atomic_set(&stats_mgr.trace.tail, 0);
        atomic64_set(&stats_mgr.trace.dropped_entries, 0);
        stats_mgr.trace.circular = true;
        stats_mgr.trace.enabled = false; /* Enable when needed */
    }
    
    /* Set default configuration */
    stats_mgr.config.level = STATS_LEVEL_DETAILED;
    stats_mgr.config.enabled = true;
    stats_mgr.config.auto_blacklist = true;
    stats_mgr.config.rate_window_ms = 1000;
    stats_mgr.config.reliability_threshold = 80;
    stats_mgr.config.error_threshold = 10;
    stats_mgr.config.export_enabled = false;
    
    stats_mgr.export.export_interval_ms = 60000; /* 1 minute */
    stats_mgr.export.last_export_time = get_system_time_ns();
    
    stats_mgr.initialized = true;
    
    debug_printf("Interrupt statistics framework initialized\n");
    debug_printf("Level: %d, Trace buffer: %u entries\n", 
                stats_mgr.config.level, stats_mgr.trace.size);
    
    return 0;
}

/**
 * Cleanup interrupt statistics framework
 */
void interrupt_statistics_cleanup(void)
{
    if (!stats_mgr.initialized) {
        return;
    }
    
    if (stats_mgr.trace.entries) {
        kfree(stats_mgr.trace.entries);
        stats_mgr.trace.entries = NULL;
    }
    
    stats_mgr.initialized = false;
    debug_printf("Interrupt statistics framework cleaned up\n");
}

/* ===========================
 * STATISTICS COLLECTION
 * =========================== */

/**
 * Record interrupt entry
 */
void interrupt_stats_entry(int vector, uint64_t timestamp)
{
    struct interrupt_vector_stats *vstats;
    struct cpu_interrupt_stats *cstats;
    int cpu;
    
    if (!stats_mgr.initialized || !stats_mgr.config.enabled || 
        vector < 0 || vector >= 256) {
        return;
    }
    
    cpu = smp_get_current_cpu();
    vstats = &stats_mgr.vector_stats[vector];
    cstats = &stats_mgr.cpu_stats[cpu];
    
    /* Update basic counters */
    atomic64_inc(&vstats->count);
    atomic64_inc(&cstats->total_interrupts);
    atomic64_inc(&stats_mgr.system_stats.total_interrupts);
    
    /* Update timing */
    vstats->last_timestamp = timestamp;
    cstats->last_interrupt_entry = timestamp;
    
    /* Update nesting level */
    cstats->current_nesting_level++;
    if (cstats->current_nesting_level > cstats->max_nesting_level) {
        cstats->max_nesting_level = cstats->current_nesting_level;
    }
    
    if (cstats->current_nesting_level > 1) {
        atomic64_inc(&vstats->nested);
        atomic64_inc(&cstats->nested_interrupts);
    }
    
    /* Update rate tracking */
    vstats->rate_window_count++;
    
    /* Trace event if enabled */
    if (stats_mgr.trace.enabled) {
        stats_trace_event(vector, INT_EVENT_ENTRY, 0);
    }
    
    if (stats_mgr.config.level >= STATS_LEVEL_DEBUG) {
        debug_printf("INT_ENTRY: vector=%d, cpu=%d, nest=%d, ts=%llu\n",
                    vector, cpu, cstats->current_nesting_level, timestamp);
    }
}

/**
 * Record interrupt exit
 */
void interrupt_stats_exit(int vector, uint64_t timestamp, irq_return_t result)
{
    struct interrupt_vector_stats *vstats;
    struct cpu_interrupt_stats *cstats;
    int cpu;
    uint64_t duration;
    
    if (!stats_mgr.initialized || !stats_mgr.config.enabled || 
        vector < 0 || vector >= 256) {
        return;
    }
    
    cpu = smp_get_current_cpu();
    vstats = &stats_mgr.vector_stats[vector];
    cstats = &stats_mgr.cpu_stats[cpu];
    
    /* Calculate duration */
    if (cstats->last_interrupt_entry > 0) {
        duration = timestamp - cstats->last_interrupt_entry;
    } else {
        duration = 0;
    }
    
    /* Update timing statistics */
    if (duration > 0) {
        stats_update_vector_timing(vector, cstats->last_interrupt_entry, timestamp);
        
        cstats->interrupt_time_ns += duration;
        if (duration > cstats->max_interrupt_time_ns) {
            cstats->max_interrupt_time_ns = duration;
        }
    }
    
    /* Update result-based counters */
    switch (result) {
        case IRQ_HANDLED:
            atomic64_inc(&vstats->handled);
            break;
        case IRQ_NONE:
            atomic64_inc(&vstats->unhandled);
            break;
        default:
            atomic64_inc(&vstats->errors);
            vstats->consecutive_errors++;
            atomic64_inc(&stats_mgr.system_stats.error_total);
            break;
    }
    
    /* Update nesting level */
    if (cstats->current_nesting_level > 0) {
        cstats->current_nesting_level--;
    }
    
    /* Check reliability */
    if (stats_mgr.config.auto_blacklist) {
        stats_check_reliability(vector);
    }
    
    /* Trace event if enabled */
    if (stats_mgr.trace.enabled) {
        stats_trace_event(vector, INT_EVENT_EXIT, duration);
    }
    
    if (stats_mgr.config.level >= STATS_LEVEL_DEBUG) {
        debug_printf("INT_EXIT: vector=%d, cpu=%d, result=%d, duration=%llu ns\n",
                    vector, cpu, result, duration);
    }
}

/**
 * Record handler execution timing
 */
void interrupt_stats_handler_timing(int vector, uint64_t start_time, uint64_t end_time)
{
    struct interrupt_vector_stats *vstats;
    uint64_t handler_duration;
    
    if (!stats_mgr.initialized || !stats_mgr.config.enabled || 
        vector < 0 || vector >= 256 || end_time <= start_time) {
        return;
    }
    
    vstats = &stats_mgr.vector_stats[vector];
    handler_duration = end_time - start_time;
    
    atomic64_add(&vstats->handler_time_ns, handler_duration);
    
    /* Reset consecutive error count on successful handling */
    vstats->consecutive_errors = 0;
    
    /* Trace handler events if enabled */
    if (stats_mgr.trace.enabled) {
        stats_trace_event(vector, INT_EVENT_HANDLER_START, 0);
        stats_trace_event(vector, INT_EVENT_HANDLER_END, handler_duration);
    }
}

/**
 * Record spurious interrupt
 */
void interrupt_stats_spurious(int vector)
{
    struct interrupt_vector_stats *vstats;
    
    if (!stats_mgr.initialized || !stats_mgr.config.enabled || 
        vector < 0 || vector >= 256) {
        return;
    }
    
    vstats = &stats_mgr.vector_stats[vector];
    
    atomic64_inc(&vstats->spurious);
    atomic64_inc(&stats_mgr.system_stats.spurious_total);
    
    vstats->consecutive_errors++;
    
    /* Trace spurious event */
    if (stats_mgr.trace.enabled) {
        stats_trace_event(vector, INT_EVENT_SPURIOUS, 0);
    }
    
    if (stats_mgr.config.level >= STATS_LEVEL_DETAILED) {
        debug_printf("SPURIOUS: vector=%d, total=%llu\n", 
                    vector, atomic64_read(&vstats->spurious));
    }
}

/**
 * Record interrupt throttling
 */
void interrupt_stats_throttled(int vector)
{
    struct interrupt_vector_stats *vstats;
    
    if (!stats_mgr.initialized || vector < 0 || vector >= 256) {
        return;
    }
    
    vstats = &stats_mgr.vector_stats[vector];
    atomic64_inc(&vstats->throttled);
    
    if (stats_mgr.trace.enabled) {
        stats_trace_event(vector, INT_EVENT_THROTTLED, 0);
    }
}

/**
 * Record interrupt coalescing
 */
void interrupt_stats_coalesced(int vector, uint32_t count)
{
    struct interrupt_vector_stats *vstats;
    
    if (!stats_mgr.initialized || vector < 0 || vector >= 256) {
        return;
    }
    
    vstats = &stats_mgr.vector_stats[vector];
    atomic64_add(&vstats->coalesced, count);
    
    if (stats_mgr.trace.enabled) {
        stats_trace_event(vector, INT_EVENT_COALESCED, count);
    }
}

/* ===========================
 * STATISTICS ANALYSIS
 * =========================== */

/**
 * Update vector timing statistics
 */
static void stats_update_vector_timing(int vector, uint64_t start_time, uint64_t end_time)
{
    struct interrupt_vector_stats *vstats;
    uint64_t latency;
    
    vstats = &stats_mgr.vector_stats[vector];
    latency = end_time - start_time;
    
    /* Update min/max latency */
    uint64_t current_min = atomic64_read(&vstats->min_latency_ns);
    if (latency < current_min) {
        atomic64_set(&vstats->min_latency_ns, latency);
    }
    
    uint64_t current_max = atomic64_read(&vstats->max_latency_ns);
    if (latency > current_max) {
        atomic64_set(&vstats->max_latency_ns, latency);
    }
    
    /* Update total time */
    atomic64_add(&vstats->total_time_ns, latency);
    atomic64_add(&stats_mgr.system_stats.total_time_ns, latency);
    
    /* Update latency histogram */
    if (stats_mgr.config.level >= STATS_LEVEL_DETAILED) {
        stats_update_latency_histogram(vector, latency);
    }
}

/**
 * Update latency histogram
 */
static void stats_update_latency_histogram(int vector, uint64_t latency_ns)
{
    struct interrupt_vector_stats *vstats;
    int bucket;
    
    vstats = &stats_mgr.vector_stats[vector];
    
    /* Find appropriate bucket */
    for (bucket = 0; bucket < LATENCY_BUCKETS; bucket++) {
        if (latency_ns < latency_bucket_bounds[bucket]) {
            break;
        }
    }
    
    if (bucket >= LATENCY_BUCKETS) {
        bucket = LATENCY_BUCKETS - 1;
    }
    
    atomic64_inc(&vstats->latency_histogram[bucket]);
}

/**
 * Update CPU load statistics
 */
static void stats_update_cpu_load(int cpu)
{
    struct cpu_interrupt_stats *cstats;
    uint64_t current_time, measurement_period;
    uint32_t load_percent;
    
    if (cpu >= NR_CPUS) {
        return;
    }
    
    cstats = &stats_mgr.cpu_stats[cpu];
    current_time = get_system_time_ns();
    
    measurement_period = current_time - cstats->load_measurement_start;
    if (measurement_period < 1000000000ULL) {  /* Less than 1 second */
        return;
    }
    
    /* Calculate interrupt load percentage */
    if (measurement_period > 0) {
        load_percent = (uint32_t)((cstats->load_interrupt_time * 100) / measurement_period);
        cstats->interrupt_load_percent = load_percent;
    }
    
    /* Reset measurement window */
    cstats->load_measurement_start = current_time;
    cstats->load_interrupt_time = 0;
}

/**
 * Update system-wide metrics
 */
static void stats_update_system_metrics(void)
{
    uint32_t busiest_cpu = 0, quietest_cpu = 0;
    uint32_t max_load = 0, min_load = 100;
    uint32_t total_load = 0;
    int cpu;
    
    /* Find busiest and quietest CPUs */
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        uint32_t load = stats_mgr.cpu_stats[cpu].interrupt_load_percent;
        
        if (load > max_load) {
            max_load = load;
            busiest_cpu = cpu;
        }
        
        if (load < min_load) {
            min_load = load;
            quietest_cpu = cpu;
        }
        
        total_load += load;
    }
    
    stats_mgr.system_stats.busiest_cpu = busiest_cpu;
    stats_mgr.system_stats.quietest_cpu = quietest_cpu;
    stats_mgr.system_stats.system_interrupt_load = total_load / NR_CPUS;
    
    /* Update uptime */
    stats_mgr.system_stats.uptime_ns = get_system_time_ns() - stats_mgr.system_stats.boot_time;
    
    /* Calculate average interrupt rate */
    if (stats_mgr.system_stats.uptime_ns > 0) {
        uint64_t total_interrupts = atomic64_read(&stats_mgr.system_stats.total_interrupts);
        stats_mgr.system_stats.avg_interrupt_rate = 
            (uint32_t)((total_interrupts * 1000000000ULL) / stats_mgr.system_stats.uptime_ns);
    }
}

/**
 * Check vector reliability and blacklist if necessary
 */
static void stats_check_reliability(int vector)
{
    struct interrupt_vector_stats *vstats;
    uint32_t reliability;
    
    vstats = &stats_mgr.vector_stats[vector];
    
    /* Calculate reliability score */
    reliability = stats_calculate_reliability_score(vstats);
    vstats->reliability_score = reliability;
    
    /* Blacklist if reliability is too low */
    if (reliability < stats_mgr.config.reliability_threshold || 
        vstats->consecutive_errors >= stats_mgr.config.error_threshold) {
        
        if (!vstats->blacklisted) {
            vstats->blacklisted = true;
            stats_mgr.system_stats.blacklisted_vectors++;
            
            debug_printf("BLACKLISTED: Vector %d (reliability=%u%%, errors=%u)\n",
                        vector, reliability, vstats->consecutive_errors);
        }
    }
}

/**
 * Calculate reliability score for a vector
 */
static uint32_t stats_calculate_reliability_score(struct interrupt_vector_stats *vstats)
{
    uint64_t total = atomic64_read(&vstats->count);
    uint64_t handled = atomic64_read(&vstats->handled);
    uint64_t spurious = atomic64_read(&vstats->spurious);
    uint64_t errors = atomic64_read(&vstats->errors);
    
    if (total == 0) {
        return 100;
    }
    
    /* Simple reliability calculation */
    uint64_t successful = handled;
    uint64_t problematic = spurious + errors;
    
    if (problematic >= total) {
        return 0;
    }
    
    return (uint32_t)((successful * 100) / total);
}

/* ===========================
 * TRACING SUPPORT
 * =========================== */

/**
 * Add trace event
 */
static void stats_trace_event(int vector, int_event_type_t event_type, uint64_t duration)
{
    struct interrupt_trace_entry *entry;
    uint32_t head, next_head;
    int cpu;
    
    if (!stats_mgr.trace.enabled || !stats_mgr.trace.entries) {
        return;
    }
    
    cpu = smp_get_current_cpu();
    
    /* Get next head position */
    head = atomic_read(&stats_mgr.trace.head);
    next_head = (head + 1) % stats_mgr.trace.size;
    
    /* Check if buffer is full */
    if (next_head == atomic_read(&stats_mgr.trace.tail)) {
        if (stats_mgr.trace.circular) {
            /* Advance tail in circular mode */
            atomic_set(&stats_mgr.trace.tail, (atomic_read(&stats_mgr.trace.tail) + 1) % stats_mgr.trace.size);
        } else {
            /* Drop entry in linear mode */
            atomic64_inc(&stats_mgr.trace.dropped_entries);
            return;
        }
    }
    
    /* Fill trace entry */
    entry = &stats_mgr.trace.entries[head];
    entry->timestamp = get_system_time_ns();
    entry->cpu = cpu;
    entry->vector = vector;
    entry->event_type = event_type;
    entry->nesting_level = stats_mgr.cpu_stats[cpu].current_nesting_level;
    entry->duration_ns = duration;
    entry->context_data = 0; /* Could be extended for more context */
    
    /* Update head pointer */
    atomic_set(&stats_mgr.trace.head, next_head);
}

/**
 * Enable/disable interrupt tracing
 */
void interrupt_stats_trace_enable(bool enable)
{
    unsigned long flags;
    
    if (!stats_mgr.initialized) {
        return;
    }
    
    spin_lock_irqsave(&stats_mgr.trace.lock, flags);
    stats_mgr.trace.enabled = enable;
    
    if (enable) {
        /* Reset trace buffer */
        atomic_set(&stats_mgr.trace.head, 0);
        atomic_set(&stats_mgr.trace.tail, 0);
        atomic64_set(&stats_mgr.trace.dropped_entries, 0);
    }
    
    spin_unlock_irqrestore(&stats_mgr.trace.lock, flags);
    
    debug_printf("Interrupt tracing %s\n", enable ? "enabled" : "disabled");
}

/* ===========================
 * PERIODIC UPDATES
 * =========================== */

/**
 * Periodic statistics update function
 */
static void stats_periodic_update(void)
{
    static uint64_t last_update_time = 0;
    uint64_t current_time = get_system_time_ns();
    int vector, cpu;
    
    /* Update every second */
    if (last_update_time == 0) {
        last_update_time = current_time;
        return;
    }
    
    if (current_time - last_update_time < 1000000000ULL) {
        return;
    }
    
    last_update_time = current_time;
    
    /* Update per-vector rates */
    for (vector = 0; vector < 256; vector++) {
        struct interrupt_vector_stats *vstats = &stats_mgr.vector_stats[vector];
        uint64_t window_duration = current_time - vstats->rate_window_start;
        
        if (window_duration >= 1000000000ULL) {  /* 1 second window */
            vstats->rate_per_second = (uint32_t)((vstats->rate_window_count * 1000000000ULL) / window_duration);
            
            if (vstats->rate_per_second > vstats->peak_rate) {
                vstats->peak_rate = vstats->rate_per_second;
            }
            
            /* Update peak system rate */
            if (vstats->rate_per_second > stats_mgr.system_stats.peak_interrupt_rate) {
                stats_mgr.system_stats.peak_interrupt_rate = vstats->rate_per_second;
            }
            
            /* Reset window */
            vstats->rate_window_start = current_time;
            vstats->rate_window_count = 0;
        }
    }
    
    /* Update per-CPU load metrics */
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        stats_update_cpu_load(cpu);
    }
    
    /* Update system-wide metrics */
    stats_update_system_metrics();
}

/**
 * Main periodic function to be called by system timer
 */
void interrupt_stats_periodic_update(void)
{
    if (stats_mgr.initialized && stats_mgr.config.enabled) {
        stats_periodic_update();
    }
}

/* ===========================
 * PUBLIC API
 * =========================== */

/**
 * Set vector name for statistics
 */
void interrupt_stats_set_vector_name(int vector, const char *name)
{
    if (!stats_mgr.initialized || vector < 0 || vector >= 256 || !name) {
        return;
    }
    
    strncpy(stats_mgr.vector_stats[vector].name, name, 
            sizeof(stats_mgr.vector_stats[vector].name) - 1);
    stats_mgr.vector_stats[vector].name[sizeof(stats_mgr.vector_stats[vector].name) - 1] = '\0';
}

/**
 * Set handler name for statistics
 */
void interrupt_stats_set_handler_name(int vector, const char *handler_name)
{
    if (!stats_mgr.initialized || vector < 0 || vector >= 256 || !handler_name) {
        return;
    }
    
    strncpy(stats_mgr.vector_stats[vector].handler_name, handler_name, 
            sizeof(stats_mgr.vector_stats[vector].handler_name) - 1);
    stats_mgr.vector_stats[vector].handler_name[sizeof(stats_mgr.vector_stats[vector].handler_name) - 1] = '\0';
}

/**
 * Get vector statistics
 */
int interrupt_stats_get_vector(int vector, struct interrupt_vector_stats *stats)
{
    if (!stats_mgr.initialized || vector < 0 || vector >= 256 || !stats) {
        return -EINVAL;
    }
    
    memcpy(stats, &stats_mgr.vector_stats[vector], sizeof(*stats));
    return 0;
}

/**
 * Get CPU statistics
 */
int interrupt_stats_get_cpu(int cpu, struct cpu_interrupt_stats *stats)
{
    if (!stats_mgr.initialized || cpu < 0 || cpu >= NR_CPUS || !stats) {
        return -EINVAL;
    }
    
    memcpy(stats, &stats_mgr.cpu_stats[cpu], sizeof(*stats));
    return 0;
}

/**
 * Get system statistics
 */
void interrupt_stats_get_system(struct system_interrupt_stats *stats)
{
    if (!stats_mgr.initialized || !stats) {
        return;
    }
    
    memcpy(stats, &stats_mgr.system_stats, sizeof(*stats));
}

/**
 * Configure statistics collection
 */
int interrupt_stats_configure(stats_level_t level, bool auto_blacklist, 
                             uint32_t reliability_threshold)
{
    if (!stats_mgr.initialized) {
        return -ENODEV;
    }
    
    stats_mgr.config.level = level;
    stats_mgr.config.auto_blacklist = auto_blacklist;
    stats_mgr.config.reliability_threshold = reliability_threshold;
    
    debug_printf("Interrupt statistics configured: level=%d, auto_blacklist=%s, threshold=%u\n",
                level, auto_blacklist ? "yes" : "no", reliability_threshold);
    
    return 0;
}

/**
 * Dump comprehensive statistics
 */
void interrupt_stats_dump_all(void)
{
    int vector, cpu;
    uint64_t total_interrupts, total_time;
    uint32_t active_vectors = 0;
    
    if (!stats_mgr.initialized) {
        debug_printf("Interrupt statistics not initialized\n");
        return;
    }
    
    /* Force periodic update */
    stats_periodic_update();
    
    debug_printf("=== Comprehensive Interrupt Statistics ===\n");
    
    /* System-wide statistics */
    total_interrupts = atomic64_read(&stats_mgr.system_stats.total_interrupts);
    total_time = atomic64_read(&stats_mgr.system_stats.total_time_ns);
    
    debug_printf("System Statistics:\n");
    debug_printf("  Total interrupts: %llu\n", total_interrupts);
    debug_printf("  Total time: %llu ns (%llu ms)\n", total_time, total_time / 1000000);
    debug_printf("  Uptime: %llu ns (%llu seconds)\n", 
                stats_mgr.system_stats.uptime_ns, stats_mgr.system_stats.uptime_ns / 1000000000ULL);
    debug_printf("  Average rate: %u interrupts/second\n", stats_mgr.system_stats.avg_interrupt_rate);
    debug_printf("  Peak rate: %u interrupts/second\n", stats_mgr.system_stats.peak_interrupt_rate);
    debug_printf("  System load: %u%% interrupt overhead\n", stats_mgr.system_stats.system_interrupt_load);
    debug_printf("  Busiest CPU: %u, Quietest CPU: %u\n", 
                stats_mgr.system_stats.busiest_cpu, stats_mgr.system_stats.quietest_cpu);
    debug_printf("  Spurious total: %llu\n", atomic64_read(&stats_mgr.system_stats.spurious_total));
    debug_printf("  Error total: %llu\n", atomic64_read(&stats_mgr.system_stats.error_total));
    debug_printf("  Reliability: %u%%\n", stats_mgr.system_stats.system_reliability);
    debug_printf("  Blacklisted vectors: %u\n", stats_mgr.system_stats.blacklisted_vectors);
    
    /* Per-CPU statistics */
    debug_printf("\nPer-CPU Statistics:\n");
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        struct cpu_interrupt_stats *cstats = &stats_mgr.cpu_stats[cpu];
        debug_printf("  CPU %d: %llu interrupts, %u%% load, max nest: %u\n",
                    cpu, atomic64_read(&cstats->total_interrupts),
                    cstats->interrupt_load_percent, cstats->max_nesting_level);
    }
    
    /* Active vectors */
    debug_printf("\nActive Interrupt Vectors:\n");
    for (vector = 0; vector < 256; vector++) {
        struct interrupt_vector_stats *vstats = &stats_mgr.vector_stats[vector];
        uint64_t count = atomic64_read(&vstats->count);
        
        if (count > 0) {
            active_vectors++;
            uint64_t avg_latency = 0;
            if (count > 0 && atomic64_read(&vstats->total_time_ns) > 0) {
                avg_latency = atomic64_read(&vstats->total_time_ns) / count;
            }
            
            debug_printf("  Vector %3d (%s): %llu interrupts, %u/sec, %llu ns avg\n",
                        vector, vstats->name, count, vstats->rate_per_second, avg_latency);
            debug_printf("    Handled: %llu, Unhandled: %llu, Spurious: %llu, Errors: %llu\n",
                        atomic64_read(&vstats->handled), atomic64_read(&vstats->unhandled),
                        atomic64_read(&vstats->spurious), atomic64_read(&vstats->errors));
            debug_printf("    Latency: min=%llu ns, max=%llu ns, reliability=%u%%\n",
                        atomic64_read(&vstats->min_latency_ns), atomic64_read(&vstats->max_latency_ns),
                        vstats->reliability_score);
            
            if (vstats->blacklisted) {
                debug_printf("    *** BLACKLISTED ***\n");
            }
        }
    }
    
    debug_printf("\nSummary: %u active vectors, %llu total interrupts\n", 
                active_vectors, total_interrupts);
    
    /* Trace buffer status */
    if (stats_mgr.trace.entries) {
        debug_printf("Trace buffer: %u entries, %llu dropped, %s\n",
                    stats_mgr.trace.size, atomic64_read(&stats_mgr.trace.dropped_entries),
                    stats_mgr.trace.enabled ? "enabled" : "disabled");
    }
}

/**
 * Enable or disable statistics debugging
 */
void interrupt_stats_debug_enable(bool enable)
{
    if (enable) {
        stats_mgr.config.level = STATS_LEVEL_DEBUG;
    } else if (stats_mgr.config.level == STATS_LEVEL_DEBUG) {
        stats_mgr.config.level = STATS_LEVEL_DETAILED;
    }
    
    debug_printf("Interrupt statistics debugging %s\n", enable ? "enabled" : "disabled");
}

/**
 * Reset all statistics
 */
void interrupt_stats_reset(void)
{
    int vector, cpu;
    
    if (!stats_mgr.initialized) {
        return;
    }
    
    /* Reset per-vector statistics */
    for (vector = 0; vector < 256; vector++) {
        struct interrupt_vector_stats *vstats = &stats_mgr.vector_stats[vector];
        
        atomic64_set(&vstats->count, 0);
        atomic64_set(&vstats->handled, 0);
        atomic64_set(&vstats->unhandled, 0);
        atomic64_set(&vstats->spurious, 0);
        atomic64_set(&vstats->errors, 0);
        atomic64_set(&vstats->nested, 0);
        atomic64_set(&vstats->throttled, 0);
        atomic64_set(&vstats->coalesced, 0);
        atomic64_set(&vstats->total_time_ns, 0);
        atomic64_set(&vstats->min_latency_ns, UINT64_MAX);
        atomic64_set(&vstats->max_latency_ns, 0);
        atomic64_set(&vstats->handler_time_ns, 0);
        
        for (int j = 0; j < LATENCY_BUCKETS; j++) {
            atomic64_set(&vstats->latency_histogram[j], 0);
        }
        
        vstats->rate_per_second = 0;
        vstats->peak_rate = 0;
        vstats->rate_window_start = get_system_time_ns();
        vstats->rate_window_count = 0;
        vstats->reliability_score = 100;
        vstats->consecutive_errors = 0;
        vstats->blacklisted = false;
    }
    
    /* Reset per-CPU statistics */
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        struct cpu_interrupt_stats *cstats = &stats_mgr.cpu_stats[cpu];
        
        atomic64_set(&cstats->total_interrupts, 0);
        atomic64_set(&cstats->nested_interrupts, 0);
        atomic64_set(&cstats->context_switches, 0);
        atomic64_set(&cstats->preemptions, 0);
        atomic64_set(&cstats->ipi_sent, 0);
        atomic64_set(&cstats->ipi_received, 0);
        atomic64_set(&cstats->double_faults, 0);
        atomic64_set(&cstats->nmi_count, 0);
        atomic64_set(&cstats->machine_checks, 0);
        atomic64_set(&cstats->pic_interrupts, 0);
        atomic64_set(&cstats->apic_interrupts, 0);
        atomic64_set(&cstats->msi_interrupts, 0);
        atomic64_set(&cstats->msix_interrupts, 0);
        
        cstats->interrupt_time_ns = 0;
        cstats->max_interrupt_time_ns = 0;
        cstats->current_nesting_level = 0;
        cstats->max_nesting_level = 0;
        cstats->interrupt_load_percent = 0;
        cstats->load_measurement_start = get_system_time_ns();
        cstats->load_interrupt_time = 0;
    }
    
    /* Reset system statistics */
    atomic64_set(&stats_mgr.system_stats.total_interrupts, 0);
    atomic64_set(&stats_mgr.system_stats.total_time_ns, 0);
    atomic64_set(&stats_mgr.system_stats.spurious_total, 0);
    atomic64_set(&stats_mgr.system_stats.error_total, 0);
    
    stats_mgr.system_stats.boot_time = get_system_time_ns();
    stats_mgr.system_stats.avg_interrupt_rate = 0;
    stats_mgr.system_stats.peak_interrupt_rate = 0;
    stats_mgr.system_stats.system_interrupt_load = 0;
    stats_mgr.system_stats.system_reliability = 100;
    stats_mgr.system_stats.blacklisted_vectors = 0;
    
    /* Reset trace buffer */
    if (stats_mgr.trace.entries) {
        atomic_set(&stats_mgr.trace.head, 0);
        atomic_set(&stats_mgr.trace.tail, 0);
        atomic64_set(&stats_mgr.trace.dropped_entries, 0);
    }
    
    debug_printf("All interrupt statistics reset\n");
}