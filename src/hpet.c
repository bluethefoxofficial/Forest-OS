/*
 * HPET (High Precision Event Timer) Driver for Forest OS
 * Provides high-resolution timing services and timer event generation
 * Integrates with ACPI discovery and Forest OS timer subsystem
 */

#include "interrupt.h"
#include "timer.h"
#include "acpi.h"
#include "cpu_ops.h"
#include "debuglog.h"
#include "panic.h"
#include "mm.h"
#include "memory.h"
#include "atomic.h"
#include "spinlock.h"

/* HPET Register Offsets */
#define HPET_GENERAL_CAP_ID         0x000   /* General Capabilities and ID */
#define HPET_GENERAL_CONFIG         0x010   /* General Configuration */
#define HPET_GENERAL_INT_STATUS     0x020   /* General Interrupt Status */
#define HPET_MAIN_COUNTER           0x0F0   /* Main Counter Value */
#define HPET_TIMER_CONFIG_CAP       0x100   /* Timer Configuration and Capabilities */
#define HPET_TIMER_COMPARATOR       0x108   /* Timer Comparator Value */
#define HPET_TIMER_FSB_INT_ROUTE    0x110   /* Timer FSB Interrupt Route */

/* Timer register offsets (for timer N, add N*0x20 to base) */
#define HPET_TIMER_OFFSET           0x20

/* HPET Configuration Register Bits */
#define HPET_CFG_ENABLE             0x001   /* Overall Enable */
#define HPET_CFG_LEG_RT_CNF         0x002   /* Legacy Replacement Route */

/* Timer Configuration Register Bits */
#define HPET_TN_INT_TYPE_CNF        0x002   /* Interrupt Type (0=Edge, 1=Level) */
#define HPET_TN_INT_ENB_CNF         0x004   /* Interrupt Enable */
#define HPET_TN_TYPE_CNF            0x008   /* Type (0=OneShot, 1=Periodic) */
#define HPET_TN_PER_INT_CAP         0x010   /* Periodic Interrupt Capable */
#define HPET_TN_SIZE_CAP            0x020   /* Size (0=32bit, 1=64bit) */
#define HPET_TN_VAL_SET_CNF         0x040   /* Value Set */
#define HPET_TN_32BIT_CNF           0x100   /* 32-bit Mode */
#define HPET_TN_FSB_EN_CNF          0x4000  /* FSB Interrupt Delivery Enable */

/* HPET Capabilities Register Fields */
#define HPET_CAP_REV_ID_SHIFT       0
#define HPET_CAP_NUM_TIM_SHIFT      8
#define HPET_CAP_COUNT_SIZE_SHIFT   13
#define HPET_CAP_LEG_RT_SHIFT       15
#define HPET_CAP_VENDOR_ID_SHIFT    16
#define HPET_CAP_COUNTER_CLK_SHIFT  32

/* ACPI HPET Table Structure */
typedef struct {
    char signature[4];      /* "HPET" */
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
    uint32_t event_timer_block_id;
    uint32_t base_address_lower;
    uint32_t base_address_upper;
    uint8_t hpet_number;
    uint16_t min_clock_tick;
    uint8_t page_protection;
} __attribute__((packed)) acpi_hpet_table_t;

/* Global timer_source export for timer_abstraction.c */
struct timer_source hpet_timer_source = {0};

/* HPET Timer structure */
struct hpet_timer {
    bool available;
    bool in_use;
    bool periodic_capable;
    bool supports_64bit;
    bool fsb_capable;
    uint32_t irq_routing_cap;
    uint32_t assigned_irq;
    atomic64_t interrupt_count;
    uint64_t period_femtoseconds;
    volatile uint64_t *config_reg;
    volatile uint64_t *comparator_reg;
    volatile uint64_t *fsb_route_reg;
};

/* HPET Device State */
struct hpet_device {
    bool detected;
    bool initialized;
    bool enabled;
    volatile uint64_t *base_address;
    uint32_t physical_address;
    uint64_t frequency;         /* Frequency in Hz */
    uint64_t period_femtoseconds;  /* Period in femtoseconds */
    uint32_t num_timers;
    uint32_t revision;
    uint32_t vendor_id;
    bool supports_64bit;
    bool supports_legacy_replacement;
    struct hpet_timer timers[8];  /* Up to 8 timers */
    atomic64_t main_counter_wraps;
    uint64_t last_counter_value;
    spinlock_t lock;
    struct timer_source timer_source;
};

static struct hpet_device hpet = {
    .detected = false,
    .initialized = false,
    .enabled = false,
    .lock = SPINLOCK_INIT("hpet_lock")
};

/* Function prototypes */
static int hpet_detect_from_acpi(void);
static int hpet_init_hardware(void);
static int hpet_init_timers(void);
uint64_t hpet_read_main_counter(void);
static uint64_t hpet_read_register(uint32_t offset);
static void hpet_write_register(uint32_t offset, uint64_t value);
static int hpet_allocate_timer(bool periodic);
static void hpet_free_timer(int timer_id);
static irq_return_t hpet_timer_interrupt_handler(struct interrupt_context *ctx);

/* Timer source callbacks - match struct timer_source function pointer signatures */
static int hpet_timer_source_init(struct timer_source *self);
static void hpet_timer_source_cleanup(struct timer_source *self);
static uint64_t hpet_timer_source_read(struct timer_source *self);
static void hpet_timer_source_set_periodic(struct timer_source *self, uint64_t period_ns);
static void hpet_timer_source_set_oneshot(struct timer_source *self, uint64_t timeout_ns);
static void hpet_timer_source_stop(struct timer_source *self);

/*
 * Read HPET register
 */
static uint64_t hpet_read_register(uint32_t offset)
{
    if (!hpet.base_address) {
        return 0;
    }
    
    return *(volatile uint64_t *)((uint8_t *)hpet.base_address + offset);
}

/*
 * Write HPET register
 */
static void hpet_write_register(uint32_t offset, uint64_t value)
{
    if (!hpet.base_address) {
        return;
    }
    
    *(volatile uint64_t *)((uint8_t *)hpet.base_address + offset) = value;
}

/*
 * Read HPET main counter
 */
uint64_t hpet_read_main_counter(void)
{
    uint64_t counter = hpet_read_register(HPET_MAIN_COUNTER);
    
    /* Detect counter wrapping */
    if (counter < hpet.last_counter_value) {
        atomic64_inc(&hpet.main_counter_wraps);
    }
    hpet.last_counter_value = counter;
    
    return counter;
}

/*
 * Detect HPET from ACPI tables
 */
static int hpet_detect_from_acpi(void)
{
    /* This is a placeholder for ACPI HPET table detection */
    /* In a complete implementation, this would:
     * 1. Search ACPI tables for HPET table
     * 2. Parse the HPET table to get base address and configuration
     * 3. Validate the HPET hardware
     */
    
    debuglog_printf("HPET: Detecting HPET from ACPI tables\n");
    
    /* For now, try the standard HPET base address */
    hpet.physical_address = 0xFED00000;  /* Standard HPET base address */
    
    /* Map HPET registers */
    hpet.base_address = (volatile uint64_t *)mm_map_physical_page(
        hpet.physical_address, 
        PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE
    );
    
    if (!hpet.base_address) {
        debuglog_printf("HPET: Failed to map HPET registers at 0x%08x\n", hpet.physical_address);
        return -1;
    }
    
    /* Read capabilities register to verify HPET presence */
    uint64_t cap_reg = hpet_read_register(HPET_GENERAL_CAP_ID);
    if (cap_reg == 0 || cap_reg == 0xFFFFFFFFFFFFFFFFULL) {
        debuglog_printf("HPET: HPET not found at standard address\n");
        return -1;
    }
    
    /* Parse capabilities */
    hpet.revision = cap_reg & 0xFF;
    hpet.num_timers = ((cap_reg >> HPET_CAP_NUM_TIM_SHIFT) & 0x1F) + 1;
    hpet.supports_64bit = (cap_reg >> HPET_CAP_COUNT_SIZE_SHIFT) & 1;
    hpet.supports_legacy_replacement = (cap_reg >> HPET_CAP_LEG_RT_SHIFT) & 1;
    hpet.vendor_id = (cap_reg >> HPET_CAP_VENDOR_ID_SHIFT) & 0xFFFF;
    hpet.period_femtoseconds = cap_reg >> HPET_CAP_COUNTER_CLK_SHIFT;
    
    /* Calculate frequency */
    hpet.frequency = 1000000000000000ULL / hpet.period_femtoseconds;  /* 1e15 / period_fs */
    
    debuglog_printf("HPET: Found HPET v%d.%d, %d timers, %s, freq=%lu Hz\n",
                (hpet.revision >> 4) & 0xF, hpet.revision & 0xF,
                hpet.num_timers,
                hpet.supports_64bit ? "64-bit" : "32-bit",
                hpet.frequency);
    
    hpet.detected = true;
    return 0;
}

/*
 * Initialize HPET hardware
 */
static int hpet_init_hardware(void)
{
    uint64_t config_reg;
    unsigned long flags;
    
    debuglog_printf("HPET: Initializing HPET hardware\n");
    
    spin_lock_irqsave(&hpet.lock, flags);
    
    /* Disable HPET initially */
    config_reg = hpet_read_register(HPET_GENERAL_CONFIG);
    config_reg &= ~HPET_CFG_ENABLE;
    hpet_write_register(HPET_GENERAL_CONFIG, config_reg);
    
    /* Reset main counter */
    hpet_write_register(HPET_MAIN_COUNTER, 0);
    hpet.last_counter_value = 0;
    atomic64_set(&hpet.main_counter_wraps, 0);
    
    /* Configure legacy replacement if supported and desired */
    if (hpet.supports_legacy_replacement) {
        config_reg |= HPET_CFG_LEG_RT_CNF;
        debuglog_printf("HPET: Enabling legacy replacement mode\n");
    }
    
    /* Enable HPET */
    config_reg |= HPET_CFG_ENABLE;
    hpet_write_register(HPET_GENERAL_CONFIG, config_reg);
    
    hpet.enabled = true;
    
    spin_unlock_irqrestore(&hpet.lock, flags);
    
    debuglog_printf("HPET: Hardware initialization complete\n");
    return 0;
}

/*
 * Initialize HPET timers
 */
static int hpet_init_timers(void)
{
    debuglog_printf("HPET: Initializing %d HPET timers\n", hpet.num_timers);
    
    for (uint32_t i = 0; i < hpet.num_timers; i++) {
        struct hpet_timer *timer = &hpet.timers[i];
        uint32_t timer_offset = HPET_TIMER_CONFIG_CAP + (i * HPET_TIMER_OFFSET);
        
        /* Read timer capabilities */
        uint64_t cap_reg = hpet_read_register(timer_offset);
        
        timer->available = true;
        timer->in_use = false;
        timer->periodic_capable = (cap_reg & HPET_TN_PER_INT_CAP) != 0;
        timer->supports_64bit = (cap_reg & HPET_TN_SIZE_CAP) != 0;
        timer->fsb_capable = (cap_reg & (1ULL << 15)) != 0;  /* FSB Interrupt Delivery Capable */
        timer->irq_routing_cap = (cap_reg >> 32) & 0xFFFFFFFF;
        timer->assigned_irq = 0;
        atomic64_set(&timer->interrupt_count, 0);
        
        /* Set up register pointers */
        timer->config_reg = (volatile uint64_t *)((uint8_t *)hpet.base_address + timer_offset);
        timer->comparator_reg = (volatile uint64_t *)((uint8_t *)hpet.base_address + timer_offset + 8);
        timer->fsb_route_reg = timer->fsb_capable ? 
            (volatile uint64_t *)((uint8_t *)hpet.base_address + timer_offset + 16) : NULL;
        
        /* Disable and configure timer */
        uint64_t config = cap_reg & 0xFFFFFFFF00000000ULL;  /* Keep capability bits */
        config &= ~HPET_TN_INT_ENB_CNF;  /* Disable interrupts */
        config &= ~HPET_TN_TYPE_CNF;     /* One-shot mode */
        
        if (timer->supports_64bit && hpet.supports_64bit) {
            config &= ~HPET_TN_32BIT_CNF;  /* 64-bit mode */
        } else {
            config |= HPET_TN_32BIT_CNF;   /* 32-bit mode */
        }
        
        *timer->config_reg = config;
        
        debuglog_printf("HPET: Timer %d: %s, %s, %s, IRQ cap=0x%08x\n", 
                   i,
                   timer->periodic_capable ? "periodic" : "oneshot-only",
                   timer->supports_64bit ? "64-bit" : "32-bit", 
                   timer->fsb_capable ? "FSB" : "legacy",
                   timer->irq_routing_cap);
    }
    
    return 0;
}

/*
 * Initialize HPET driver
 */
int hpet_init_advanced(void)
{
    int ret;
    
    debuglog_printf("HPET: Initializing HPET driver\n");
    
    if (hpet.initialized) {
        debuglog_printf("HPET: Already initialized\n");
        return 0;
    }
    
    /* Detect HPET hardware */
    ret = hpet_detect_from_acpi();
    if (ret != 0) {
        debuglog_printf("HPET: HPET hardware not detected\n");
        return ret;
    }
    
    /* Initialize hardware */
    ret = hpet_init_hardware();
    if (ret != 0) {
        debuglog_printf("HPET: Hardware initialization failed\n");
        return ret;
    }
    
    /* Initialize timers */
    ret = hpet_init_timers();
    if (ret != 0) {
        debuglog_printf("HPET: Timer initialization failed\n");
        return ret;
    }
    
    /* Set up timer source */
    hpet.timer_source.name = "HPET";
    hpet.timer_source.type = INTCTL_HPET;    hpet.timer_source.per_cpu = false;
    hpet.timer_source.frequency = hpet.frequency;
    hpet.timer_source.high_precision = true;
    hpet.timer_source.init = hpet_timer_source_init;
    hpet.timer_source.read_counter = hpet_timer_source_read;
    hpet.timer_source.set_periodic = hpet_timer_source_set_periodic;
    hpet.timer_source.set_oneshot = hpet_timer_source_set_oneshot;
    
    /* Register timer source */
    register_timer_source(&hpet.timer_source);

    /* Export global timer_source for timer_abstraction.c */
    hpet_timer_source = hpet.timer_source;

    hpet.initialized = true;
    
    debuglog_printf("HPET: Initialization complete\n");
    return 0;
}

/*
 * Allocate HPET timer for use
 */
static int hpet_allocate_timer(bool periodic)
{
    unsigned long flags;
    int timer_id = -1;
    
    spin_lock_irqsave(&hpet.lock, flags);
    
    for (uint32_t i = 0; i < hpet.num_timers; i++) {
        struct hpet_timer *timer = &hpet.timers[i];
        
        if (timer->available && !timer->in_use) {
            if (periodic && !timer->periodic_capable) {
                continue;  /* Need periodic timer but this one doesn't support it */
            }
            
            timer->in_use = true;
            timer_id = i;
            break;
        }
    }
    
    spin_unlock_irqrestore(&hpet.lock, flags);
    
    if (timer_id >= 0) {
        debuglog_printf("HPET: Allocated timer %d (%s mode)\n", 
                   timer_id, periodic ? "periodic" : "oneshot");
    }
    
    return timer_id;
}

/*
 * Free HPET timer
 */
static void hpet_free_timer(int timer_id)
{
    unsigned long flags;
    
    if (timer_id < 0 || timer_id >= (int)hpet.num_timers) {
        return;
    }
    
    spin_lock_irqsave(&hpet.lock, flags);
    
    struct hpet_timer *timer = &hpet.timers[timer_id];
    
    /* Disable timer */
    uint64_t config = *timer->config_reg;
    config &= ~HPET_TN_INT_ENB_CNF;
    *timer->config_reg = config;
    
    timer->in_use = false;
    timer->assigned_irq = 0;
    
    spin_unlock_irqrestore(&hpet.lock, flags);
    
    debuglog_printf("HPET: Freed timer %d\n", timer_id);
}

/*
 * HPET timer interrupt handler
 */
static irq_return_t hpet_timer_interrupt_handler(struct interrupt_context *ctx)
{
    /* Determine which timer caused the interrupt */
    uint64_t status = hpet_read_register(HPET_GENERAL_INT_STATUS);
    
    for (uint32_t i = 0; i < hpet.num_timers; i++) {
        if (status & (1ULL << i)) {
            atomic64_inc(&hpet.timers[i].interrupt_count);
            
            /* Clear interrupt status */
            hpet_write_register(HPET_GENERAL_INT_STATUS, 1ULL << i);
            
            debuglog_printf("HPET: Timer %d interrupt\n", i);
        }
    }
    
    /* Call timer subsystem */
    timer_interrupt_handler();
    
    return IRQ_HANDLED;
}

/*
 * Timer source callbacks
 */
static int hpet_timer_source_init(struct timer_source *self)
{
    (void)self;  /* HPET uses global state */
    debuglog_printf("HPET: Timer source initialized\n");
    return 0;
}

static void hpet_timer_source_cleanup(struct timer_source *self)
{
    (void)self;  /* HPET uses global state */
    debuglog_printf("HPET: Timer source cleaned up\n");
}

static uint64_t hpet_timer_source_read(struct timer_source *self)
{
    (void)self;  /* HPET uses global state */
    if (!hpet.enabled) {
        return 0;
    }

    uint64_t counter = hpet_read_main_counter();
    uint64_t wraps = atomic64_read(&hpet.main_counter_wraps);

    /* Convert counter value to nanoseconds */
    uint64_t total_ticks = (wraps << (hpet.supports_64bit ? 64 : 32)) + counter;
    uint64_t nanoseconds = (total_ticks * hpet.period_femtoseconds) / 1000000ULL;

    return nanoseconds;
}

static void hpet_timer_source_set_periodic(struct timer_source *self, uint64_t period_ns)
{
    (void)self;  /* HPET uses global state */
    int timer_id = hpet_allocate_timer(true);
    if (timer_id < 0) {
        debuglog_printf("HPET: No periodic timer available\n");
        return;
    }

    struct hpet_timer *timer = &hpet.timers[timer_id];

    /* Calculate comparator value */
    uint64_t period_ticks = (period_ns * 1000000ULL) / hpet.period_femtoseconds;

    /* Configure timer for periodic mode */
    uint64_t config = *timer->config_reg;
    config |= HPET_TN_TYPE_CNF;     /* Periodic mode */
    config |= HPET_TN_VAL_SET_CNF;  /* Value set enable */
    config |= HPET_TN_INT_ENB_CNF;  /* Enable interrupts */

    *timer->config_reg = config;
    *timer->comparator_reg = period_ticks;

    debuglog_printf("HPET: Set periodic timer %d, period=%lu ns (%lu ticks)\n",
                timer_id, period_ns, period_ticks);
}

static void hpet_timer_source_set_oneshot(struct timer_source *self, uint64_t timeout_ns)
{
    (void)self;  /* HPET uses global state */
    int timer_id = hpet_allocate_timer(false);
    if (timer_id < 0) {
        debuglog_printf("HPET: No timer available for oneshot\n");
        return;
    }

    struct hpet_timer *timer = &hpet.timers[timer_id];

    /* Calculate target value */
    uint64_t current_counter = hpet_read_main_counter();
    uint64_t timeout_ticks = (timeout_ns * 1000000ULL) / hpet.period_femtoseconds;
    uint64_t target_value = current_counter + timeout_ticks;

    /* Configure timer for oneshot mode */
    uint64_t config = *timer->config_reg;
    config &= ~HPET_TN_TYPE_CNF;    /* Oneshot mode */
    config |= HPET_TN_INT_ENB_CNF;  /* Enable interrupts */

    *timer->config_reg = config;
    *timer->comparator_reg = target_value;

    debuglog_printf("HPET: Set oneshot timer %d, timeout=%lu ns (target=%lu)\n",
                timer_id, timeout_ns, target_value);
}

static void hpet_timer_source_stop(struct timer_source *self)
{
    (void)self;  /* HPET uses global state */
    /* Stop all timers in use */
    for (uint32_t i = 0; i < hpet.num_timers; i++) {
        if (hpet.timers[i].in_use) {
            hpet_free_timer(i);
        }
    }
    
    debuglog_printf("HPET: All timers stopped\n");
}

/*
 * Get HPET statistics
 */
void hpet_get_stats(struct hpet_stats *stats)
{
    if (!stats) {
        return;
    }
    
    stats->detected = hpet.detected;
    stats->initialized = hpet.initialized;
    stats->enabled = hpet.enabled;
    stats->frequency = hpet.frequency;
    stats->period_femtoseconds = hpet.period_femtoseconds;
    stats->num_timers = hpet.num_timers;
    stats->supports_64bit = hpet.supports_64bit;
    stats->supports_legacy_replacement = hpet.supports_legacy_replacement;
    stats->main_counter_wraps = atomic64_read(&hpet.main_counter_wraps);
    
    if (hpet.enabled) {
        stats->current_counter = hpet_read_main_counter();
    } else {
        stats->current_counter = 0;
    }
    
    /* Copy timer statistics */
    for (uint32_t i = 0; i < hpet.num_timers && i < 8; i++) {
        struct hpet_timer *timer = &hpet.timers[i];
        
        stats->timers[i].in_use = timer->in_use;
        stats->timers[i].periodic_capable = timer->periodic_capable;
        stats->timers[i].supports_64bit = timer->supports_64bit;
        stats->timers[i].interrupt_count = atomic64_read(&timer->interrupt_count);
    }
}

/*
 * Check if HPET is available
 */
bool hpet_is_available(void)
{
    return hpet.detected && hpet.initialized;
}

/*
 * Get current HPET time in nanoseconds
 */
uint64_t hpet_get_time_ns(void)
{
    if (!hpet.enabled) {
        return 0;
    }

    return hpet_timer_source_read(NULL);
}

/*
 * Configure HPET for periodic operation at given frequency
 */
void hpet_configure_periodic(uint32_t frequency)
{
    if (!hpet.enabled || frequency == 0) {
        return;
    }

    /* Calculate period in nanoseconds from frequency */
    uint64_t period_ns = 1000000000ULL / frequency;

    hpet_timer_source_set_periodic(NULL, period_ns);
}