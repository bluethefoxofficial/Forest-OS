/*
 * Timer Interrupt Source Abstraction Layer for Forest OS
 * Provides unified interface for multiple timing sources
 * Manages timer source selection, calibration, and fallback
 */

#include "include/interrupt.h"
#include "include/timer.h"
#include "include/cpu_ops.h"
#include "include/debuglog.h"
#include "include/panic.h"
#include "include/atomic.h"
#include "include/spinlock.h"

/* Stub for TSC frequency - to be implemented */
static inline uint64_t tsc_get_frequency(void) {
    return 0;  /* TSC not calibrated yet */
}

/* Timer source priorities (higher = better) */
#define TIMER_PRIORITY_VERY_HIGH    100
#define TIMER_PRIORITY_HIGH         75
#define TIMER_PRIORITY_MEDIUM       50
#define TIMER_PRIORITY_LOW          25
#define TIMER_PRIORITY_VERY_LOW     10

/* Timer precision levels */
#define TIMER_PRECISION_VERY_HIGH   100
#define TIMER_PRECISION_HIGH        75
#define TIMER_PRECISION_MEDIUM      50
#define TIMER_PRECISION_LOW         25
#define TIMER_PRECISION_VERY_LOW    10

/* Timer source states */
#define TIMER_STATE_UNINITIALIZED   0
#define TIMER_STATE_AVAILABLE       1
#define TIMER_STATE_ACTIVE          2
#define TIMER_STATE_FAILED          3
#define TIMER_STATE_SUSPENDED       4

/* Maximum number of timer sources */
#define MAX_TIMER_SOURCES           16

/* Timer calibration constants */
#define CALIBRATION_DURATION_MS     100
#define MIN_CALIBRATION_ACCURACY    95  /* 95% accuracy minimum */
#define TSC_CALIBRATION_SAMPLES     5

/* Timer source structure - defined in interrupt.h */
/* Using struct timer_source from interrupt.h */

/* Timer abstraction state */
struct timer_abstraction {
    bool initialized;
    struct timer_source *sources[MAX_TIMER_SOURCES];
    uint32_t num_sources;
    struct timer_source *primary_source;
    struct timer_source *fallback_source;
    struct timer_source *calibration_source;
    uint64_t system_uptime_ns;
    uint64_t last_update_time;
    atomic64_t timer_switches;
    atomic64_t timer_failures;
    spinlock_t lock;
    bool auto_fallback_enabled;
    uint32_t failure_threshold;
};

static struct timer_abstraction timer_abs = {
    .initialized = false,
    .num_sources = 0,
    .primary_source = NULL,
    .fallback_source = NULL,
    .calibration_source = NULL,
    .system_uptime_ns = 0,
    .auto_fallback_enabled = true,
    .failure_threshold = 3,
    .lock = SPINLOCK_INIT("timer_lock")
};

/* Timer source instances (extern references) */
extern struct timer_source hpet_timer_source;
extern struct timer_source apic_timer_source;
extern struct timer_source pit_timer_source;

/* Function prototypes */
static int timer_source_compare(struct timer_source *a, struct timer_source *b);
static void timer_source_select_primary(void);
static void timer_source_select_fallback(void);
static int timer_source_calibrate(struct timer_source *source);
static bool timer_source_validate(struct timer_source *source);
static void timer_source_mark_failed(struct timer_source *source);
static uint64_t timer_read_tsc(void);
static void timer_fallback_to_next(void);

/*
 * Compare two timer sources for priority
 */
static int timer_source_compare(struct timer_source *a, struct timer_source *b)
{
    if (!a) return -1;
    if (!b) return 1;
    
    /* Higher priority wins */
    if (a->priority != b->priority) {
        return (a->priority > b->priority) ? 1 : -1;
    }
    
    /* Higher precision wins */
    if (a->precision != b->precision) {
        return (a->precision > b->precision) ? 1 : -1;
    }
    
    /* Higher frequency wins */
    return (a->frequency > b->frequency) ? 1 : -1;
}

/*
 * Select primary timer source
 */
static void timer_source_select_primary(void)
{
    struct timer_source *best = NULL;
    
    for (uint32_t i = 0; i < timer_abs.num_sources; i++) {
        struct timer_source *source = timer_abs.sources[i];
        
        if (source->state == TIMER_STATE_AVAILABLE && 
            timer_source_compare(source, best) > 0) {
            best = source;
        }
    }
    
    if (best && best != timer_abs.primary_source) {
        if (timer_abs.primary_source) {
            debuglog_printf("Timer: Switching primary from %s to %s\n", 
                       timer_abs.primary_source->name, best->name);
            timer_abs.primary_source->stop(timer_abs.primary_source);
        }
        
        timer_abs.primary_source = best;
        best->state = TIMER_STATE_ACTIVE;
        
        if (best->init) {
            best->init(best);
        }
        
        debuglog_printf("Timer: Selected %s as primary timer source\n", best->name);
    }
}

/*
 * Select fallback timer source
 */
static void timer_source_select_fallback(void)
{
    struct timer_source *best = NULL;
    
    for (uint32_t i = 0; i < timer_abs.num_sources; i++) {
        struct timer_source *source = timer_abs.sources[i];
        
        if (source != timer_abs.primary_source && 
            source->state == TIMER_STATE_AVAILABLE && 
            timer_source_compare(source, best) > 0) {
            best = source;
        }
    }
    
    timer_abs.fallback_source = best;
    
    if (best) {
        debuglog_printf("Timer: Selected %s as fallback timer source\n", best->name);
    }
}

/*
 * Calibrate timer source against TSC
 */
static int timer_source_calibrate(struct timer_source *source)
{
    uint64_t tsc_start, tsc_end;
    uint64_t timer_start, timer_end;
    uint64_t tsc_freq, calculated_freq;
    uint64_t accuracy;
    
    if (!source || !source->read) {
        return -1;
    }
    
    debuglog_printf("Timer: Calibrating %s...\n", source->name);
    
    /* Use multiple samples for better accuracy */
    uint64_t total_accuracy = 0;
    uint32_t valid_samples = 0;
    
    for (int sample = 0; sample < TSC_CALIBRATION_SAMPLES; sample++) {
        /* Record start values */
        tsc_start = read_tsc();
        timer_start = source->read(source);
        
        /* Wait for calibration period */
        pit_delay_ms(CALIBRATION_DURATION_MS / TSC_CALIBRATION_SAMPLES);
        
        /* Record end values */
        tsc_end = read_tsc();
        timer_end = source->read(source);
        
        /* Skip sample if timer didn't advance */
        if (timer_end <= timer_start) {
            continue;
        }
        
        /* Calculate frequencies */
        uint64_t tsc_ticks = tsc_end - tsc_start;
        uint64_t timer_ticks = timer_end - timer_start;
        
        if (tsc_ticks > 0 && timer_ticks > 0) {
            calculated_freq = (timer_ticks * tsc_get_frequency()) / tsc_ticks;
            
            /* Calculate accuracy compared to expected frequency */
            uint64_t expected = source->frequency;
            if (expected > 0) {
                if (calculated_freq > expected) {
                    accuracy = (expected * 100) / calculated_freq;
                } else {
                    accuracy = (calculated_freq * 100) / expected;
                }
                
                total_accuracy += accuracy;
                valid_samples++;
            }
        }
    }
    
    if (valid_samples > 0) {
        source->accuracy_percentage = total_accuracy / valid_samples;
        source->calibrated_frequency = calculated_freq;
        
        debuglog_printf("Timer: %s calibration complete - accuracy %u%%\n", 
                   source->name, source->accuracy_percentage);
        
        return (source->accuracy_percentage >= MIN_CALIBRATION_ACCURACY) ? 0 : -1;
    }
    
    debuglog_printf("Timer: %s calibration failed - no valid samples\n", source->name);
    return -1;
}

/*
 * Validate timer source functionality
 */
static bool timer_source_validate(struct timer_source *source)
{
    uint64_t initial_time, current_time;
    int validation_ms = 10;
    
    if (!source || !source->read) {
        return false;
    }
    
    /* Test basic read functionality */
    initial_time = source->read(source);
    pit_udelay(1000);  /* 1ms delay */
    current_time = source->read(source);
    
    /* Timer should advance */
    if (current_time <= initial_time) {
        debuglog_printf("Timer: %s validation failed - time not advancing\n", source->name);
        return false;
    }
    
    /* Test periodic mode if supported */
    if (source->supports_periodic && source->set_periodic) {
        source->set_periodic(source, 1000000ULL);  /* 1ms period */
        
        /* Wait and check for interrupts */
        uint64_t initial_count = atomic64_read(&source->interrupt_count);
        pit_delay_ms(validation_ms);
        uint64_t final_count = atomic64_read(&source->interrupt_count);
        
        source->stop(source);
        
        /* Should have received some interrupts */
        if (final_count <= initial_count) {
            debuglog_printf("Timer: %s validation failed - no periodic interrupts\n", source->name);
            return false;
        }
    }
    
    debuglog_printf("Timer: %s validation successful\n", source->name);
    return true;
}

/*
 * Mark timer source as failed
 */
static void timer_source_mark_failed(struct timer_source *source)
{
    if (!source) {
        return;
    }
    
    debuglog_printf("Timer: Marking %s as failed\n", source->name);
    
    source->state = TIMER_STATE_FAILED;
    atomic64_inc(&timer_abs.timer_failures);
    
    if (source == timer_abs.primary_source) {
        debuglog_printf("Timer: Primary timer source failed - switching to fallback\n");
        timer_fallback_to_next();
    }
}

/*
 * Fallback to next available timer source
 */
static void timer_fallback_to_next(void)
{
    unsigned long flags;
    
    spin_lock_irqsave(&timer_abs.lock, flags);
    
    if (timer_abs.fallback_source && 
        timer_abs.fallback_source->state == TIMER_STATE_AVAILABLE) {
        
        if (timer_abs.primary_source) {
            timer_abs.primary_source->stop(timer_abs.primary_source);
            timer_abs.primary_source->state = TIMER_STATE_FAILED;
        }
        
        timer_abs.primary_source = timer_abs.fallback_source;
        timer_abs.fallback_source = NULL;
        
        atomic64_inc(&timer_abs.timer_switches);
        
        /* Re-select fallback */
        timer_source_select_fallback();
        
        debuglog_printf("Timer: Switched to fallback timer %s\n", 
                   timer_abs.primary_source->name);
    } else {
        debuglog_printf("Timer: No fallback timer available!\n");
        timer_abs.primary_source = NULL;
    }
    
    spin_unlock_irqrestore(&timer_abs.lock, flags);
}

/*
 * Read TSC for calibration
 */
static uint64_t timer_read_tsc(void)
{
    return read_tsc();
}

/*
 * Initialize timer abstraction layer
 */
int timer_interrupt_init(void)
{
    unsigned long flags;
    
    debuglog_printf("Timer: Initializing timer abstraction layer\n");
    
    if (timer_abs.initialized) {
        debuglog_printf("Timer: Already initialized\n");
        return 0;
    }
    
    spin_lock_irqsave(&timer_abs.lock, flags);
    
    /* Clear state */
    timer_abs.num_sources = 0;
    timer_abs.primary_source = NULL;
    timer_abs.fallback_source = NULL;
    timer_abs.system_uptime_ns = 0;
    atomic64_set(&timer_abs.timer_switches, 0);
    atomic64_set(&timer_abs.timer_failures, 0);
    
    /* Register available timer sources */
    /* Note: These would be implemented by their respective drivers */
    if (hpet_is_available()) {
        timer_abs.sources[timer_abs.num_sources++] = &hpet_timer_source;
        debuglog_printf("Timer: Registered HPET timer source\n");
    }
    
    if (apic_timer_is_available()) {
        timer_abs.sources[timer_abs.num_sources++] = &apic_timer_source;
        debuglog_printf("Timer: Registered APIC timer source\n");
    }
    
    if (pit_is_available()) {
        timer_abs.sources[timer_abs.num_sources++] = &pit_timer_source;
        debuglog_printf("Timer: Registered PIT timer source\n");
    }
    
    timer_abs.initialized = true;
    
    spin_unlock_irqrestore(&timer_abs.lock, flags);
    
    /* Validate and calibrate timer sources */
    for (uint32_t i = 0; i < timer_abs.num_sources; i++) {
        struct timer_source *source = timer_abs.sources[i];
        
        source->state = TIMER_STATE_UNINITIALIZED;
        
        if (timer_source_validate(source)) {
            source->state = TIMER_STATE_AVAILABLE;
            
            if (source->requires_calibration) {
                if (timer_source_calibrate(source) != 0) {
                    debuglog_printf("Timer: %s failed calibration\n", source->name);
                    source->state = TIMER_STATE_FAILED;
                }
            }
        } else {
            source->state = TIMER_STATE_FAILED;
        }
    }
    
    /* Select primary and fallback sources */
    timer_source_select_primary();
    timer_source_select_fallback();
    
    if (!timer_abs.primary_source) {
        debuglog_printf("Timer: No suitable primary timer source found!\n");
        return -1;
    }
    
    debuglog_printf("Timer: Timer abstraction layer initialized\n");
    return 0;
}

/*
 * Register timer source
 */
int register_timer_source(struct timer_source *source)
{
    unsigned long flags;
    
    if (!source || timer_abs.num_sources >= MAX_TIMER_SOURCES) {
        return -1;
    }
    
    spin_lock_irqsave(&timer_abs.lock, flags);
    
    timer_abs.sources[timer_abs.num_sources] = source;
    timer_abs.num_sources++;
    
    source->state = TIMER_STATE_UNINITIALIZED;
    atomic64_set(&source->interrupt_count, 0);
    source->total_runtime_ns = 0;
    
    spin_unlock_irqrestore(&timer_abs.lock, flags);
    
    debuglog_printf("Timer: Registered timer source %s\n", source->name);
    
    /* Re-evaluate timer selection if already initialized */
    if (timer_abs.initialized) {
        if (timer_source_validate(source)) {
            source->state = TIMER_STATE_AVAILABLE;
            timer_source_select_primary();
            timer_source_select_fallback();
        } else {
            source->state = TIMER_STATE_FAILED;
        }
    }
    
    return 0;
}

/*
 * Unregister timer source
 */
void unregister_timer_source(struct timer_source *source)
{
    unsigned long flags;
    
    if (!source) {
        return;
    }
    
    spin_lock_irqsave(&timer_abs.lock, flags);
    
    /* Find and remove source */
    for (uint32_t i = 0; i < timer_abs.num_sources; i++) {
        if (timer_abs.sources[i] == source) {
            /* Shift remaining sources down */
            for (uint32_t j = i; j < timer_abs.num_sources - 1; j++) {
                timer_abs.sources[j] = timer_abs.sources[j + 1];
            }
            timer_abs.num_sources--;
            break;
        }
    }
    
    /* Handle active sources */
    if (timer_abs.primary_source == source) {
        timer_abs.primary_source = NULL;
        timer_source_select_primary();
    }
    
    if (timer_abs.fallback_source == source) {
        timer_abs.fallback_source = NULL;
        timer_source_select_fallback();
    }
    
    spin_unlock_irqrestore(&timer_abs.lock, flags);
    
    /* Clean up source */
    if (source->cleanup) {
        source->cleanup(source);
    }
    
    debuglog_printf("Timer: Unregistered timer source %s\n", source->name);
}

/*
 * Get primary timer source
 */
struct timer_source *get_primary_timer_source(void)
{
    return timer_abs.primary_source;
}

/*
 * Get system time from primary timer source
 */
uint64_t get_system_time_ns(void)
{
    if (!timer_abs.primary_source || !timer_abs.primary_source->read) {
        return timer_abs.system_uptime_ns;
    }
    
    return timer_abs.primary_source->read(timer_abs.primary_source);
}

/*
 * Set timer for periodic interrupts
 */
int timer_set_periodic(uint64_t period_ns)
{
    if (!timer_abs.primary_source || !timer_abs.primary_source->set_periodic) {
        return -1;
    }
    
    timer_abs.primary_source->set_periodic(timer_abs.primary_source, period_ns);
    return 0;
}

/*
 * Set timer for oneshot interrupt
 */
int timer_set_oneshot(uint64_t timeout_ns)
{
    if (!timer_abs.primary_source || !timer_abs.primary_source->set_oneshot) {
        return -1;
    }

    timer_abs.primary_source->set_oneshot(timer_abs.primary_source, timeout_ns);
    return 0;
}

/*
 * Stop timer
 */
void timer_stop(void)
{
    if (timer_abs.primary_source && timer_abs.primary_source->stop) {
        timer_abs.primary_source->stop(timer_abs.primary_source);
    }
}

/*
 * Timer interrupt handler (called by individual timer sources)
 */
void timer_interrupt_handler(void)
{
    uint64_t current_time = get_system_time_ns();
    
    /* Update system uptime */
    if (current_time > timer_abs.last_update_time) {
        timer_abs.system_uptime_ns += (current_time - timer_abs.last_update_time);
    }
    timer_abs.last_update_time = current_time;
    
    /* Update interrupt count for primary source */
    if (timer_abs.primary_source) {
        atomic64_inc(&timer_abs.primary_source->interrupt_count);
    }
    
    /* Handle timer-driven tasks */
    /* This would call scheduler, update jiffies, handle timeouts, etc. */
    
    /* Check for timer source health */
    if (timer_abs.auto_fallback_enabled && timer_abs.primary_source) {
        /* Simple health check - if we haven't received interrupts recently,
           consider switching to fallback */
        uint64_t last_count = atomic64_read(&timer_abs.primary_source->interrupt_count);
        static uint64_t prev_count = 0;
        static int missed_interrupts = 0;
        
        if (last_count == prev_count) {
            missed_interrupts++;
            if (missed_interrupts >= timer_abs.failure_threshold) {
                debuglog_printf("Timer: Primary timer appears unhealthy, switching\n");
                timer_fallback_to_next();
                missed_interrupts = 0;
            }
        } else {
            missed_interrupts = 0;
        }
        prev_count = last_count;
    }
}

/*
 * Get timer abstraction statistics
 */
void timer_get_abstraction_stats(struct timer_abstraction_stats *stats)
{
    if (!stats) {
        return;
    }
    
    stats->initialized = timer_abs.initialized;
    stats->num_sources = timer_abs.num_sources;
    stats->primary_source_name = timer_abs.primary_source ? timer_abs.primary_source->name : "None";
    stats->fallback_source_name = timer_abs.fallback_source ? timer_abs.fallback_source->name : "None";
    stats->system_uptime_ns = timer_abs.system_uptime_ns;
    stats->timer_switches = atomic64_read(&timer_abs.timer_switches);
    stats->timer_failures = atomic64_read(&timer_abs.timer_failures);
    stats->auto_fallback_enabled = timer_abs.auto_fallback_enabled;
    
    /* Copy source information */
    for (uint32_t i = 0; i < timer_abs.num_sources && i < MAX_TIMER_SOURCES; i++) {
        struct timer_source *source = timer_abs.sources[i];
        
        stats->sources[i].name = source->name;
        stats->sources[i].priority = source->priority;
        stats->sources[i].precision = source->precision;
        stats->sources[i].frequency = source->frequency;
        stats->sources[i].state = source->state;
        stats->sources[i].interrupt_count = atomic64_read(&source->interrupt_count);
        stats->sources[i].accuracy_percentage = source->accuracy_percentage;
    }
}