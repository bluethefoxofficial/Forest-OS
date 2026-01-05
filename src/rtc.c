/*
 * RTC (Real Time Clock) Driver for Forest OS
 * Provides real-time clock services and periodic interrupt generation
 * Supports CMOS RTC/MC146818A and compatible devices
 */

#include "interrupt.h"
#include "cpu_ops.h"
#include "debug.h"
#include "debuglog.h"
#include "panic.h"
#include "atomic.h"
#include "spinlock.h"
#include "timer.h"
#include "apic.h"

/* RTC I/O Ports */
#define RTC_INDEX_PORT          0x70    /* CMOS address/index port */
#define RTC_DATA_PORT           0x71    /* CMOS data port */

/* RTC Register Indices */
#define RTC_REG_SECONDS         0x00    /* Seconds (0-59) */
#define RTC_REG_SECONDS_ALARM   0x01    /* Seconds Alarm */
#define RTC_REG_MINUTES         0x02    /* Minutes (0-59) */
#define RTC_REG_MINUTES_ALARM   0x03    /* Minutes Alarm */
#define RTC_REG_HOURS           0x04    /* Hours (1-12 or 0-23) */
#define RTC_REG_HOURS_ALARM     0x05    /* Hours Alarm */
#define RTC_REG_DAY_WEEK        0x06    /* Day of Week (1-7) */
#define RTC_REG_DAY_MONTH       0x07    /* Day of Month (1-31) */
#define RTC_REG_MONTH           0x08    /* Month (1-12) */
#define RTC_REG_YEAR            0x09    /* Year (0-99) */
#define RTC_REG_STATUS_A        0x0A    /* Status Register A */
#define RTC_REG_STATUS_B        0x0B    /* Status Register B */
#define RTC_REG_STATUS_C        0x0C    /* Status Register C */
#define RTC_REG_STATUS_D        0x0D    /* Status Register D */

/* Extended RTC Registers */
#define RTC_REG_CENTURY         0x32    /* Century (19-20) - if available */

/* Status Register A Bits */
#define RTC_STAT_A_UIP          0x80    /* Update In Progress */
#define RTC_STAT_A_DV_MASK      0x70    /* Divider Select Mask */
#define RTC_STAT_A_DV_32KHZ     0x20    /* 32.768 kHz oscillator */
#define RTC_STAT_A_RS_MASK      0x0F    /* Rate Select Mask */

/* Status Register B Bits */
#define RTC_STAT_B_SET          0x80    /* Set bit - halt clock updates */
#define RTC_STAT_B_PIE          0x40    /* Periodic Interrupt Enable */
#define RTC_STAT_B_AIE          0x20    /* Alarm Interrupt Enable */
#define RTC_STAT_B_UIE          0x10    /* Update-ended Interrupt Enable */
#define RTC_STAT_B_SQWE         0x08    /* Square Wave Enable */
#define RTC_STAT_B_DM           0x04    /* Data Mode (0=BCD, 1=Binary) */
#define RTC_STAT_B_24H          0x02    /* 24-hour format */
#define RTC_STAT_B_DSE          0x01    /* Daylight Saving Enable */

/* Status Register C Bits (read-only) */
#define RTC_STAT_C_IRQF         0x80    /* Interrupt Request Flag */
#define RTC_STAT_C_PF           0x40    /* Periodic Interrupt Flag */
#define RTC_STAT_C_AF           0x20    /* Alarm Interrupt Flag */
#define RTC_STAT_C_UF           0x10    /* Update-ended Interrupt Flag */

/* Status Register D Bits */
#define RTC_STAT_D_VRT          0x80    /* Valid RAM and Time */

/* RTC Interrupt Rates */
#define RTC_RATE_NONE           0x00    /* No interrupts */
#define RTC_RATE_8192HZ         0x03    /* 8192 Hz */
#define RTC_RATE_4096HZ         0x04    /* 4096 Hz */
#define RTC_RATE_2048HZ         0x05    /* 2048 Hz */
#define RTC_RATE_1024HZ         0x06    /* 1024 Hz */
#define RTC_RATE_512HZ          0x07    /* 512 Hz */
#define RTC_RATE_256HZ          0x08    /* 256 Hz */
#define RTC_RATE_128HZ          0x09    /* 128 Hz */
#define RTC_RATE_64HZ           0x0A    /* 64 Hz */
#define RTC_RATE_32HZ           0x0B    /* 32 Hz */
#define RTC_RATE_16HZ           0x0C    /* 16 Hz */
#define RTC_RATE_8HZ            0x0D    /* 8 Hz */
#define RTC_RATE_4HZ            0x0E    /* 4 Hz */
#define RTC_RATE_2HZ            0x0F    /* 2 Hz */

/* Default settings */
#define RTC_DEFAULT_RATE        RTC_RATE_1024HZ
#define RTC_IRQ_NUMBER          8       /* RTC uses IRQ 8 */

/* RTC Time structure - defined in interrupt.h */
/* struct rtc_time is already defined in interrupt.h */

/* RTC Device State */
struct rtc_state {
    bool detected;
    bool initialized;
    bool periodic_enabled;
    bool update_enabled;
    bool alarm_enabled;
    bool binary_mode;
    bool hour_24_mode;
    uint8_t current_rate;
    uint32_t frequency;
    atomic64_t periodic_interrupts;
    atomic64_t update_interrupts;
    atomic64_t alarm_interrupts;
    struct rtc_time current_time;
    uint64_t last_update_time;
    spinlock_t lock;
};

static struct rtc_state rtc = {
    .detected = false,
    .initialized = false,
    .periodic_enabled = false,
    .update_enabled = false,
    .alarm_enabled = false,
    .lock = SPINLOCK_UNLOCKED
};

/* Rate table for frequency conversion */
static const uint32_t rtc_rate_table[] = {
    0,      /* 0x00 - No interrupts */
    0,      /* 0x01 - Invalid */
    0,      /* 0x02 - Invalid */
    8192,   /* 0x03 - 8192 Hz */
    4096,   /* 0x04 - 4096 Hz */
    2048,   /* 0x05 - 2048 Hz */
    1024,   /* 0x06 - 1024 Hz */
    512,    /* 0x07 - 512 Hz */
    256,    /* 0x08 - 256 Hz */
    128,    /* 0x09 - 128 Hz */
    64,     /* 0x0A - 64 Hz */
    32,     /* 0x0B - 32 Hz */
    16,     /* 0x0C - 16 Hz */
    8,      /* 0x0D - 8 Hz */
    4,      /* 0x0E - 4 Hz */
    2       /* 0x0F - 2 Hz */
};

/* Function prototypes */
static uint8_t rtc_read_register(uint8_t reg);
static void rtc_write_register(uint8_t reg, uint8_t value);
static int rtc_wait_for_update_complete(void);
static uint8_t rtc_bcd_to_binary(uint8_t bcd);
static void rtc_read_time_raw(struct rtc_time *time);
static void rtc_convert_time(struct rtc_time *time);
static irq_return_t rtc_interrupt_handler(int vector, struct interrupt_context *ctx);
static int rtc_set_periodic_rate(uint8_t rate);

/*
 * Read RTC register
 */
static uint8_t rtc_read_register(uint8_t reg)
{
    outb(RTC_INDEX_PORT, reg);
    io_wait();
    return inb(RTC_DATA_PORT);
}

/*
 * Write RTC register
 */
static void rtc_write_register(uint8_t reg, uint8_t value)
{
    outb(RTC_INDEX_PORT, reg);
    io_wait();
    outb(RTC_DATA_PORT, value);
    io_wait();
}

/*
 * Wait for RTC update cycle to complete
 */
static int rtc_wait_for_update_complete(void)
{
    int timeout = 1000000;  /* 1 second timeout */
    
    /* Wait for UIP to clear */
    while ((rtc_read_register(RTC_REG_STATUS_A) & RTC_STAT_A_UIP) && timeout--) {
        cpu_pause();
    }
    
    return timeout > 0 ? 0 : -1;
}

/*
 * Convert BCD to binary
 */
static uint8_t rtc_bcd_to_binary(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/*
 * Read raw time from RTC
 */
static void rtc_read_time_raw(struct rtc_time *time)
{
    if (rtc_wait_for_update_complete() != 0) {
        debug_print("RTC: Timeout waiting for update cycle\n");
        return;
    }
    
    time->second = rtc_read_register(RTC_REG_SECONDS);
    time->minute = rtc_read_register(RTC_REG_MINUTES);
    time->hour = rtc_read_register(RTC_REG_HOURS);
    time->day = rtc_read_register(RTC_REG_DAY_MONTH);
    time->month = rtc_read_register(RTC_REG_MONTH);
    time->year = rtc_read_register(RTC_REG_YEAR);
    time->weekday = rtc_read_register(RTC_REG_DAY_WEEK);
    time->century = rtc_read_register(RTC_REG_CENTURY);  /* May not be available */
}

/*
 * Convert time from RTC format to binary
 */
static void rtc_convert_time(struct rtc_time *time)
{
    if (!rtc.binary_mode) {
        /* Convert from BCD to binary */
        time->second = rtc_bcd_to_binary(time->second);
        time->minute = rtc_bcd_to_binary(time->minute);
        time->hour = rtc_bcd_to_binary(time->hour);
        time->day = rtc_bcd_to_binary(time->day);
        time->month = rtc_bcd_to_binary(time->month);
        time->year = rtc_bcd_to_binary(time->year);
        time->weekday = rtc_bcd_to_binary(time->weekday);
        time->century = rtc_bcd_to_binary(time->century);
    }
    
    /* Handle 12-hour to 24-hour conversion */
    if (!rtc.hour_24_mode) {
        bool pm = (time->hour & 0x80) != 0;
        time->hour &= 0x7F;
        
        if (!rtc.binary_mode) {
            time->hour = rtc_bcd_to_binary(time->hour);
        }
        
        if (pm && time->hour != 12) {
            time->hour += 12;
        } else if (!pm && time->hour == 12) {
            time->hour = 0;
        }
    }
    
    /* Handle century - assume 21st century if not available or invalid */
    if (time->century < 19 || time->century > 21) {
        time->century = (time->year >= 70) ? 19 : 20;
    }
}

/*
 * RTC interrupt handler
 */
static irq_return_t rtc_interrupt_handler(int vector, struct interrupt_context *ctx)
{
    uint8_t status_c;
    unsigned long flags;

    (void)vector;
    
    spin_lock_irqsave(&rtc.lock, flags);
    
    /* Read status register C to clear interrupt flags */
    status_c = rtc_read_register(RTC_REG_STATUS_C);
    
    /* Handle different interrupt types */
    if (status_c & RTC_STAT_C_PF) {
        /* Periodic interrupt */
        atomic64_inc(&rtc.periodic_interrupts);
    }
    
    if (status_c & RTC_STAT_C_UF) {
        /* Update-ended interrupt */
        atomic64_inc(&rtc.update_interrupts);
        
        /* Update cached time */
        rtc_read_time_raw(&rtc.current_time);
        rtc_convert_time(&rtc.current_time);
        rtc.last_update_time = ctx->timestamp;
    }
    
    if (status_c & RTC_STAT_C_AF) {
        /* Alarm interrupt */
        atomic64_inc(&rtc.alarm_interrupts);
        debug_print("RTC: Alarm interrupt triggered\n");
    }
    
    spin_unlock_irqrestore(&rtc.lock, flags);
    
    /* Send EOI to interrupt controller */
    if (pic_is_available()) {
        pic_send_eoi(RTC_IRQ_NUMBER);
    } else if (ioapic_is_available()) {
        apic_send_eoi();
    }
    
    return IRQ_HANDLED;
}

/*
 * Set RTC periodic interrupt rate
 */
static int rtc_set_periodic_rate(uint8_t rate)
{
    uint8_t status_a, status_b;
    unsigned long flags;
    
    if (rate > 0x0F) {
        return -1;
    }
    
    spin_lock_irqsave(&rtc.lock, flags);
    
    /* Disable interrupts during configuration */
    status_b = rtc_read_register(RTC_REG_STATUS_B);
    rtc_write_register(RTC_REG_STATUS_B, status_b & ~RTC_STAT_B_PIE);
    
    /* Set the new rate */
    status_a = rtc_read_register(RTC_REG_STATUS_A);
    status_a = (status_a & ~RTC_STAT_A_RS_MASK) | rate;
    rtc_write_register(RTC_REG_STATUS_A, status_a);
    
    /* Re-enable interrupts if they were enabled */
    if (rtc.periodic_enabled) {
        status_b |= RTC_STAT_B_PIE;
        rtc_write_register(RTC_REG_STATUS_B, status_b);
    }
    
    rtc.current_rate = rate;
    rtc.frequency = (rate < 16) ? rtc_rate_table[rate] : 0;
    
    spin_unlock_irqrestore(&rtc.lock, flags);
    
    debug_print("RTC: Set periodic rate to %d (%u Hz)\n", rate, rtc.frequency);
    return 0;
}

/*
 * Initialize RTC driver
 */
int rtc_interrupt_init(void)
{
    uint8_t status_b, status_d;
    unsigned long flags;
    
    debug_print("RTC: Initializing Real Time Clock\n");
    
    if (rtc.initialized) {
        debug_print("RTC: Already initialized\n");
        return 0;
    }
    
    /* Check if RTC is present */
    status_d = rtc_read_register(RTC_REG_STATUS_D);
    if (!(status_d & RTC_STAT_D_VRT)) {
        debug_print("RTC: RTC battery low or invalid\n");
        return -1;
    }
    
    spin_lock_irqsave(&rtc.lock, flags);
    
    /* Read current configuration */
    status_b = rtc_read_register(RTC_REG_STATUS_B);
    
    /* Determine data format */
    rtc.binary_mode = (status_b & RTC_STAT_B_DM) != 0;
    rtc.hour_24_mode = (status_b & RTC_STAT_B_24H) != 0;
    
    /* Clear any pending interrupts */
    rtc_read_register(RTC_REG_STATUS_C);
    
    /* Configure RTC for binary mode and 24-hour format */
    status_b |= RTC_STAT_B_DM | RTC_STAT_B_24H;
    rtc_write_register(RTC_REG_STATUS_B, status_b);
    
    /* Set default periodic interrupt rate */
    rtc_set_periodic_rate(RTC_DEFAULT_RATE);
    
    /* Read initial time */
    rtc_read_time_raw(&rtc.current_time);
    rtc_convert_time(&rtc.current_time);
    rtc.last_update_time = read_tsc();
    
    rtc.detected = true;
    rtc.binary_mode = true;
    rtc.hour_24_mode = true;
    
    spin_unlock_irqrestore(&rtc.lock, flags);
    
    /* Register interrupt handler */
    idt_register_handler(IRQ_RTC, 
                        rtc_interrupt_handler, 
                        "RTC");
    
    /* Enable IRQ 8 on interrupt controller */
    if (pic_is_available()) {
        pic_unmask_irq(RTC_IRQ_NUMBER);
    } else if (ioapic_is_available()) {
        ioapic_enable_irq(RTC_IRQ_NUMBER);
    }
    
    rtc.initialized = true;
    
    debug_print("RTC: Initialization complete\n");
    debug_print("RTC: Time: %02d:%02d:%02d %02d/%02d/%02d%02d\n",
                rtc.current_time.hour, rtc.current_time.minute, rtc.current_time.second,
                rtc.current_time.month, rtc.current_time.day, 
                rtc.current_time.century, rtc.current_time.year);
    
    return 0;
}

/*
 * Enable RTC periodic interrupts
 */
int rtc_enable_periodic_interrupts(uint8_t rate)
{
    uint8_t status_b;
    unsigned long flags;
    
    if (!rtc.initialized) {
        return -1;
    }
    
    if (rate != 0 && rtc_set_periodic_rate(rate) != 0) {
        return -1;
    }
    
    spin_lock_irqsave(&rtc.lock, flags);
    
    status_b = rtc_read_register(RTC_REG_STATUS_B);
    status_b |= RTC_STAT_B_PIE;
    rtc_write_register(RTC_REG_STATUS_B, status_b);
    
    rtc.periodic_enabled = true;
    
    spin_unlock_irqrestore(&rtc.lock, flags);
    
    debug_print("RTC: Enabled periodic interrupts at %u Hz\n", rtc.frequency);
    return 0;
}

/*
 * Disable RTC periodic interrupts
 */
void rtc_disable_periodic_interrupts(void)
{
    uint8_t status_b;
    unsigned long flags;
    
    if (!rtc.initialized) {
        return;
    }
    
    spin_lock_irqsave(&rtc.lock, flags);
    
    status_b = rtc_read_register(RTC_REG_STATUS_B);
    status_b &= ~RTC_STAT_B_PIE;
    rtc_write_register(RTC_REG_STATUS_B, status_b);
    
    rtc.periodic_enabled = false;
    
    spin_unlock_irqrestore(&rtc.lock, flags);
    
    debug_print("RTC: Disabled periodic interrupts\n");
}

/*
 * Enable RTC update interrupts
 */
int rtc_enable_update_interrupts(void)
{
    uint8_t status_b;
    unsigned long flags;
    
    if (!rtc.initialized) {
        return -1;
    }
    
    spin_lock_irqsave(&rtc.lock, flags);
    
    status_b = rtc_read_register(RTC_REG_STATUS_B);
    status_b |= RTC_STAT_B_UIE;
    rtc_write_register(RTC_REG_STATUS_B, status_b);
    
    rtc.update_enabled = true;
    
    spin_unlock_irqrestore(&rtc.lock, flags);
    
    debug_print("RTC: Enabled update interrupts\n");
    return 0;
}

/*
 * Disable RTC update interrupts
 */
void rtc_disable_update_interrupts(void)
{
    uint8_t status_b;
    unsigned long flags;
    
    if (!rtc.initialized) {
        return;
    }
    
    spin_lock_irqsave(&rtc.lock, flags);
    
    status_b = rtc_read_register(RTC_REG_STATUS_B);
    status_b &= ~RTC_STAT_B_UIE;
    rtc_write_register(RTC_REG_STATUS_B, status_b);
    
    rtc.update_enabled = false;
    
    spin_unlock_irqrestore(&rtc.lock, flags);
    
    debug_print("RTC: Disabled update interrupts\n");
}

/*
 * Read current time from RTC (advanced version)
 */
int rtc_read_time_advanced(struct rtc_time *time)
{
    unsigned long flags;
    
    if (!rtc.initialized || !time) {
        return -1;
    }
    
    spin_lock_irqsave(&rtc.lock, flags);
    
    rtc_read_time_raw(time);
    rtc_convert_time(time);
    
    spin_unlock_irqrestore(&rtc.lock, flags);
    
    return 0;
}

/*
 * Get RTC statistics
 */
void rtc_get_stats(struct rtc_stats *stats)
{
    if (!stats) {
        return;
    }
    
    stats->detected = rtc.detected;
    stats->initialized = rtc.initialized;
    stats->periodic_enabled = rtc.periodic_enabled;
    stats->update_enabled = rtc.update_enabled;
    stats->alarm_enabled = rtc.alarm_enabled;
    stats->binary_mode = rtc.binary_mode;
    stats->hour_24_mode = rtc.hour_24_mode;
    stats->current_rate = rtc.current_rate;
    stats->frequency = rtc.frequency;
    stats->periodic_interrupts = atomic64_read(&rtc.periodic_interrupts);
    stats->update_interrupts = atomic64_read(&rtc.update_interrupts);
    stats->alarm_interrupts = atomic64_read(&rtc.alarm_interrupts);
    stats->last_update_time = rtc.last_update_time;
    stats->current_time = rtc.current_time;
}

/*
 * Check if RTC is available
 */
bool rtc_is_available(void)
{
    return rtc.detected && rtc.initialized;
}

/*
 * Read RTC time in microseconds
 * Returns a monotonically increasing timestamp based on RTC periodic interrupts
 */
uint64_t rtc_read_time_us(void)
{
    if (!rtc.initialized) {
        return 0;
    }

    uint64_t interrupts = atomic64_read(&rtc.periodic_interrupts);
    uint32_t frequency = rtc.frequency;

    if (frequency == 0) {
        return 0;
    }

    /* Convert interrupt count to microseconds based on configured frequency */
    return (interrupts * 1000000ULL) / frequency;
}
