/*
 * spurious_interrupt.c - Spurious Interrupt Detection and Handling for Forest OS
 * 
 * This module provides:
 * - Detection of spurious interrupts from various sources
 * - Statistical analysis and threshold-based filtering
 * - Automatic recovery and mitigation strategies
 * - Integration with PIC, APIC, and other interrupt controllers
 * - Rate limiting and flood protection
 * - Comprehensive logging and debugging support
 * 
 * Spurious interrupts can occur due to:
 * - Electrical noise and EMI
 * - Hardware configuration issues
 * - Controller timing problems
 * - Bus contention and signal integrity issues
 * 
 * Copyright (c) 2024 Forest OS Project
 */

#include "interrupt.h"
#include "memory.h"
#include "smp.h"
#include "debug.h"
#include "time.h"
#include <string.h>

/* Spurious interrupt detection thresholds */
#define SPURIOUS_THRESHOLD_RATE_PER_SEC     100    /* Max spurious/sec before action */
#define SPURIOUS_BURST_THRESHOLD            10     /* Max consecutive spurious */
#define SPURIOUS_RATIO_THRESHOLD_PERCENT    20     /* Max % of total interrupts */
#define SPURIOUS_DETECTION_WINDOW_MS        1000   /* Analysis window */
#define SPURIOUS_FLOOD_THRESHOLD            1000   /* Flood detection threshold */

/* Spurious interrupt types */
typedef enum {
    SPURIOUS_TYPE_UNKNOWN,
    SPURIOUS_TYPE_PIC_LINE7,        /* PIC line 7 spurious */
    SPURIOUS_TYPE_PIC_LINE15,       /* PIC line 15 spurious */
    SPURIOUS_TYPE_APIC_SPURIOUS,    /* APIC spurious vector */
    SPURIOUS_TYPE_UNHANDLED,        /* No handler responded */
    SPURIOUS_TYPE_ELECTRICAL,       /* Electrical noise */
    SPURIOUS_TYPE_TIMING,           /* Timing-related */
    SPURIOUS_TYPE_CONFIGURATION,    /* Controller config issue */
    SPURIOUS_TYPE_FLOOD             /* Spurious interrupt flood */
} spurious_type_t;

/* Spurious interrupt source information */
struct spurious_source {
    int vector;
    spurious_type_t type;
    const char *name;
    const char *description;
    bool auto_disable;              /* Auto-disable vector on flood */
    bool requires_eoi;              /* Requires EOI handling */
    uint32_t detection_mask;        /* Detection criteria mask */
};

/* Per-vector spurious tracking */
struct spurious_vector_stats {
    /* Basic counters */
    uint64_t total_count;
    uint64_t recent_count;          /* Count in recent window */
    uint64_t consecutive_count;     /* Consecutive spurious count */
    uint64_t flood_events;          /* Number of flood events */
    
    /* Timing analysis */
    uint64_t last_spurious_time;
    uint64_t min_interval_ns;
    uint64_t max_interval_ns;
    uint64_t avg_interval_ns;
    uint64_t window_start_time;
    
    /* Rate analysis */
    uint32_t rate_per_second;
    uint32_t peak_rate;
    uint32_t spurious_ratio_percent; /* % of total interrupts */
    
    /* State tracking */
    bool flood_detected;
    bool rate_limited;
    bool disabled_due_to_spurious;
    uint32_t mitigation_level;      /* 0-3, increasing severity */
    
    /* Recovery tracking */
    uint64_t last_mitigation_time;
    uint32_t recovery_attempts;
    bool recovery_in_progress;
};

/* Global spurious interrupt management */
struct spurious_manager {
    /* Per-vector statistics */
    struct spurious_vector_stats vector_stats[256];
    
    /* Global statistics */
    struct {
        uint64_t total_spurious;
        uint64_t spurious_floods;
        uint64_t vectors_disabled;
        uint64_t recovery_successes;
        uint64_t recovery_failures;
        uint32_t active_mitigations;
        uint32_t peak_spurious_rate;
    } global_stats;
    
    /* Detection configuration */
    struct {
        bool enabled;
        uint32_t rate_threshold;
        uint32_t burst_threshold;
        uint32_t ratio_threshold;
        uint32_t flood_threshold;
        uint32_t detection_window_ms;
        bool auto_mitigation;
        bool aggressive_filtering;
    } config;
    
    /* State management */
    spinlock_t lock;
    bool initialized;
    uint64_t last_analysis_time;
    uint32_t analysis_interval_ms;
    
    /* Debug and logging */
    bool debug_enabled;
    bool log_all_spurious;
    uint32_t log_level;
};

static struct spurious_manager spurious_mgr = {0};

/* Known spurious interrupt sources */
static struct spurious_source known_sources[] = {
    {
        .vector = 39,   /* IRQ 7 */
        .type = SPURIOUS_TYPE_PIC_LINE7,
        .name = "PIC Line 7",
        .description = "8259A PIC spurious interrupt on line 7",
        .auto_disable = false,
        .requires_eoi = false,
        .detection_mask = 0x01
    },
    {
        .vector = 47,   /* IRQ 15 */
        .type = SPURIOUS_TYPE_PIC_LINE15,
        .name = "PIC Line 15",
        .description = "8259A PIC spurious interrupt on line 15",
        .auto_disable = false,
        .requires_eoi = false,
        .detection_mask = 0x02
    },
    {
        .vector = APIC_SPURIOUS_VECTOR,
        .type = SPURIOUS_TYPE_APIC_SPURIOUS,
        .name = "APIC Spurious",
        .description = "Local APIC spurious interrupt",
        .auto_disable = false,
        .requires_eoi = true,
        .detection_mask = 0x04
    }
};

#define NUM_KNOWN_SOURCES (sizeof(known_sources) / sizeof(known_sources[0]))

/* Forward declarations */
static void spurious_analyze_vector(int vector);
static void spurious_apply_mitigation(int vector, uint32_t level);
static void spurious_attempt_recovery(int vector);
static bool spurious_is_known_source(int vector);
static spurious_type_t spurious_classify_interrupt(int vector, struct interrupt_context *ctx);
static void spurious_update_statistics(int vector, spurious_type_t type);
static void spurious_log_event(int vector, spurious_type_t type, const char *details);
static void spurious_periodic_analysis(void);

/* ===========================
 * INITIALIZATION
 * =========================== */

/**
 * Initialize spurious interrupt detection and handling
 */
int spurious_interrupt_init(void)
{
    int i;
    
    if (spurious_mgr.initialized) {
        return 0;
    }
    
    /* Initialize spurious manager */
    memset(&spurious_mgr, 0, sizeof(spurious_mgr));
    spinlock_init(&spurious_mgr.lock, "spurious_interrupt");
    
    /* Set default configuration */
    spurious_mgr.config.enabled = true;
    spurious_mgr.config.rate_threshold = SPURIOUS_THRESHOLD_RATE_PER_SEC;
    spurious_mgr.config.burst_threshold = SPURIOUS_BURST_THRESHOLD;
    spurious_mgr.config.ratio_threshold = SPURIOUS_RATIO_THRESHOLD_PERCENT;
    spurious_mgr.config.flood_threshold = SPURIOUS_FLOOD_THRESHOLD;
    spurious_mgr.config.detection_window_ms = SPURIOUS_DETECTION_WINDOW_MS;
    spurious_mgr.config.auto_mitigation = true;
    spurious_mgr.config.aggressive_filtering = false;
    
    spurious_mgr.analysis_interval_ms = 1000;
    spurious_mgr.debug_enabled = false;
    spurious_mgr.log_all_spurious = false;
    spurious_mgr.log_level = 1;
    
    /* Initialize per-vector statistics */
    for (i = 0; i < 256; i++) {
        struct spurious_vector_stats *stats = &spurious_mgr.vector_stats[i];
        memset(stats, 0, sizeof(*stats));
        stats->min_interval_ns = UINT64_MAX;
        stats->window_start_time = get_system_time_ns();
    }
    
    spurious_mgr.last_analysis_time = get_system_time_ns();
    spurious_mgr.initialized = true;
    
    debug_printf("Spurious interrupt detection system initialized\n");
    debug_printf("Detection thresholds: rate=%u/sec, burst=%u, ratio=%u%%, flood=%u\n",
                spurious_mgr.config.rate_threshold,
                spurious_mgr.config.burst_threshold,
                spurious_mgr.config.ratio_threshold,
                spurious_mgr.config.flood_threshold);
    
    return 0;
}

/**
 * Cleanup spurious interrupt system
 */
void spurious_interrupt_cleanup(void)
{
    if (!spurious_mgr.initialized) {
        return;
    }
    
    spurious_mgr.initialized = false;
    debug_printf("Spurious interrupt detection system cleaned up\n");
}

/* ===========================
 * DETECTION AND CLASSIFICATION
 * =========================== */

/**
 * Main spurious interrupt detection function
 * Called when an interrupt is not handled by any registered handler
 */
irq_return_t spurious_interrupt_detected(int vector, struct interrupt_context *ctx)
{
    spurious_type_t type;
    struct spurious_vector_stats *stats;
    unsigned long flags;
    uint64_t current_time;
    bool flood_detected = false;
    
    if (!spurious_mgr.initialized || !spurious_mgr.config.enabled) {
        return IRQ_NONE;
    }
    
    if (vector < 0 || vector >= 256) {
        return IRQ_NONE;
    }
    
    current_time = get_system_time_ns();
    
    spin_lock_irqsave(&spurious_mgr.lock, flags);
    
    stats = &spurious_mgr.vector_stats[vector];
    
    /* Classify the spurious interrupt */
    type = spurious_classify_interrupt(vector, ctx);
    
    /* Update basic counters */
    stats->total_count++;
    stats->recent_count++;
    stats->consecutive_count++;
    spurious_mgr.global_stats.total_spurious++;
    
    /* Update timing statistics */
    if (stats->last_spurious_time > 0) {
        uint64_t interval = current_time - stats->last_spurious_time;
        
        if (interval < stats->min_interval_ns) {
            stats->min_interval_ns = interval;
        }
        if (interval > stats->max_interval_ns) {
            stats->max_interval_ns = interval;
        }
        
        /* Update average interval */
        if (stats->total_count > 1) {
            stats->avg_interval_ns = ((stats->avg_interval_ns * (stats->total_count - 1)) + interval) / stats->total_count;
        }
    }
    stats->last_spurious_time = current_time;
    
    /* Check for flood conditions */
    if (stats->consecutive_count >= spurious_mgr.config.flood_threshold) {
        if (!stats->flood_detected) {
            stats->flood_detected = true;
            stats->flood_events++;
            spurious_mgr.global_stats.spurious_floods++;
            flood_detected = true;
            
            spurious_log_event(vector, type, "Spurious interrupt flood detected");
        }
    }
    
    /* Update global statistics */
    spurious_update_statistics(vector, type);
    
    spin_unlock_irqrestore(&spurious_mgr.lock, flags);
    
    /* Apply immediate mitigation if flooding */
    if (flood_detected && spurious_mgr.config.auto_mitigation) {
        spurious_apply_mitigation(vector, 1);
    }
    
    /* Log spurious event if enabled */
    if (spurious_mgr.log_all_spurious || spurious_mgr.debug_enabled) {
        spurious_log_event(vector, type, "Spurious interrupt detected");
    }
    
    /* Handle known spurious interrupt types */
    switch (type) {
        case SPURIOUS_TYPE_PIC_LINE7:
        case SPURIOUS_TYPE_PIC_LINE15:
            /* PIC spurious interrupts don't require EOI */
            return IRQ_HANDLED;
            
        case SPURIOUS_TYPE_APIC_SPURIOUS:
            /* APIC spurious interrupts require EOI */
            if (apic_is_available()) {
                apic_send_eoi();
            }
            return IRQ_HANDLED;
            
        default:
            break;
    }
    
    return IRQ_NONE;
}

/**
 * Check if an interrupt should be considered spurious
 */
bool spurious_is_interrupt_spurious(int vector, struct interrupt_context *ctx)
{
    struct spurious_vector_stats *stats;
    unsigned long flags;
    bool is_spurious = false;
    
    if (!spurious_mgr.initialized || vector < 0 || vector >= 256) {
        return false;
    }
    
    spin_lock_irqsave(&spurious_mgr.lock, flags);
    
    stats = &spurious_mgr.vector_stats[vector];
    
    /* Check if this vector is known to be spurious */
    if (spurious_is_known_source(vector)) {
        is_spurious = true;
    }
    
    /* Check rate-based detection */
    else if (stats->rate_per_second > spurious_mgr.config.rate_threshold) {
        is_spurious = true;
    }
    
    /* Check burst detection */
    else if (stats->consecutive_count > spurious_mgr.config.burst_threshold) {
        is_spurious = true;
    }
    
    /* Check if vector is currently rate limited due to spurious behavior */
    else if (stats->rate_limited) {
        is_spurious = true;
    }
    
    spin_unlock_irqrestore(&spurious_mgr.lock, flags);
    
    return is_spurious;
}

/**
 * Reset spurious counter for a vector (when genuine interrupt occurs)
 */
void spurious_reset_consecutive_count(int vector)
{
    struct spurious_vector_stats *stats;
    unsigned long flags;
    
    if (!spurious_mgr.initialized || vector < 0 || vector >= 256) {
        return;
    }
    
    spin_lock_irqsave(&spurious_mgr.lock, flags);
    
    stats = &spurious_mgr.vector_stats[vector];
    
    /* Reset consecutive counter when genuine interrupt occurs */
    if (stats->consecutive_count > 0) {
        stats->consecutive_count = 0;
        
        /* Clear flood detection if it was active */
        if (stats->flood_detected) {
            stats->flood_detected = false;
            spurious_log_event(vector, SPURIOUS_TYPE_UNKNOWN, "Spurious flood cleared");
        }
    }
    
    spin_unlock_irqrestore(&spurious_mgr.lock, flags);
}

/* ===========================
 * MITIGATION AND RECOVERY
 * =========================== */

/**
 * Apply mitigation measures for spurious interrupts
 */
static void spurious_apply_mitigation(int vector, uint32_t level)
{
    struct spurious_vector_stats *stats;
    unsigned long flags;
    
    if (!spurious_mgr.initialized || vector < 0 || vector >= 256) {
        return;
    }
    
    spin_lock_irqsave(&spurious_mgr.lock, flags);
    
    stats = &spurious_mgr.vector_stats[vector];
    
    if (stats->mitigation_level >= level) {
        spin_unlock_irqrestore(&spurious_mgr.lock, flags);
        return;  /* Already at this level or higher */
    }
    
    stats->mitigation_level = level;
    stats->last_mitigation_time = get_system_time_ns();
    spurious_mgr.global_stats.active_mitigations++;
    
    spin_unlock_irqrestore(&spurious_mgr.lock, flags);
    
    switch (level) {
        case 1:  /* Rate limiting */
            stats->rate_limited = true;
            spurious_log_event(vector, SPURIOUS_TYPE_UNKNOWN, 
                              "Applied rate limiting mitigation");
            break;
            
        case 2:  /* Temporary disable */
            if (interrupt_mgr.irq_chips[vector] && 
                interrupt_mgr.irq_chips[vector]->disable) {
                interrupt_mgr.irq_chips[vector]->disable(vector);
                spurious_log_event(vector, SPURIOUS_TYPE_UNKNOWN, 
                                  "Temporarily disabled spurious vector");
            }
            break;
            
        case 3:  /* Extended disable */
            stats->disabled_due_to_spurious = true;
            spurious_mgr.global_stats.vectors_disabled++;
            
            if (interrupt_mgr.irq_chips[vector] && 
                interrupt_mgr.irq_chips[vector]->disable) {
                interrupt_mgr.irq_chips[vector]->disable(vector);
            }
            
            spurious_log_event(vector, SPURIOUS_TYPE_UNKNOWN, 
                              "Extended disable due to persistent spurious interrupts");
            break;
            
        default:
            break;
    }
    
    if (spurious_mgr.debug_enabled) {
        debug_printf("Applied mitigation level %u for vector %d\n", level, vector);
    }
}

/**
 * Attempt recovery from spurious interrupt mitigation
 */
static void spurious_attempt_recovery(int vector)
{
    struct spurious_vector_stats *stats;
    unsigned long flags;
    uint64_t current_time;
    bool attempt_recovery = false;
    
    if (!spurious_mgr.initialized || vector < 0 || vector >= 256) {
        return;
    }
    
    current_time = get_system_time_ns();
    
    spin_lock_irqsave(&spurious_mgr.lock, flags);
    
    stats = &spurious_mgr.vector_stats[vector];
    
    /* Check if recovery should be attempted */
    if (stats->mitigation_level > 0 && !stats->recovery_in_progress) {
        /* Wait at least 30 seconds before attempting recovery */
        if (current_time - stats->last_mitigation_time > 30000000000ULL) {
            /* Check if spurious rate has decreased significantly */
            if (stats->rate_per_second < spurious_mgr.config.rate_threshold / 4) {
                attempt_recovery = true;
                stats->recovery_in_progress = true;
                stats->recovery_attempts++;
            }
        }
    }
    
    spin_unlock_irqrestore(&spurious_mgr.lock, flags);
    
    if (attempt_recovery) {
        /* Gradually reduce mitigation level */
        if (stats->mitigation_level > 1) {
            stats->mitigation_level--;
            spurious_log_event(vector, SPURIOUS_TYPE_UNKNOWN, 
                              "Attempting recovery - reducing mitigation level");
        } else {
            /* Full recovery */
            stats->mitigation_level = 0;
            stats->rate_limited = false;
            stats->disabled_due_to_spurious = false;
            stats->recovery_in_progress = false;
            
            if (interrupt_mgr.irq_chips[vector] && 
                interrupt_mgr.irq_chips[vector]->enable) {
                interrupt_mgr.irq_chips[vector]->enable(vector);
            }
            
            spurious_mgr.global_stats.recovery_successes++;
            spurious_mgr.global_stats.active_mitigations--;
            
            spurious_log_event(vector, SPURIOUS_TYPE_UNKNOWN, 
                              "Successfully recovered from spurious interrupt mitigation");
        }
        
        if (spurious_mgr.debug_enabled) {
            debug_printf("Recovery attempt for vector %d: mitigation level now %u\n", 
                        vector, stats->mitigation_level);
        }
    }
}

/* ===========================
 * ANALYSIS AND STATISTICS
 * =========================== */

/**
 * Perform periodic analysis of spurious interrupts
 */
static void spurious_periodic_analysis(void)
{
    int i;
    uint64_t current_time;
    uint32_t window_size_ms;
    
    if (!spurious_mgr.initialized) {
        return;
    }
    
    current_time = get_system_time_ns();
    
    /* Check if analysis interval has elapsed */
    if (current_time - spurious_mgr.last_analysis_time < 
        spurious_mgr.analysis_interval_ms * 1000000ULL) {
        return;
    }
    
    window_size_ms = spurious_mgr.config.detection_window_ms;
    
    for (i = 0; i < 256; i++) {
        spurious_analyze_vector(i);
        
        /* Attempt recovery for vectors under mitigation */
        if (spurious_mgr.vector_stats[i].mitigation_level > 0) {
            spurious_attempt_recovery(i);
        }
    }
    
    spurious_mgr.last_analysis_time = current_time;
}

/**
 * Analyze spurious behavior for a specific vector
 */
static void spurious_analyze_vector(int vector)
{
    struct spurious_vector_stats *stats;
    struct irq_desc *desc;
    unsigned long flags;
    uint64_t current_time, window_duration;
    uint32_t total_interrupts;
    
    if (vector < 0 || vector >= 256) {
        return;
    }
    
    current_time = get_system_time_ns();
    
    spin_lock_irqsave(&spurious_mgr.lock, flags);
    
    stats = &spurious_mgr.vector_stats[vector];
    desc = &interrupt_mgr.irq_desc[vector];
    
    window_duration = current_time - stats->window_start_time;
    
    /* Calculate rate per second */
    if (window_duration >= 1000000000ULL) {  /* At least 1 second */
        stats->rate_per_second = (uint32_t)((stats->recent_count * 1000000000ULL) / window_duration);
        
        if (stats->rate_per_second > stats->peak_rate) {
            stats->peak_rate = stats->rate_per_second;
        }
        
        if (stats->rate_per_second > spurious_mgr.global_stats.peak_spurious_rate) {
            spurious_mgr.global_stats.peak_spurious_rate = stats->rate_per_second;
        }
        
        /* Calculate spurious ratio */
        total_interrupts = (uint32_t)atomic_read(&desc->count);
        if (total_interrupts > 0) {
            stats->spurious_ratio_percent = (uint32_t)((stats->total_count * 100) / total_interrupts);
        }
        
        /* Reset window */
        stats->recent_count = 0;
        stats->window_start_time = current_time;
    }
    
    /* Check for mitigation triggers */
    if (spurious_mgr.config.auto_mitigation && stats->mitigation_level == 0) {
        if (stats->rate_per_second > spurious_mgr.config.rate_threshold) {
            spin_unlock_irqrestore(&spurious_mgr.lock, flags);
            spurious_apply_mitigation(vector, 1);
            return;
        }
        
        if (stats->spurious_ratio_percent > spurious_mgr.config.ratio_threshold) {
            spin_unlock_irqrestore(&spurious_mgr.lock, flags);
            spurious_apply_mitigation(vector, 2);
            return;
        }
    }
    
    spin_unlock_irqrestore(&spurious_mgr.lock, flags);
}

/* ===========================
 * STATISTICS AND DEBUGGING
 * =========================== */

/**
 * Get spurious interrupt statistics for a vector
 */
int spurious_get_vector_stats(int vector, struct spurious_vector_stats *stats)
{
    unsigned long flags;
    
    if (!spurious_mgr.initialized || vector < 0 || vector >= 256 || !stats) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&spurious_mgr.lock, flags);
    memcpy(stats, &spurious_mgr.vector_stats[vector], sizeof(*stats));
    spin_unlock_irqrestore(&spurious_mgr.lock, flags);
    
    return 0;
}

/**
 * Get global spurious interrupt statistics
 */
void spurious_get_global_stats(struct spurious_manager *stats)
{
    unsigned long flags;
    
    if (!spurious_mgr.initialized || !stats) {
        return;
    }
    
    spin_lock_irqsave(&spurious_mgr.lock, flags);
    memcpy(stats, &spurious_mgr, sizeof(*stats));
    spin_unlock_irqrestore(&spurious_mgr.lock, flags);
}

/**
 * Dump spurious interrupt statistics
 */
void spurious_dump_statistics(void)
{
    int i, active_vectors = 0;
    unsigned long flags;
    struct spurious_vector_stats *stats;
    
    if (!spurious_mgr.initialized) {
        debug_printf("Spurious interrupt detection not initialized\n");
        return;
    }
    
    /* Force periodic analysis before dumping */
    spurious_periodic_analysis();
    
    spin_lock_irqsave(&spurious_mgr.lock, flags);
    
    debug_printf("=== Spurious Interrupt Statistics ===\n");
    debug_printf("Global Stats:\n");
    debug_printf("  Total spurious: %llu\n", spurious_mgr.global_stats.total_spurious);
    debug_printf("  Spurious floods: %llu\n", spurious_mgr.global_stats.spurious_floods);
    debug_printf("  Vectors disabled: %llu\n", spurious_mgr.global_stats.vectors_disabled);
    debug_printf("  Recovery successes: %llu\n", spurious_mgr.global_stats.recovery_successes);
    debug_printf("  Recovery failures: %llu\n", spurious_mgr.global_stats.recovery_failures);
    debug_printf("  Active mitigations: %u\n", spurious_mgr.global_stats.active_mitigations);
    debug_printf("  Peak spurious rate: %u/sec\n", spurious_mgr.global_stats.peak_spurious_rate);
    
    debug_printf("\nConfiguration:\n");
    debug_printf("  Detection enabled: %s\n", spurious_mgr.config.enabled ? "yes" : "no");
    debug_printf("  Rate threshold: %u/sec\n", spurious_mgr.config.rate_threshold);
    debug_printf("  Burst threshold: %u\n", spurious_mgr.config.burst_threshold);
    debug_printf("  Ratio threshold: %u%%\n", spurious_mgr.config.ratio_threshold);
    debug_printf("  Auto mitigation: %s\n", spurious_mgr.config.auto_mitigation ? "yes" : "no");
    
    debug_printf("\nPer-Vector Statistics (showing vectors with spurious activity):\n");
    for (i = 0; i < 256; i++) {
        stats = &spurious_mgr.vector_stats[i];
        
        if (stats->total_count > 0) {
            active_vectors++;
            debug_printf("Vector %3d: %llu total, %u/sec, %u%% ratio, level %u%s%s\n",
                        i, stats->total_count, stats->rate_per_second, 
                        stats->spurious_ratio_percent, stats->mitigation_level,
                        stats->flood_detected ? " [FLOOD]" : "",
                        stats->rate_limited ? " [LIMITED]" : "");
            
            if (stats->flood_events > 0) {
                debug_printf("    Floods: %llu, Max consecutive: %llu\n",
                            stats->flood_events, stats->consecutive_count);
            }
            
            if (stats->min_interval_ns < UINT64_MAX) {
                debug_printf("    Intervals: min=%llu ns, avg=%llu ns, max=%llu ns\n",
                            stats->min_interval_ns, stats->avg_interval_ns, stats->max_interval_ns);
            }
        }
    }
    
    debug_printf("\nSummary: %d vectors with spurious activity\n", active_vectors);
    
    spin_unlock_irqrestore(&spurious_mgr.lock, flags);
}

/**
 * Configure spurious interrupt detection
 */
int spurious_configure(uint32_t rate_threshold, uint32_t burst_threshold, 
                      uint32_t ratio_threshold, bool auto_mitigation)
{
    unsigned long flags;
    
    if (!spurious_mgr.initialized) {
        return -ENODEV;
    }
    
    spin_lock_irqsave(&spurious_mgr.lock, flags);
    
    spurious_mgr.config.rate_threshold = rate_threshold;
    spurious_mgr.config.burst_threshold = burst_threshold;
    spurious_mgr.config.ratio_threshold = ratio_threshold;
    spurious_mgr.config.auto_mitigation = auto_mitigation;
    
    spin_unlock_irqrestore(&spurious_mgr.lock, flags);
    
    debug_printf("Spurious detection configured: rate=%u/sec, burst=%u, ratio=%u%%, auto=%s\n",
                rate_threshold, burst_threshold, ratio_threshold, 
                auto_mitigation ? "yes" : "no");
    
    return 0;
}

/**
 * Enable or disable spurious interrupt debugging
 */
void spurious_debug_enable(bool enable)
{
    spurious_mgr.debug_enabled = enable;
    spurious_mgr.log_all_spurious = enable;
    debug_printf("Spurious interrupt debugging %s\n", enable ? "enabled" : "disabled");
}

/* ===========================
 * HELPER FUNCTIONS
 * =========================== */

/**
 * Check if a vector is a known spurious source
 */
static bool spurious_is_known_source(int vector)
{
    int i;
    
    for (i = 0; i < NUM_KNOWN_SOURCES; i++) {
        if (known_sources[i].vector == vector) {
            return true;
        }
    }
    
    return false;
}

/**
 * Classify the type of spurious interrupt
 */
static spurious_type_t spurious_classify_interrupt(int vector, struct interrupt_context *ctx)
{
    int i;
    struct spurious_vector_stats *stats;
    
    /* Check known sources first */
    for (i = 0; i < NUM_KNOWN_SOURCES; i++) {
        if (known_sources[i].vector == vector) {
            return known_sources[i].type;
        }
    }
    
    stats = &spurious_mgr.vector_stats[vector];
    
    /* Classify based on behavior patterns */
    if (stats->consecutive_count >= spurious_mgr.config.flood_threshold) {
        return SPURIOUS_TYPE_FLOOD;
    }
    
    if (stats->avg_interval_ns > 0 && stats->avg_interval_ns < 1000000) {  /* < 1ms */
        return SPURIOUS_TYPE_TIMING;
    }
    
    if (stats->rate_per_second > spurious_mgr.config.rate_threshold * 2) {
        return SPURIOUS_TYPE_ELECTRICAL;
    }
    
    return SPURIOUS_TYPE_UNHANDLED;
}

/**
 * Update spurious statistics
 */
static void spurious_update_statistics(int vector, spurious_type_t type)
{
    /* Statistics are updated in the caller function */
    /* This function can be extended for type-specific statistics */
    (void)vector;
    (void)type;
}

/**
 * Log spurious interrupt event
 */
static void spurious_log_event(int vector, spurious_type_t type, const char *details)
{
    const char *type_names[] = {
        "Unknown", "PIC Line 7", "PIC Line 15", "APIC Spurious",
        "Unhandled", "Electrical", "Timing", "Configuration", "Flood"
    };
    
    if (type >= 0 && type < sizeof(type_names)/sizeof(type_names[0])) {
        debug_printf("SPURIOUS: Vector %d (%s): %s\n", 
                    vector, type_names[type], details ? details : "");
    } else {
        debug_printf("SPURIOUS: Vector %d (Unknown Type %d): %s\n", 
                    vector, type, details ? details : "");
    }
}

/**
 * Main periodic function to be called by system timer
 */
void spurious_periodic_update(void)
{
    if (spurious_mgr.initialized && spurious_mgr.config.enabled) {
        spurious_periodic_analysis();
    }
}