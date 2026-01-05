/*
 * Local APIC Timer Driver for High-Precision Timing
 * Provides accurate timing services using the Local APIC timer
 * Integrates with Forest OS timer subsystem and interrupt management
 */

#include "interrupt.h"
#include "timer.h"
#include "apic.h"
#include "cpu_ops.h"
#include "debuglog.h"
#include "panic.h"
#include "atomic.h"
#include "mm.h"

#if defined(__GNUC__) && !defined(__clang__)
#define NO_OPTIMIZE __attribute__((optimize("O0")))
#elif defined(__clang__)
#define NO_OPTIMIZE __attribute__((optnone))
#else
#define NO_OPTIMIZE
#endif

/* APIC Timer registers (from apic.c) */
#define LAPIC_REG_TIMER_LVT         0x320  /* Timer Local Vector Table */
#define LAPIC_REG_TIMER_INITIAL     0x380  /* Timer Initial Count Register */
#define LAPIC_REG_TIMER_CURRENT     0x390  /* Timer Current Count Register */
#define LAPIC_REG_TIMER_DCR         0x3E0  /* Timer Divide Configuration Register */

/* Timer modes */
#define APIC_TIMER_MODE_ONESHOT     0x00000
#define APIC_TIMER_MODE_PERIODIC    0x20000
#define APIC_TIMER_MODE_TSC         0x40000

/* Timer divide values */
#define APIC_TIMER_DIV_2            0x0
#define APIC_TIMER_DIV_4            0x1
#define APIC_TIMER_DIV_8            0x2
#define APIC_TIMER_DIV_16           0x3
#define APIC_TIMER_DIV_32           0x8
#define APIC_TIMER_DIV_64           0x9
#define APIC_TIMER_DIV_128          0xA
#define APIC_TIMER_DIV_1            0xB

/* Timer frequency and precision */
#define APIC_TIMER_FREQUENCY_HZ     1000    /* 1ms resolution */
#define APIC_TIMER_MAX_PERIOD_US    100000  /* 100ms maximum */
#define APIC_TIMER_MIN_PERIOD_US    100     /* 100μs minimum */

/* Global timer_source export for timer_abstraction.c */
struct timer_source apic_timer_source = {0};

/* Timer state */
struct apic_timer_state {
    bool initialized;
    bool running;
    bool periodic_mode;
    uint32_t base_frequency;
    uint32_t current_divisor;
    uint32_t ticks_per_ms;
    uint32_t ticks_per_us;
    uint64_t total_ticks;
    uint64_t system_time_ns;
    atomic64_t timer_interrupts;
    atomic64_t missed_deadlines;
    spinlock_t lock;
    struct timer_source timer_source;
};

static struct apic_timer_state apic_timer = {
    .initialized = false,
    .running = false,
    .periodic_mode = true,
    .lock = SPINLOCK_INIT("apic_timer")
};

/* External functions from apic.c */
extern uint32_t apic_read_register(uint32_t reg);
extern void apic_write_register(uint32_t reg, uint32_t value);
extern void apic_send_eoi(void);
extern bool apic_is_available(void);

/* Local function prototypes */
static int apic_timer_calibrate_frequency(void);
static int apic_timer_set_mode(bool periodic);
static int apic_timer_set_frequency(uint32_t frequency_hz);
static uint64_t apic_timer_read_counter(void);
static irq_return_t apic_timer_interrupt_handler(struct interrupt_context *ctx);
static uint32_t apic_timer_get_optimal_divisor(uint32_t target_frequency);

/* Timer source callbacks - match struct timer_source function pointer signatures */
static int apic_timer_source_init(struct timer_source *self);
static void apic_timer_source_cleanup(struct timer_source *self);
static uint64_t apic_timer_source_read(struct timer_source *self);
static void apic_timer_source_set_periodic(struct timer_source *self, uint64_t period_ns);
static void apic_timer_source_set_oneshot(struct timer_source *self, uint64_t timeout_ns);
static void apic_timer_source_stop(struct timer_source *self);

/*
 * Get optimal timer divisor for target frequency
 */
static uint32_t apic_timer_get_optimal_divisor(uint32_t target_frequency)
{
    struct {
        uint32_t divisor_val;
        uint32_t divisor_reg;
    } divisors[] = {
        {1, APIC_TIMER_DIV_1},
        {2, APIC_TIMER_DIV_2},
        {4, APIC_TIMER_DIV_4},
        {8, APIC_TIMER_DIV_8},
        {16, APIC_TIMER_DIV_16},
        {32, APIC_TIMER_DIV_32},
        {64, APIC_TIMER_DIV_64},
        {128, APIC_TIMER_DIV_128}
    };
    
    uint32_t base_freq = apic_timer.base_frequency;
    uint32_t best_divisor = APIC_TIMER_DIV_1;
    uint32_t min_error = 0xFFFFFFFF;
    
    for (int i = 0; i < 8; i++) {
        uint32_t effective_freq = base_freq / divisors[i].divisor_val;
        uint32_t max_count = 0xFFFFFFFF;
        uint32_t max_freq = effective_freq;
        
        if (target_frequency <= max_freq) {
            uint32_t error = (max_freq > target_frequency) ? 
                           (max_freq - target_frequency) : 
                           (target_frequency - max_freq);
                           
            if (error < min_error) {
                min_error = error;
                best_divisor = divisors[i].divisor_reg;
            }
        }
    }
    
    return best_divisor;
}

/*
 * Calibrate APIC timer frequency
 */
static int apic_timer_calibrate_frequency(void)
{
    const uint32_t CALIBRATION_MS = 10;
    uint32_t timer_start, timer_end, timer_ticks;
    uint64_t tsc_start, tsc_end;
    
    debuglog_printf("APIC Timer: Calibrating timer frequency\n");
    
    /* Set timer to use divisor 16 for calibration */
    apic_write_register(LAPIC_REG_TIMER_DCR, APIC_TIMER_DIV_16);
    apic_timer.current_divisor = 16;
    
    /* Set maximum initial count */
    apic_write_register(LAPIC_REG_TIMER_INITIAL, 0xFFFFFFFF);
    
    /* Record start values */
    tsc_start = read_tsc();
    timer_start = apic_read_register(LAPIC_REG_TIMER_CURRENT);
    
    /* Wait for calibration period */
    timer_sleep_ms(CALIBRATION_MS);
    
    /* Record end values */
    tsc_end = read_tsc();
    timer_end = apic_read_register(LAPIC_REG_TIMER_CURRENT);
    
    /* Stop timer */
    apic_write_register(LAPIC_REG_TIMER_INITIAL, 0);
    
    /* Calculate timer frequency */
    timer_ticks = timer_start - timer_end;
    
    if (timer_ticks == 0) {
        debuglog_printf("APIC Timer: Calibration failed - no timer ticks\n");
        return -1;
    }
    
    /* Base frequency = timer_ticks * divisor / calibration_time */
    apic_timer.base_frequency = (timer_ticks * 16 * 1000) / CALIBRATION_MS;
    apic_timer.ticks_per_ms = apic_timer.base_frequency / (1000 * 16);
    apic_timer.ticks_per_us = apic_timer.base_frequency / (1000000 * 16);
    
    debuglog_printf("APIC Timer: Calibrated frequency = %u Hz\n", apic_timer.base_frequency);
    debuglog_printf("APIC Timer: Ticks per ms = %u, per us = %u\n", 
                apic_timer.ticks_per_ms, apic_timer.ticks_per_us);
    
    return 0;
}

/*
 * Set timer mode (periodic or oneshot)
 */
NO_OPTIMIZE static int apic_timer_set_mode(bool periodic)
{
    uint32_t lvt_value = IRQ_APIC_TIMER;
    
    if (periodic) {
        lvt_value |= APIC_TIMER_MODE_PERIODIC;
        apic_timer.periodic_mode = true;
    } else {
        lvt_value |= APIC_TIMER_MODE_ONESHOT;
        apic_timer.periodic_mode = false;
    }
    
    apic_write_register(LAPIC_REG_TIMER_LVT, lvt_value);
    
    debuglog_printf("APIC Timer: Set to %s mode\n", periodic ? "periodic" : "oneshot");
    
    return 0;
}

/*
 * Set timer frequency
 */
NO_OPTIMIZE static int apic_timer_set_frequency(uint32_t frequency_hz)
{
    uint32_t divisor_reg, effective_freq, initial_count;
    
    if (frequency_hz == 0 || frequency_hz > apic_timer.base_frequency) {
        return -1;
    }
    
    spinlock_acquire(&apic_timer.lock);
    
    /* Get optimal divisor */
    divisor_reg = apic_timer_get_optimal_divisor(frequency_hz);
    apic_write_register(LAPIC_REG_TIMER_DCR, divisor_reg);
    
    /* Calculate current divisor value */
    switch (divisor_reg) {
        case APIC_TIMER_DIV_1:   apic_timer.current_divisor = 1; break;
        case APIC_TIMER_DIV_2:   apic_timer.current_divisor = 2; break;
        case APIC_TIMER_DIV_4:   apic_timer.current_divisor = 4; break;
        case APIC_TIMER_DIV_8:   apic_timer.current_divisor = 8; break;
        case APIC_TIMER_DIV_16:  apic_timer.current_divisor = 16; break;
        case APIC_TIMER_DIV_32:  apic_timer.current_divisor = 32; break;
        case APIC_TIMER_DIV_64:  apic_timer.current_divisor = 64; break;
        case APIC_TIMER_DIV_128: apic_timer.current_divisor = 128; break;
        default: apic_timer.current_divisor = 16; break;
    }
    
    /* Calculate effective frequency and initial count */
    effective_freq = apic_timer.base_frequency / apic_timer.current_divisor;
    initial_count = effective_freq / frequency_hz;
    
    if (initial_count == 0) {
        initial_count = 1;
    }
    
    /* Set initial count */
    apic_write_register(LAPIC_REG_TIMER_INITIAL, initial_count);
    
    spinlock_release(&apic_timer.lock);
    
    debuglog_printf("APIC Timer: Set frequency %u Hz (divisor=%u, count=%u)\n", 
                frequency_hz, apic_timer.current_divisor, initial_count);
    
    return 0;
}

/*
 * Read current timer counter
 */
static uint64_t apic_timer_read_counter(void)
{
    uint32_t current_count = apic_read_register(LAPIC_REG_TIMER_CURRENT);
    uint64_t elapsed_ns;
    
    /* Convert timer ticks to nanoseconds */
    if (apic_timer.current_divisor > 0 && apic_timer.base_frequency > 0) {
        uint64_t effective_freq = apic_timer.base_frequency / apic_timer.current_divisor;
        elapsed_ns = ((uint64_t)current_count * 1000000000ULL) / effective_freq;
    } else {
        elapsed_ns = 0;
    }
    
    return apic_timer.system_time_ns + elapsed_ns;
}

/*
 * APIC timer interrupt handler
 */
static irq_return_t apic_timer_interrupt_handler(struct interrupt_context *ctx)
{
    atomic64_inc(&apic_timer.timer_interrupts);
    
    if (apic_timer.periodic_mode) {
        /* Update system time */
        apic_timer.system_time_ns += 1000000ULL;  /* 1ms increment */
    }
    
    /* Call timer subsystem */
    timer_interrupt_handler();
    
    apic_send_eoi();
    return IRQ_HANDLED;
}

/*
 * Initialize APIC timer
 */
int apic_timer_init(void)
{
    int ret;
    
    debuglog_printf("APIC Timer: Initializing Local APIC timer\n");
    
    if (apic_timer.initialized) {
        debuglog_printf("APIC Timer: Already initialized\n");
        return 0;
    }
    
    if (!apic_is_available()) {
        debuglog_printf("APIC Timer: Local APIC not available\n");
        return -1;
    }
    
    /* Calibrate timer frequency */
    ret = apic_timer_calibrate_frequency();
    if (ret != 0) {
        debuglog_printf("APIC Timer: Calibration failed\n");
        return ret;
    }
    
    /* Setup timer source structure */
    apic_timer.timer_source.name = "Local APIC Timer";
    apic_timer.timer_source.type = INTCTL_LOCAL_APIC;    apic_timer.timer_source.per_cpu = true;
    apic_timer.timer_source.frequency = apic_timer.base_frequency;
    apic_timer.timer_source.high_precision = true;
    apic_timer.timer_source.init = apic_timer_source_init;
    apic_timer.timer_source.read_counter = apic_timer_source_read;
    apic_timer.timer_source.set_periodic = apic_timer_source_set_periodic;
    apic_timer.timer_source.set_oneshot = apic_timer_source_set_oneshot;
    
    /* Register interrupt handler */
    idt_register_handler(IRQ_APIC_TIMER, 
                        (interrupt_handler_t)apic_timer_interrupt_handler, 
                        "APIC Timer");
    
    /* Register as timer source */
    register_timer_source(&apic_timer.timer_source);

    /* Export global timer_source for timer_abstraction.c */
    apic_timer_source = apic_timer.timer_source;

    apic_timer.initialized = true;
    
    debuglog_printf("APIC Timer: Initialization complete\n");
    
    return 0;
}

/*
 * Timer source initialization callback
 */
static int apic_timer_source_init(struct timer_source *self)
{
    (void)self;  /* APIC timer uses global state */
    debuglog_printf("APIC Timer: Timer source initialized\n");
    return 0;
}

/*
 * Timer source cleanup callback
 */
static void apic_timer_source_cleanup(struct timer_source *source)
{
    apic_timer_source_stop(source);
    debuglog_printf("APIC Timer: Timer source cleaned up\n");
}

/*
 * Timer source read callback
 */
static uint64_t apic_timer_source_read(struct timer_source *self)
{
    (void)self;  /* APIC timer uses global state */
    return apic_timer_read_counter();
}

/*
 * Timer source set periodic callback
 */
NO_OPTIMIZE static void apic_timer_source_set_periodic(struct timer_source *self, uint64_t period_ns)
{
    uint32_t frequency_hz;
    int ret;

    (void)self;  /* APIC timer uses global state */

    if (period_ns == 0) {
        return;
    }
    
    /* Convert period to frequency */
    frequency_hz = 1000000000ULL / period_ns;
    
    if (frequency_hz < 1) {
        frequency_hz = 1;
    } else if (frequency_hz > 10000) {
        frequency_hz = 10000;  /* Limit to 10kHz */
    }
    
    spinlock_acquire(&apic_timer.lock);
    
    /* Set periodic mode */
    ret = apic_timer_set_mode(true);
    if (ret == 0) {
        ret = apic_timer_set_frequency(frequency_hz);
        if (ret == 0) {
            apic_timer.running = true;
        }
    }
    
    spinlock_release(&apic_timer.lock);
    
    debuglog_printf("APIC Timer: Set periodic mode, period=%llu ns, freq=%u Hz\n",
                (unsigned long long)period_ns, frequency_hz);

    (void)ret;  /* Suppress unused variable warning */
}

/*
 * Timer source set oneshot callback
 */
NO_OPTIMIZE static void apic_timer_source_set_oneshot(struct timer_source *self, uint64_t timeout_ns)
{
    uint32_t initial_count;
    int ret;

    (void)self;  /* APIC timer uses global state */

    if (timeout_ns == 0) {
        return;
    }
    
    spinlock_acquire(&apic_timer.lock);
    
    /* Set oneshot mode */
    ret = apic_timer_set_mode(false);
    if (ret != 0) {
        spinlock_release(&apic_timer.lock);
        return;
    }
    
    /* Set divisor to 1 for maximum precision */
    apic_write_register(LAPIC_REG_TIMER_DCR, APIC_TIMER_DIV_1);
    apic_timer.current_divisor = 1;
    
    /* Calculate initial count for timeout */
    initial_count = (timeout_ns * apic_timer.base_frequency) / 1000000000ULL;
    
    if (initial_count == 0) {
        initial_count = 1;
    } else if (initial_count > 0xFFFFFFFE) {
        initial_count = 0xFFFFFFFE;
    }
    
    /* Set initial count */
    apic_write_register(LAPIC_REG_TIMER_INITIAL, initial_count);
    
    apic_timer.running = true;
    
    spinlock_release(&apic_timer.lock);
    
    debuglog_printf("APIC Timer: Set oneshot mode, timeout=%llu ns, count=%u\n",
                (unsigned long long)timeout_ns, initial_count);

    (void)ret;  /* Suppress unused variable warning */
}

/*
 * Timer source stop callback
 */
static void apic_timer_source_stop(struct timer_source *source)
{
    spinlock_acquire(&apic_timer.lock);
    
    /* Stop timer by setting initial count to 0 */
    apic_write_register(LAPIC_REG_TIMER_INITIAL, 0);
    
    apic_timer.running = false;
    
    spinlock_release(&apic_timer.lock);
    
    debuglog_printf("APIC Timer: Stopped\n");
}

/*
 * Get APIC timer statistics
 */
void apic_timer_get_stats(struct apic_timer_stats *stats)
{
    if (!stats) {
        return;
    }
    
    stats->initialized = apic_timer.initialized;
    stats->running = apic_timer.running;
    stats->periodic_mode = apic_timer.periodic_mode;
    stats->base_frequency = apic_timer.base_frequency;
    stats->current_divisor = apic_timer.current_divisor;
    stats->timer_interrupts = atomic64_read(&apic_timer.timer_interrupts);
    stats->missed_deadlines = atomic64_read(&apic_timer.missed_deadlines);
    stats->system_time_ns = apic_timer.system_time_ns;
}

/*
 * Check if APIC timer is available
 */
bool apic_timer_is_available(void)
{
    return apic_timer.initialized && apic_is_available();
}

/*
 * Get current system time in nanoseconds
 */
uint64_t apic_timer_get_time_ns(void)
{
    if (!apic_timer.initialized) {
        return 0;
    }
    
    return apic_timer_read_counter();
}
