/*
 * Advanced Local APIC (Advanced Programmable Interrupt Controller) Driver
 * Supports APIC detection, initialization, timer setup, and interrupt routing
 * Integrates with Forest OS SMP and interrupt management systems
 */

#include "interrupt.h"
#include "smp.h"
#include "hardware.h"
#include "cpu_ops.h"
#include "debuglog.h"
#include "panic.h"
#include "mm.h"
#include "memory.h"
#include "atomic.h"
#include "timer.h"
#include "apic.h"

/* Local APIC Register Offsets */
#define LAPIC_REG_ID                0x020  /* APIC ID Register */
#define LAPIC_REG_VERSION           0x030  /* APIC Version Register */
#define LAPIC_REG_TPR               0x080  /* Task Priority Register */
#define LAPIC_REG_APR               0x090  /* Arbitration Priority Register */
#define LAPIC_REG_PPR               0x0A0  /* Processor Priority Register */
#define LAPIC_REG_EOI               0x0B0  /* End of Interrupt Register */
#define LAPIC_REG_RRD               0x0C0  /* Remote Read Register */
#define LAPIC_REG_LDR               0x0D0  /* Logical Destination Register */
#define LAPIC_REG_DFR               0x0E0  /* Destination Format Register */
#define LAPIC_REG_SPURIOUS          0x0F0  /* Spurious Interrupt Vector Register */
#define LAPIC_REG_ISR_BASE          0x100  /* In-Service Register (8 registers) */
#define LAPIC_REG_TMR_BASE          0x180  /* Trigger Mode Register (8 registers) */
#define LAPIC_REG_IRR_BASE          0x200  /* Interrupt Request Register (8 registers) */
#define LAPIC_REG_ERROR_STATUS      0x280  /* Error Status Register */
#define LAPIC_REG_ICR_LOW           0x300  /* Interrupt Command Register (Low) */
#define LAPIC_REG_ICR_HIGH          0x310  /* Interrupt Command Register (High) */
#define LAPIC_REG_TIMER_LVT         0x320  /* Timer Local Vector Table */
#define LAPIC_REG_THERMAL_LVT       0x330  /* Thermal Local Vector Table */
#define LAPIC_REG_PERF_LVT          0x340  /* Performance Counter LVT */
#define LAPIC_REG_LINT0_LVT         0x350  /* Local Interrupt 0 LVT */
#define LAPIC_REG_LINT1_LVT         0x360  /* Local Interrupt 1 LVT */
#define LAPIC_REG_ERROR_LVT         0x370  /* Error LVT */
#define LAPIC_REG_TIMER_INITIAL     0x380  /* Timer Initial Count Register */
#define LAPIC_REG_TIMER_CURRENT     0x390  /* Timer Current Count Register */
#define LAPIC_REG_TIMER_DCR         0x3E0  /* Timer Divide Configuration Register */

/* APIC MSRs */
#define MSR_APIC_BASE               0x1B
#define MSR_X2APIC_APICID           0x802
#define MSR_X2APIC_VERSION          0x803
#define MSR_X2APIC_TPR              0x808
#define MSR_X2APIC_PPR              0x80A
#define MSR_X2APIC_EOI              0x80B
#define MSR_X2APIC_LDR              0x80D
#define MSR_X2APIC_SPURIOUS         0x80F
#define MSR_X2APIC_ICR              0x830

/* APIC Base MSR flags */
#define APIC_BASE_BSP               0x100
#define APIC_BASE_X2APIC_ENABLE     0x400
#define APIC_BASE_GLOBAL_ENABLE     0x800

/* Spurious Vector Register flags */
#define APIC_SPURIOUS_ENABLE        0x100
#define APIC_SPURIOUS_FOCUS_DISABLE 0x200

/* LVT Entry flags */
#define APIC_LVT_MASKED             0x10000
#define APIC_LVT_TRIGGER_LEVEL      0x8000
#define APIC_LVT_REMOTE_IRR         0x4000
#define APIC_LVT_PIN_POLARITY       0x2000
#define APIC_LVT_PENDING            0x1000

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

/* ICR delivery modes */
#define APIC_ICR_DELIVERY_FIXED     0x000
#define APIC_ICR_DELIVERY_LOWPRI    0x100
#define APIC_ICR_DELIVERY_SMI       0x200
#define APIC_ICR_DELIVERY_NMI       0x400
#define APIC_ICR_DELIVERY_INIT      0x500
#define APIC_ICR_DELIVERY_STARTUP   0x600

/* ICR destination shorthand */
#define APIC_ICR_DEST_SELF          0x40000
#define APIC_ICR_DEST_ALL           0x80000
#define APIC_ICR_DEST_ALL_BUT_SELF  0xC0000

/* ICR flags */
#define APIC_ICR_LEVEL_ASSERT       0x4000
#define APIC_ICR_TRIGGER_LEVEL      0x8000
#define APIC_ICR_PENDING            0x1000

#if defined(__GNUC__) && !defined(__clang__)
#define NO_OPTIMIZE __attribute__((optimize("O0")))
#elif defined(__clang__)
#define NO_OPTIMIZE __attribute__((optnone))
#else
#define NO_OPTIMIZE
#endif

// Local helper to avoid calling through a 64-bit MSR wrapper that some 32-bit
// toolchains choke on at higher optimization levels.
static inline void apic_wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" :: "c"(msr), "a"(low), "d"(high));
}

/* APIC state management */
struct apic_state {
    bool detected;
    bool x2apic_available;
    bool x2apic_enabled;
    bool local_apic_enabled;
    uint64_t apic_base_msr;
    uint32_t *lapic_base;
    uint32_t apic_id;
    uint32_t version;
    uint32_t max_lvt_entries;
    uint32_t timer_frequency;
    uint32_t timer_calibration;
    atomic64_t timer_interrupts;
    atomic64_t spurious_interrupts;
    atomic64_t error_interrupts;
    spinlock_t lock;
};

static struct apic_state apic = {
    .detected = false,
    .x2apic_available = false,
    .x2apic_enabled = false,
    .local_apic_enabled = false,
    .lock = SPINLOCK_INIT("apic_lock")
};

/* Function prototypes */
static bool apic_detect_availability(void);
static bool apic_detect_x2apic(void);
static int apic_setup_spurious_vector(void);
static int apic_setup_lvt_entries(void);
static irq_return_t apic_spurious_handler(struct interrupt_context *ctx);
static irq_return_t apic_timer_handler(struct interrupt_context *ctx);
static irq_return_t apic_error_handler(struct interrupt_context *ctx);

/*
 * Detect APIC availability and capabilities
 */
static bool apic_detect_availability(void)
{
    const cpuid_info_t *cpu_info = hardware_get_cpuid_info();
    
    if (!cpu_info || !cpu_info->cpuid_supported) {
        debuglog_printf("APIC: CPUID not supported\n");
        return false;
    }
    
    /* Check for APIC support in CPUID */
    if (!(cpu_info->features.basic_edx & CPUID_FEAT_EDX_APIC)) {
        debuglog_printf("APIC: Local APIC not supported by CPU\n");
        return false;
    }
    
    /* Check APIC base MSR */
    apic.apic_base_msr = read_msr(MSR_APIC_BASE);
    if (!(apic.apic_base_msr & APIC_BASE_GLOBAL_ENABLE)) {
        debuglog_printf("APIC: APIC is disabled in MSR\n");
        return false;
    }
    
    /* Get APIC base address */
    uint64_t base_addr = apic.apic_base_msr & 0xFFFFF000ULL;
    
    /* Map APIC registers */
    apic.lapic_base = (uint32_t *)mm_map_physical_page(base_addr, 
                                                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE);
    if (!apic.lapic_base) {
        debuglog_printf("APIC: Failed to map APIC registers\n");
        return false;
    }
    
    /* Read APIC version to verify accessibility */
    uint32_t version = apic_read_register(LAPIC_REG_VERSION);
    if (version == 0xFFFFFFFF || version == 0) {
        debuglog_printf("APIC: Cannot read APIC version register\n");
        return false;
    }
    
    apic.version = version & 0xFF;
    apic.max_lvt_entries = ((version >> 16) & 0xFF) + 1;
    
    debuglog_printf("APIC: Local APIC detected, version=0x%02x, LVT entries=%d\n", 
                apic.version, apic.max_lvt_entries);
    
    return true;
}

/*
 * Detect x2APIC availability
 */
static bool apic_detect_x2apic(void)
{
    const cpuid_info_t *cpu_info = hardware_get_cpuid_info();
    
    if (cpu_info && (cpu_info->features.basic_ecx & CPUID_FEAT_ECX_X2APIC)) {
        debuglog_printf("APIC: x2APIC supported by CPU\n");
        return true;
    }
    
    return false;
}

/*
 * Read APIC register (handles both xAPIC and x2APIC modes)
 */
uint32_t apic_read_register(uint32_t reg)
{
    if (apic.x2apic_enabled) {
        /* x2APIC mode - use MSR */
        uint32_t msr = MSR_X2APIC_APICID + (reg >> 4);
        return (uint32_t)read_msr(msr);
    } else {
        /* xAPIC mode - memory mapped */
        return apic.lapic_base[reg / 4];
    }
}

/*
 * Write APIC register (handles both xAPIC and x2APIC modes)  
 */
void apic_write_register(uint32_t reg, uint32_t value)
{
    if (apic.x2apic_enabled) {
        /* x2APIC mode - use MSR */
        uint32_t msr = MSR_X2APIC_APICID + (reg >> 4);
        apic_wrmsr(msr, value);
    } else {
        /* xAPIC mode - memory mapped */
        apic.lapic_base[reg / 4] = value;
    }
}

/*
 * Enable Local APIC
 */
NO_OPTIMIZE int apic_enable_local_apic(void)
{
    spinlock_acquire(&apic.lock);
    
    /* Enable APIC in MSR */
    uint64_t apic_base = apic.apic_base_msr;
    apic_base |= APIC_BASE_GLOBAL_ENABLE;
    
    /* Enable x2APIC if available and desired */
    if (apic.x2apic_available && !apic.x2apic_enabled) {
        apic_base |= APIC_BASE_X2APIC_ENABLE;
        apic.x2apic_enabled = true;
        debuglog_printf("APIC: Enabling x2APIC mode\n");
    }
    
    apic_wrmsr(MSR_APIC_BASE, apic_base);
    apic.apic_base_msr = apic_base;
    
    /* Read APIC ID */
    apic.apic_id = apic_read_register(LAPIC_REG_ID);
    if (!apic.x2apic_enabled) {
        apic.apic_id = (apic.apic_id >> 24) & 0xFF;
    }
    
    apic.local_apic_enabled = true;
    
    spinlock_release(&apic.lock);
    
    debuglog_printf("APIC: Local APIC enabled, ID=0x%02x, %s mode\n", 
                apic.apic_id, apic.x2apic_enabled ? "x2APIC" : "xAPIC");
    
    return 0;
}

/*
 * Setup spurious interrupt vector
 */
static int apic_setup_spurious_vector(void)
{
    uint32_t spurious_reg;
    
    /* Configure spurious vector register */
    spurious_reg = APIC_SPURIOUS_VECTOR | APIC_SPURIOUS_ENABLE;
    
    /* Disable focus processor checking if available */
    if (apic.version >= 0x10) {
        spurious_reg |= APIC_SPURIOUS_FOCUS_DISABLE;
    }
    
    apic_write_register(LAPIC_REG_SPURIOUS, spurious_reg);
    
    /* Register spurious interrupt handler */
    idt_register_handler(APIC_SPURIOUS_VECTOR, 
                        (interrupt_handler_t)apic_spurious_handler, 
                        "APIC Spurious");
    
    debuglog_printf("APIC: Spurious vector configured (vector=0x%02x)\n", 
                APIC_SPURIOUS_VECTOR);
    
    return 0;
}

/*
 * Setup Local Vector Table (LVT) entries
 */
static int apic_setup_lvt_entries(void)
{
    /* Mask all LVT entries initially */
    apic_write_register(LAPIC_REG_TIMER_LVT, APIC_LVT_MASKED);
    apic_write_register(LAPIC_REG_LINT0_LVT, APIC_LVT_MASKED);
    apic_write_register(LAPIC_REG_LINT1_LVT, APIC_LVT_MASKED);
    apic_write_register(LAPIC_REG_ERROR_LVT, APIC_LVT_MASKED);
    
    if (apic.max_lvt_entries > 4) {
        apic_write_register(LAPIC_REG_THERMAL_LVT, APIC_LVT_MASKED);
    }
    
    if (apic.max_lvt_entries > 5) {
        apic_write_register(LAPIC_REG_PERF_LVT, APIC_LVT_MASKED);
    }
    
    /* Setup error interrupt */
    idt_register_handler(IRQ_APIC_ERROR, 
                        (interrupt_handler_t)apic_error_handler, 
                        "APIC Error");
    apic_write_register(LAPIC_REG_ERROR_LVT, IRQ_APIC_ERROR);
    
    debuglog_printf("APIC: LVT entries configured\n");
    
    return 0;
}

/*
 * Calibrate APIC timer frequency
 */
int apic_calibrate_timer(void)
{
    const uint32_t CALIBRATION_MS = 10;  /* Calibrate over 10ms */
    uint32_t tsc_start, tsc_end;
    uint32_t timer_start, timer_end;
    
    debuglog_printf("APIC: Calibrating timer frequency...\n");
    
    /* Set timer divisor to 16 */
    apic_write_register(LAPIC_REG_TIMER_DCR, APIC_TIMER_DIV_16);
    
    /* Set maximum initial count */
    apic_write_register(LAPIC_REG_TIMER_INITIAL, 0xFFFFFFFF);
    
    /* Read TSC and timer start values */
    tsc_start = read_tsc();
    timer_start = apic_read_register(LAPIC_REG_TIMER_CURRENT);
    
    /* Wait for calibration period using PIT or other timing source */
    timer_sleep_ms(CALIBRATION_MS);
    
    /* Read TSC and timer end values */
    tsc_end = read_tsc();
    timer_end = apic_read_register(LAPIC_REG_TIMER_CURRENT);
    
    /* Calculate timer frequency */
    uint32_t timer_ticks = timer_start - timer_end;
    uint32_t tsc_ticks = tsc_end - tsc_start;
    
    if (timer_ticks > 0) {
        apic.timer_frequency = (timer_ticks * 1000) / CALIBRATION_MS;
        apic.timer_frequency *= 16;  /* Account for divisor */
        apic.timer_calibration = 1;
        
        debuglog_printf("APIC: Timer frequency calibrated to %u Hz\n", 
                   apic.timer_frequency);
    } else {
        debuglog_printf("APIC: Timer calibration failed\n");
        apic.timer_frequency = 100000000;  /* Fallback: 100MHz */
        apic.timer_calibration = 0;
    }
    
    /* Stop timer */
    apic_write_register(LAPIC_REG_TIMER_LVT, APIC_LVT_MASKED);
    apic_write_register(LAPIC_REG_TIMER_INITIAL, 0);
    
    return 0;
}

/*
 * Initialize Local APIC
 */
int local_apic_init(void)
{
    int ret;
    
    debuglog_printf("APIC: Initializing Local APIC\n");
    
    if (apic.detected) {
        debuglog_printf("APIC: Already initialized\n");
        return 0;
    }
    
    /* Detect APIC availability */
    if (!apic_detect_availability()) {
        debuglog_printf("APIC: Local APIC not available\n");
        return -1;
    }
    
    /* Detect x2APIC support */
    apic.x2apic_available = apic_detect_x2apic();
    
    /* Enable Local APIC */
    ret = apic_enable_local_apic();
    if (ret != 0) {
        debuglog_printf("APIC: Failed to enable Local APIC\n");
        return ret;
    }
    
    /* Setup spurious vector */
    ret = apic_setup_spurious_vector();
    if (ret != 0) {
        debuglog_printf("APIC: Failed to setup spurious vector\n");
        return ret;
    }
    
    /* Setup LVT entries */
    ret = apic_setup_lvt_entries();
    if (ret != 0) {
        debuglog_printf("APIC: Failed to setup LVT entries\n");
        return ret;
    }
    
    /* Calibrate timer */
    ret = apic_calibrate_timer();
    if (ret != 0) {
        debuglog_printf("APIC: Failed to calibrate timer\n");
        /* Non-fatal, continue */
    }
    
    /* Clear any pending errors */
    apic_write_register(LAPIC_REG_ERROR_STATUS, 0);
    apic_read_register(LAPIC_REG_ERROR_STATUS);
    
    /* Set Task Priority Register to accept all interrupts */
    apic_write_register(LAPIC_REG_TPR, 0);
    
    apic.detected = true;
    
    debuglog_printf("APIC: Local APIC initialization complete\n");
    
    /* Disable PIC when APIC is available */
    pic_8259a_disable();
    
    return 0;
}

/*
 * Send End of Interrupt to APIC
 */
void apic_send_eoi(void)
{
    if (!apic.detected || !apic.local_apic_enabled) {
        return;
    }
    
    apic_write_register(LAPIC_REG_EOI, 0);
}

/*
 * Send Inter-Processor Interrupt
 */
int apic_send_ipi(uint32_t dest_apic_id, uint32_t vector, uint32_t delivery_mode)
{
    uint32_t icr_low, icr_high;
    int timeout = 1000;
    
    if (!apic.detected || !apic.local_apic_enabled) {
        return -1;
    }
    
    /* Wait for any previous IPI to complete */
    while ((apic_read_register(LAPIC_REG_ICR_LOW) & APIC_ICR_PENDING) && timeout--) {
        cpu_pause();
    }
    
    if (timeout <= 0) {
        debuglog_printf("APIC: IPI send timeout\n");
        return -1;
    }
    
    /* Setup ICR */
    icr_high = dest_apic_id << 24;
    icr_low = vector | delivery_mode | APIC_ICR_LEVEL_ASSERT;
    
    /* Send IPI */
    apic_write_register(LAPIC_REG_ICR_HIGH, icr_high);
    apic_write_register(LAPIC_REG_ICR_LOW, icr_low);
    
    return 0;
}

/*
 * APIC spurious interrupt handler
 */
static irq_return_t apic_spurious_handler(struct interrupt_context *ctx)
{
    atomic64_inc(&apic.spurious_interrupts);
    
    debuglog_printf("APIC: Spurious interrupt received\n");
    
    /* Do not send EOI for spurious interrupts */
    return IRQ_HANDLED;
}

/*
 * APIC timer interrupt handler
 */
static irq_return_t apic_timer_handler(struct interrupt_context *ctx)
{
    atomic64_inc(&apic.timer_interrupts);
    
    /* Handle timer interrupt */
    timer_interrupt_handler();
    
    apic_send_eoi();
    return IRQ_HANDLED;
}

/*
 * APIC error interrupt handler
 */
static irq_return_t apic_error_handler(struct interrupt_context *ctx)
{
    uint32_t error_status;
    
    atomic64_inc(&apic.error_interrupts);
    
    /* Read and clear error status */
    error_status = apic_read_register(LAPIC_REG_ERROR_STATUS);
    apic_write_register(LAPIC_REG_ERROR_STATUS, 0);
    apic_read_register(LAPIC_REG_ERROR_STATUS);  /* Required dummy read */
    
    debuglog_printf("APIC: Error interrupt, status=0x%08x\n", error_status);
    
    apic_send_eoi();
    return IRQ_HANDLED;
}

/*
 * Get APIC statistics
 */
void apic_get_stats(struct apic_stats *stats)
{
    if (!stats) {
        return;
    }
    
    stats->detected = apic.detected;
    stats->local_apic_enabled = apic.local_apic_enabled;
    stats->x2apic_available = apic.x2apic_available;
    stats->x2apic_enabled = apic.x2apic_enabled;
    stats->apic_id = apic.apic_id;
    stats->version = apic.version;
    stats->max_lvt_entries = apic.max_lvt_entries;
    stats->timer_frequency = apic.timer_frequency;
    stats->timer_interrupts = atomic64_read(&apic.timer_interrupts);
    stats->spurious_interrupts = atomic64_read(&apic.spurious_interrupts);
    stats->error_interrupts = atomic64_read(&apic.error_interrupts);
}

/*
 * Check if APIC is available
 */
bool apic_is_available(void)
{
    return apic.detected && apic.local_apic_enabled;
}

/*
 * Get Local APIC ID
 */
uint32_t apic_get_id(void)
{
    if (!apic.detected) {
        return 0;
    }
    
    return apic.apic_id;
}
