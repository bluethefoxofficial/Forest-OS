/*
 * PIT (Programmable Interval Timer) Driver for Forest OS
 * Provides legacy timer support and fallback timing services
 * Essential for system initialization and timer calibration
 */

#include "interrupt.h"
#include "timer.h"
#include "cpu_ops.h"
#include "debug.h"
#include "panic.h"
#include "atomic.h"
#include "spinlock.h"

/* PIT I/O Port Addresses */
#define PIT_CHANNEL0_DATA       0x40    /* Channel 0 data port */
#define PIT_CHANNEL1_DATA       0x41    /* Channel 1 data port */
#define PIT_CHANNEL2_DATA       0x42    /* Channel 2 data port */
#define PIT_COMMAND             0x43    /* Command register */

/* PIT Command Register Bits */
#define PIT_CMD_CHANNEL_SHIFT   6       /* Channel select bits */
#define PIT_CMD_ACCESS_SHIFT    4       /* Access mode bits */
#define PIT_CMD_MODE_SHIFT      1       /* Operating mode bits */
#define PIT_CMD_BCD             0x01    /* BCD/Binary countdown select */

/* PIT Channels */
#define PIT_CHANNEL0            0       /* System timer */
#define PIT_CHANNEL1            1       /* RAM refresh (legacy) */
#define PIT_CHANNEL2            2       /* PC speaker */

/* PIT Access Modes */
#define PIT_ACCESS_LATCH        0       /* Latch count value */
#define PIT_ACCESS_LOW          1       /* Low byte only */
#define PIT_ACCESS_HIGH         2       /* High byte only */
#define PIT_ACCESS_BOTH         3       /* Low byte, then high byte */

/* PIT Operating Modes */
#define PIT_MODE_TERMINAL       0       /* Interrupt on terminal count */
#define PIT_MODE_ONESHOT        1       /* Hardware re-triggerable one-shot */
#define PIT_MODE_RATEGEN        2       /* Rate generator */
#define PIT_MODE_SQUAREWAVE     3       /* Square wave generator */
#define PIT_MODE_SOFTSTROBE     4       /* Software triggered strobe */
#define PIT_MODE_HARDSTROBE     5       /* Hardware triggered strobe */

/* PIT Constants */
#define PIT_FREQUENCY           1193182 /* PIT base frequency in Hz */
#define PIT_MAX_COUNT           65535   /* Maximum 16-bit count value */
#define PIT_MIN_FREQUENCY       18      /* Minimum reasonable frequency */
#define PIT_MAX_FREQUENCY       1000000 /* Maximum reasonable frequency */

/* Default PIT timer frequency (1000 Hz = 1ms period) */
#define PIT_DEFAULT_FREQUENCY   1000

/* Global timer_source export for timer_abstraction.c */
struct timer_source pit_timer_source = {0};

/* PIT State */
struct pit_state {
    bool initialized;
    bool channel0_in_use;
    bool channel2_in_use;
    uint32_t current_frequency;
    uint16_t current_divisor;
    atomic64_t timer_interrupts;
    atomic64_t calibration_ticks;
    uint64_t system_time_ns;
    uint64_t last_interrupt_time;
    spinlock_t lock;
    struct timer_source timer_source;
};

static struct pit_state pit = {
    .initialized = false,
    .channel0_in_use = false,
    .channel2_in_use = false,
    .current_frequency = PIT_DEFAULT_FREQUENCY,
    .lock = SPINLOCK_UNLOCKED
};

/* Function prototypes */
static int pit_set_frequency(uint32_t frequency);
static int pit_set_channel_mode(uint8_t channel, uint8_t access, uint8_t mode);
static void pit_set_count(uint8_t channel, uint16_t count);
static uint16_t pit_read_count(uint8_t channel);
static irq_return_t pit_timer_interrupt_handler(struct interrupt_context *ctx);
static uint64_t pit_calculate_ns_per_tick(void);

/* Timer source callbacks */
static int pit_timer_source_init(struct timer_source *source);
static void pit_timer_source_cleanup(struct timer_source *source);
static uint64_t pit_timer_source_read(struct timer_source *source);
static int pit_timer_source_set_periodic(struct timer_source *source, uint64_t period_ns);
static int pit_timer_source_set_oneshot(struct timer_source *source, uint64_t timeout_ns);
static void pit_timer_source_stop(struct timer_source *source);

/*
 * Calculate nanoseconds per PIT tick based on current frequency
 */
static uint64_t pit_calculate_ns_per_tick(void)
{
    return (1000000000ULL * pit.current_divisor) / PIT_FREQUENCY;
}

/*
 * Set PIT channel mode and access
 */
static int pit_set_channel_mode(uint8_t channel, uint8_t access, uint8_t mode)
{
    uint8_t command;
    
    if (channel > 2 || access > 3 || mode > 5) {
        return -1;
    }
    
    command = (channel << PIT_CMD_CHANNEL_SHIFT) |
              (access << PIT_CMD_ACCESS_SHIFT) |
              (mode << PIT_CMD_MODE_SHIFT);
    
    outb(PIT_COMMAND, command);
    
    return 0;
}

/*
 * Set PIT channel count value
 */
static void pit_set_count(uint8_t channel, uint16_t count)
{
    uint16_t port = PIT_CHANNEL0_DATA + channel;
    
    outb(port, count & 0xFF);        /* Low byte */
    outb(port, (count >> 8) & 0xFF); /* High byte */
}

/*
 * Read PIT channel count value
 */
static uint16_t pit_read_count(uint8_t channel)
{
    uint16_t port = PIT_CHANNEL0_DATA + channel;
    uint16_t count;
    
    /* Latch the current count */
    pit_set_channel_mode(channel, PIT_ACCESS_LATCH, 0);
    
    /* Read the latched count */
    count = inb(port);              /* Low byte */
    count |= inb(port) << 8;        /* High byte */
    
    return count;
}

/*
 * Set PIT frequency for channel 0
 */
static int pit_set_frequency(uint32_t frequency)
{
    uint16_t divisor;
    unsigned long flags;
    
    if (frequency < PIT_MIN_FREQUENCY || frequency > PIT_MAX_FREQUENCY) {
        debug_print("PIT: Invalid frequency %u Hz\n", frequency);
        return -1;
    }
    
    /* Calculate divisor */
    divisor = PIT_FREQUENCY / frequency;
    if (divisor == 0) {
        divisor = 1;
    } else if (divisor > PIT_MAX_COUNT) {
        divisor = PIT_MAX_COUNT;
    }
    
    spin_lock_irqsave(&pit.lock, flags);
    
    /* Set channel 0 to mode 2 (rate generator) with 16-bit access */
    pit_set_channel_mode(PIT_CHANNEL0, PIT_ACCESS_BOTH, PIT_MODE_RATEGEN);
    
    /* Set the divisor */
    pit_set_count(PIT_CHANNEL0, divisor);
    
    /* Update state */
    pit.current_frequency = PIT_FREQUENCY / divisor;
    pit.current_divisor = divisor;
    pit.channel0_in_use = true;
    
    spin_unlock_irqrestore(&pit.lock, flags);
    
    debug_print("PIT: Set frequency to %u Hz (divisor=%u, actual=%u Hz)\n",
                frequency, divisor, pit.current_frequency);
    
    return 0;
}

/*
 * PIT timer interrupt handler
 */
static irq_return_t pit_timer_interrupt_handler(struct interrupt_context *ctx)
{
    uint64_t current_time = ctx->timestamp;
    uint64_t period_ns = pit_calculate_ns_per_tick();
    
    atomic64_inc(&pit.timer_interrupts);
    
    /* Update system time */
    pit.system_time_ns += period_ns;
    pit.last_interrupt_time = current_time;
    
    /* Call timer subsystem */
    timer_interrupt_handler();
    
    /* Send EOI to PIC (PIT uses IRQ 0) */
    pic_send_eoi(0);
    
    return IRQ_HANDLED;
}

/*
 * Wrapper functions for timer_source interface
 * These accept struct timer_source *self to match the interface
 */
static void pit_set_periodic_wrapper(struct timer_source *self, uint64_t ns)
{
    (void)self;  /* PIT doesn't need self-reference */
    if (ns == 0) return;
    uint32_t frequency = 1000000000ULL / ns;
    if (frequency < PIT_MIN_FREQUENCY) frequency = PIT_MIN_FREQUENCY;
    else if (frequency > PIT_MAX_FREQUENCY) frequency = PIT_MAX_FREQUENCY;
    pit_set_frequency(frequency);
}

static void pit_set_oneshot_wrapper(struct timer_source *self, uint64_t ns)
{
    (void)self;  /* PIT doesn't need self-reference */
    if (ns == 0) return;
    uint32_t ticks = (ns * PIT_FREQUENCY) / 1000000000ULL;
    if (ticks == 0) ticks = 1;
    else if (ticks > PIT_MAX_COUNT) ticks = PIT_MAX_COUNT;
    pit_set_channel_mode(PIT_CHANNEL0, PIT_ACCESS_BOTH, PIT_MODE_TERMINAL);
    pit_set_count(PIT_CHANNEL0, ticks);
    pit.channel0_in_use = true;
}

static uint64_t pit_read_counter_wrapper(struct timer_source *self)
{
    (void)self;  /* PIT doesn't need self-reference */
    return pit.system_time_ns;
}

static int pit_init_wrapper(struct timer_source *self)
{
    (void)self;  /* PIT doesn't need self-reference */
    return pit_init_advanced();
}

static void pit_disable_wrapper(struct timer_source *self)
{
    (void)self;  /* PIT doesn't need self-reference */
    pit_disable_system_timer();
}

/*
 * Initialize PIT driver
 */
int pit_init_advanced(void)
{
    int ret;
    
    debug_print("PIT: Initializing Programmable Interval Timer\n");
    
    if (pit.initialized) {
        debug_print("PIT: Already initialized\n");
        return 0;
    }
    
    /* Reset PIT state */
    pit.system_time_ns = 0;
    pit.last_interrupt_time = 0;
    atomic64_set(&pit.timer_interrupts, 0);
    atomic64_set(&pit.calibration_ticks, 0);
    
    /* Set default frequency */
    ret = pit_set_frequency(PIT_DEFAULT_FREQUENCY);
    if (ret != 0) {
        debug_print("PIT: Failed to set default frequency\n");
        return ret;
    }
    
    /* Register interrupt handler for IRQ 0 */
    idt_register_handler(IRQ_TIMER, 
                        (interrupt_handler_t)pit_timer_interrupt_handler, 
                        "PIT Timer");
    
    /* Set up timer source */
    pit.timer_source.name = "PIT";
    pit.timer_source.type = INTCTL_PIT;
    pit.timer_source.frequency = PIT_FREQUENCY;
    pit.timer_source.high_precision = false;
    pit.timer_source.per_cpu = false;
    pit.timer_source.init = pit_init_wrapper;
    pit.timer_source.enable = NULL;
    pit.timer_source.disable = pit_disable_wrapper;
    pit.timer_source.set_frequency = NULL;
    pit.timer_source.get_frequency = NULL;
    pit.timer_source.set_periodic = pit_set_periodic_wrapper;
    pit.timer_source.set_oneshot = pit_set_oneshot_wrapper;
    pit.timer_source.read_counter = pit_read_counter_wrapper;
    pit.timer_source.calibrate = NULL;
    
    /* Register timer source */
    register_timer_source(&pit.timer_source);

    /* Export global timer_source for timer_abstraction.c */
    pit_timer_source = pit.timer_source;

    pit.initialized = true;
    
    debug_print("PIT: Initialization complete\n");
    return 0;
}

/*
 * Delay using PIT for calibration purposes
 */
void pit_delay_ms(uint32_t milliseconds)
{
    uint64_t target_ticks = atomic64_read(&pit.timer_interrupts) + milliseconds;
    
    while (atomic64_read(&pit.timer_interrupts) < target_ticks) {
        cpu_pause();
    }
}

/*
 * Delay using PIT busy-wait (for early initialization)
 */
void pit_udelay(uint32_t microseconds)
{
    uint32_t ticks = (microseconds * PIT_FREQUENCY) / 1000000;
    uint16_t initial_count, current_count;
    
    if (ticks == 0) {
        return;
    }
    
    /* Use channel 2 for timing */
    pit_set_channel_mode(PIT_CHANNEL2, PIT_ACCESS_BOTH, PIT_MODE_ONESHOT);
    pit_set_count(PIT_CHANNEL2, ticks);
    
    initial_count = pit_read_count(PIT_CHANNEL2);
    
    /* Wait for the count to reach zero */
    do {
        current_count = pit_read_count(PIT_CHANNEL2);
    } while (current_count > 0 && current_count <= initial_count);
}

/*
 * Get PIT frequency for calibration
 */
uint32_t pit_get_frequency(void)
{
    return PIT_FREQUENCY;
}

/*
 * Timer source callbacks
 */
static int pit_timer_source_init(struct timer_source *source)
{
    debug_print("PIT: Timer source initialized\n");
    return 0;
}

static void pit_timer_source_cleanup(struct timer_source *source)
{
    pit_timer_source_stop(source);
    debug_print("PIT: Timer source cleaned up\n");
}

static uint64_t pit_timer_source_read(struct timer_source *source)
{
    /* Return accumulated system time */
    return pit.system_time_ns;
}

static int pit_timer_source_set_periodic(struct timer_source *source, uint64_t period_ns)
{
    uint32_t frequency;
    
    if (period_ns == 0) {
        return -1;
    }
    
    /* Convert period to frequency */
    frequency = 1000000000ULL / period_ns;
    
    if (frequency < PIT_MIN_FREQUENCY) {
        frequency = PIT_MIN_FREQUENCY;
    } else if (frequency > PIT_MAX_FREQUENCY) {
        frequency = PIT_MAX_FREQUENCY;
    }
    
    debug_print("PIT: Setting periodic mode, period=%lu ns, freq=%u Hz\n", 
                period_ns, frequency);
    
    return pit_set_frequency(frequency);
}

static int pit_timer_source_set_oneshot(struct timer_source *source, uint64_t timeout_ns)
{
    uint32_t ticks;
    unsigned long flags;
    
    if (timeout_ns == 0) {
        return -1;
    }
    
    /* Convert timeout to PIT ticks */
    ticks = (timeout_ns * PIT_FREQUENCY) / 1000000000ULL;
    
    if (ticks == 0) {
        ticks = 1;
    } else if (ticks > PIT_MAX_COUNT) {
        ticks = PIT_MAX_COUNT;
    }
    
    spin_lock_irqsave(&pit.lock, flags);
    
    /* Set channel 0 to mode 0 (interrupt on terminal count) */
    pit_set_channel_mode(PIT_CHANNEL0, PIT_ACCESS_BOTH, PIT_MODE_TERMINAL);
    pit_set_count(PIT_CHANNEL0, ticks);
    
    pit.channel0_in_use = true;
    
    spin_unlock_irqrestore(&pit.lock, flags);
    
    debug_print("PIT: Set oneshot mode, timeout=%lu ns (%u ticks)\n", 
                timeout_ns, ticks);
    
    return 0;
}

static void pit_timer_source_stop(struct timer_source *source)
{
    unsigned long flags;
    
    spin_lock_irqsave(&pit.lock, flags);
    
    /* Disable channel 0 by setting it to a high count */
    pit_set_channel_mode(PIT_CHANNEL0, PIT_ACCESS_BOTH, PIT_MODE_RATEGEN);
    pit_set_count(PIT_CHANNEL0, PIT_MAX_COUNT);
    
    pit.channel0_in_use = false;
    
    spin_unlock_irqrestore(&pit.lock, flags);
    
    debug_print("PIT: Timer stopped\n");
}

/*
 * Calibrate other timing sources using PIT
 */
uint64_t pit_calibrate_timing_source(uint64_t (*read_counter)(void), uint32_t ms_duration)
{
    uint64_t pit_start, pit_end;
    uint64_t counter_start, counter_end;
    uint64_t pit_ticks, counter_ticks;
    uint64_t frequency = 0;
    
    if (!read_counter || ms_duration == 0) {
        return 0;
    }
    
    debug_print("PIT: Calibrating timing source over %u ms\n", ms_duration);
    
    /* Record start values */
    pit_start = atomic64_read(&pit.timer_interrupts);
    counter_start = read_counter();
    
    /* Wait for calibration period */
    pit_delay_ms(ms_duration);
    
    /* Record end values */
    pit_end = atomic64_read(&pit.timer_interrupts);
    counter_end = read_counter();
    
    /* Calculate frequencies */
    pit_ticks = pit_end - pit_start;
    counter_ticks = counter_end - counter_start;
    
    if (pit_ticks > 0) {
        /* Calculate counter frequency based on PIT ticks */
        frequency = (counter_ticks * pit.current_frequency) / pit_ticks;
        
        debug_print("PIT: Calibration complete - counter freq = %lu Hz\n", frequency);
        debug_print("PIT: PIT ticks: %lu, Counter ticks: %lu\n", pit_ticks, counter_ticks);
    } else {
        debug_print("PIT: Calibration failed - no PIT ticks recorded\n");
    }
    
    return frequency;
}

/*
 * Get PIT statistics
 */
void pit_get_stats(struct pit_stats *stats)
{
    if (!stats) {
        return;
    }
    
    stats->initialized = pit.initialized;
    stats->channel0_in_use = pit.channel0_in_use;
    stats->channel2_in_use = pit.channel2_in_use;
    stats->current_frequency = pit.current_frequency;
    stats->current_divisor = pit.current_divisor;
    stats->timer_interrupts = atomic64_read(&pit.timer_interrupts);
    stats->system_time_ns = pit.system_time_ns;
    stats->last_interrupt_time = pit.last_interrupt_time;
}

/*
 * Check if PIT is available
 */
bool pit_is_available(void)
{
    return pit.initialized;
}

/*
 * Get current PIT time in nanoseconds
 */
uint64_t pit_get_time_ns(void)
{
    if (!pit.initialized) {
        return 0;
    }

    return pit_timer_source_read(&pit.timer_source);
}

/*
 * Read PIT counter value (returns system time in nanoseconds)
 */
uint64_t pit_read_counter(void)
{
    return pit.system_time_ns;
}

/*
 * Configure PIT with specified frequency
 */
void pit_configure(uint32_t frequency)
{
    if (!pit.initialized) {
        return;
    }

    pit_set_frequency(frequency);
}

/*
 * Enable PIT channel for system timer use
 */
int pit_enable_system_timer(void)
{
    if (!pit.initialized) {
        return -1;
    }
    
    /* Enable IRQ 0 on the interrupt controller */
    if (pic_is_available()) {
        pic_unmask_irq(0);
    } else if (ioapic_is_available()) {
        ioapic_enable_irq(0);
    }
    
    debug_print("PIT: System timer enabled\n");
    return 0;
}

/*
 * Disable PIT channel for system timer use
 */
void pit_disable_system_timer(void)
{
    if (!pit.initialized) {
        return;
    }
    
    /* Disable IRQ 0 on the interrupt controller */
    if (pic_is_available()) {
        pic_mask_irq(0);
    } else if (ioapic_is_available()) {
        ioapic_disable_irq(0);
    }
    
    debug_print("PIT: System timer disabled\n");
}