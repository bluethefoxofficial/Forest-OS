/*
 * NMI (Non-Maskable Interrupt) Handler for Forest OS
 * Provides critical error detection and system diagnostics
 * Handles memory parity errors, watchdog timeouts, and hardware failures
 */

#include "include/interrupt.h"
#include "include/cpu_ops.h"
#include "include/debuglog.h"
#include "include/panic.h"
#include "include/atomic.h"
#include "include/spinlock.h"
#include "include/mm.h"
#include "include/timer.h"
#include "include/system.h"

/* Stubs for missing CR register functions */
static inline unsigned long cpu_read_cr0(void) {
    unsigned long val;
    __asm__ volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

static inline unsigned long cpu_read_cr3(void) {
    unsigned long val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline unsigned long cpu_read_cr4(void) {
    unsigned long val;
    __asm__ volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

/* Architecture-aware register access macros */
#if ARCH_64BIT
    #define FRAME_IP(frame) ((frame).rip)
    #define FRAME_SP(frame) ((frame).rsp)
    #define FRAME_FLAGS(frame) ((frame).rflags)
    #define REG_TYPE uint64_t
#else
    #define FRAME_IP(frame) ((frame).eip)
    #define FRAME_SP(frame) ((frame).useresp)
    #define FRAME_FLAGS(frame) ((frame).eflags)
    #define REG_TYPE uint32_t
#endif

/* NMI Control and Status Ports */
#define NMI_STATUS_CONTROL_A        0x61    /* System Control Port A */
#define NMI_STATUS_CONTROL_B        0x70    /* CMOS/RTC Index (NMI mask) */
#define NMI_EXTENDED_STATUS         0x462   /* Extended NMI status (some systems) */

/* NMI Control Port A Bits */
#define NMI_CTRL_A_REFRESH_TOGGLE   0x10    /* Refresh toggle */
#define NMI_CTRL_A_CHANNEL_CHECK    0x40    /* Channel check */
#define NMI_CTRL_A_PARITY_CHECK     0x80    /* Parity check */

/* NMI Control Port B (CMOS Index) */
#define NMI_DISABLE_BIT             0x80    /* NMI disable bit */

/* NMI Source Identification */
#define NMI_SOURCE_UNKNOWN          0x00
#define NMI_SOURCE_MEMORY_PARITY    0x01
#define NMI_SOURCE_CHANNEL_CHECK    0x02
#define NMI_SOURCE_WATCHDOG         0x03
#define NMI_SOURCE_PCIe_ERROR       0x04
#define NMI_SOURCE_THERMAL          0x05
#define NMI_SOURCE_VOLTAGE          0x06
#define NMI_SOURCE_SOFTWARE         0x07

/* NMI Handler States */
#define NMI_STATE_NORMAL            0x00
#define NMI_STATE_IN_HANDLER        0x01
#define NMI_STATE_RECURSIVE         0x02
#define NMI_STATE_DISABLED          0x03

/* NMI Statistics and State */
struct nmi_state {
    bool enabled;
    atomic_t handler_state;
    atomic64_t total_nmis;
    atomic64_t memory_parity_errors;
    atomic64_t channel_check_errors;
    atomic64_t watchdog_timeouts;
    atomic64_t pcie_errors;
    atomic64_t thermal_events;
    atomic64_t voltage_events;
    atomic64_t software_nmis;
    atomic64_t unknown_nmis;
    atomic64_t recursive_nmis;
    uint64_t last_nmi_time;
    uint64_t last_nmi_rip;
    uint32_t last_nmi_source;
    uint32_t consecutive_nmis;
    spinlock_t lock;
    bool panic_on_nmi;
    bool log_all_nmis;
    uint32_t nmi_flood_threshold;
    uint64_t nmi_flood_window_ms;
};

static struct nmi_state nmi = {
    .enabled = true,
    .handler_state = ATOMIC_INIT(NMI_STATE_NORMAL),
    .panic_on_nmi = false,
    .log_all_nmis = true,
    .nmi_flood_threshold = 100,    /* 100 NMIs in window is a flood */
    .nmi_flood_window_ms = 1000,   /* 1 second window */
    .lock = SPINLOCK_UNLOCKED
};

/* NMI source information */
struct nmi_source_info {
    const char *name;
    const char *description;
    bool recoverable;
    bool requires_immediate_action;
};

static const struct nmi_source_info nmi_sources[] = {
    [NMI_SOURCE_UNKNOWN] = {
        "Unknown", "Unknown NMI source", false, true
    },
    [NMI_SOURCE_MEMORY_PARITY] = {
        "Memory Parity", "Memory parity error detected", false, true
    },
    [NMI_SOURCE_CHANNEL_CHECK] = {
        "Channel Check", "I/O channel check error", false, true
    },
    [NMI_SOURCE_WATCHDOG] = {
        "Watchdog", "Watchdog timer timeout", true, false
    },
    [NMI_SOURCE_PCIe_ERROR] = {
        "PCIe Error", "PCIe correctable/uncorrectable error", true, true
    },
    [NMI_SOURCE_THERMAL] = {
        "Thermal", "Thermal event or overheating", true, false
    },
    [NMI_SOURCE_VOLTAGE] = {
        "Voltage", "Power supply voltage event", false, true
    },
    [NMI_SOURCE_SOFTWARE] = {
        "Software", "Software-triggered NMI", true, false
    }
};

/* Function prototypes */
static uint32_t nmi_identify_source(void);
static void nmi_handle_memory_parity(struct interrupt_context *ctx);
static void nmi_handle_channel_check(struct interrupt_context *ctx);
static void nmi_handle_watchdog_timeout(struct interrupt_context *ctx);
static void nmi_handle_pcie_error(struct interrupt_context *ctx);
static void nmi_handle_thermal_event(struct interrupt_context *ctx);
static void nmi_handle_voltage_event(struct interrupt_context *ctx);
static void nmi_handle_software_nmi(struct interrupt_context *ctx);
static void nmi_handle_unknown(struct interrupt_context *ctx);
static bool nmi_is_flood_condition(uint64_t current_time);
static void nmi_dump_system_state(struct interrupt_context *ctx);
static void nmi_log_event(uint32_t source, struct interrupt_context *ctx);

/*
 * Identify NMI source by reading hardware status registers
 */
static uint32_t nmi_identify_source(void)
{
    uint8_t status_a = inb(NMI_STATUS_CONTROL_A);
    
    /* Check for memory parity error */
    if (status_a & NMI_CTRL_A_PARITY_CHECK) {
        return NMI_SOURCE_MEMORY_PARITY;
    }
    
    /* Check for channel check error */
    if (status_a & NMI_CTRL_A_CHANNEL_CHECK) {
        return NMI_SOURCE_CHANNEL_CHECK;
    }
    
    /* Check for watchdog timeout (system-specific) */
    /* This would need to be implemented based on specific hardware */
    
    /* Default to unknown source */
    return NMI_SOURCE_UNKNOWN;
}

/*
 * Handle memory parity error
 */
static void nmi_handle_memory_parity(struct interrupt_context *ctx)
{
    REG_TYPE fault_address = 0;
    (void)fault_address; /* Suppress unused warning */

    atomic64_inc(&nmi.memory_parity_errors);

    debuglog_printf("NMI: CRITICAL - Memory parity error detected!\n");
    debuglog_printf("NMI: This indicates possible RAM failure or corruption\n");

    /* Try to get fault address from memory controller if available */
    /* This is hardware-specific and would need to be implemented */

    debuglog_printf("NMI: Memory parity error at IP=0x%lx\n", (unsigned long)FRAME_IP(ctx->frame));
    
    /* Memory parity errors are usually not recoverable */
    if (nmi.panic_on_nmi) {
        panic("Unrecoverable memory parity error");
    }
    
    /* Log for post-mortem analysis */
    debuglog_printf("NMI: System continuing despite memory error - data corruption possible\n");
}

/*
 * Handle I/O channel check error
 */
static void nmi_handle_channel_check(struct interrupt_context *ctx)
{
    atomic64_inc(&nmi.channel_check_errors);
    
    debuglog_printf("NMI: I/O channel check error detected\n");
    debuglog_printf("NMI: Possible expansion card or bus error\n");
    
    /* Clear the error condition */
    uint8_t status = inb(NMI_STATUS_CONTROL_A);
    status |= NMI_CTRL_A_CHANNEL_CHECK;
    outb(NMI_STATUS_CONTROL_A, status);
    status &= ~NMI_CTRL_A_CHANNEL_CHECK;
    outb(NMI_STATUS_CONTROL_A, status);
    
    debuglog_printf("NMI: I/O channel error cleared\n");
}

/*
 * Handle watchdog timeout
 */
static void nmi_handle_watchdog_timeout(struct interrupt_context *ctx)
{
    atomic64_inc(&nmi.watchdog_timeouts);
    
    debuglog_printf("NMI: Watchdog timer timeout detected\n");
    debuglog_printf("NMI: System may have hung or deadlocked\n");
    
    /* Reset watchdog if possible */
    /* This is hardware-specific implementation */
    
    /* Dump system state for analysis */
    nmi_dump_system_state(ctx);
    
    /* Watchdog timeouts might be recoverable depending on implementation */
    if (!nmi.panic_on_nmi) {
        debuglog_printf("NMI: Attempting to continue after watchdog timeout\n");
    } else {
        panic("Watchdog timeout - system may be deadlocked");
    }
}

/*
 * Handle PCIe error
 */
static void nmi_handle_pcie_error(struct interrupt_context *ctx)
{
    atomic64_inc(&nmi.pcie_errors);
    
    debuglog_printf("NMI: PCIe error detected\n");
    
    /* Read PCIe error status registers if available */
    /* This would be implemented based on specific PCIe hardware */
    
    debuglog_printf("NMI: PCIe error handling not fully implemented\n");
}

/*
 * Handle thermal event
 */
static void nmi_handle_thermal_event(struct interrupt_context *ctx)
{
    atomic64_inc(&nmi.thermal_events);
    
    debuglog_printf("NMI: Thermal event detected\n");
    debuglog_printf("NMI: System may be overheating\n");
    
    /* Check CPU temperature if thermal monitoring is available */
    /* This would integrate with thermal management subsystem */
    
    debuglog_printf("NMI: Consider checking system cooling\n");
}

/*
 * Handle voltage event
 */
static void nmi_handle_voltage_event(struct interrupt_context *ctx)
{
    atomic64_inc(&nmi.voltage_events);
    
    debuglog_printf("NMI: Power supply voltage event detected\n");
    debuglog_printf("NMI: Power supply may be failing\n");
    
    /* This is usually not recoverable */
    if (nmi.panic_on_nmi) {
        panic("Power supply voltage error");
    }
}

/*
 * Handle software-triggered NMI
 */
static void nmi_handle_software_nmi(struct interrupt_context *ctx)
{
    atomic64_inc(&nmi.software_nmis);
    
    debuglog_printf("NMI: Software-triggered NMI\n");
    
    /* Software NMIs are usually for debugging or testing */
    debuglog_printf("NMI: Software NMI at IP=0x%lx\n", (unsigned long)FRAME_IP(ctx->frame));
}

/*
 * Handle unknown NMI source
 */
static void nmi_handle_unknown(struct interrupt_context *ctx)
{
    atomic64_inc(&nmi.unknown_nmis);
    
    debuglog_printf("NMI: Unknown NMI source detected\n");
    debuglog_printf("NMI: This may indicate hardware failure or spurious NMI\n");
    
    /* Dump system state for debugging */
    nmi_dump_system_state(ctx);
    
    /* Unknown NMIs are suspicious */
    if (nmi.panic_on_nmi) {
        panic("Unknown NMI source");
    }
}

/*
 * Check for NMI flood condition
 */
static bool nmi_is_flood_condition(uint64_t current_time)
{
    uint64_t window_start = current_time - (nmi.nmi_flood_window_ms * 1000000ULL); /* Convert to nanoseconds */
    
    /* Simple flood detection - count recent NMIs */
    if (nmi.last_nmi_time > window_start) {
        nmi.consecutive_nmis++;
    } else {
        nmi.consecutive_nmis = 1;
    }
    
    return nmi.consecutive_nmis >= nmi.nmi_flood_threshold;
}

/*
 * Dump system state for NMI analysis
 */
static void nmi_dump_system_state(struct interrupt_context *ctx)
{
    debuglog_printf("\n=== NMI SYSTEM STATE DUMP ===\n");
    debuglog_printf("IP: 0x%lx, SP: 0x%lx\n", (unsigned long)FRAME_IP(ctx->frame), (unsigned long)FRAME_SP(ctx->frame));
    debuglog_printf("FLAGS: 0x%lx, CS: 0x%04x\n", (unsigned long)FRAME_FLAGS(ctx->frame), (uint16_t)ctx->frame.cs);
    debuglog_printf("Timestamp: %lu\n", (unsigned long)ctx->timestamp);

    /* Read some system registers */
    REG_TYPE cr0 = cpu_read_cr0();
    REG_TYPE cr2 = cpu_read_cr2();
    REG_TYPE cr3 = cpu_read_cr3();
    REG_TYPE cr4 = cpu_read_cr4();
    
    debuglog_printf("CR0: 0x%lx, CR2: 0x%lx\n", cr0, cr2);
    debuglog_printf("CR3: 0x%lx, CR4: 0x%lx\n", cr3, cr4);
    
    /* Check interrupt state */
    debuglog_printf("Interrupts: %s\n", irq_are_enabled() ? "enabled" : "disabled");
    
    debuglog_printf("=== END NMI STATE DUMP ===\n\n");
}

/*
 * Log NMI event
 */
static void nmi_log_event(uint32_t source, struct interrupt_context *ctx)
{
    if (source >= sizeof(nmi_sources) / sizeof(nmi_sources[0])) {
        source = NMI_SOURCE_UNKNOWN;
    }
    
    const struct nmi_source_info *info = &nmi_sources[source];
    
    debuglog_printf("NMI: %s - %s (IP=0x%lx)\n",
                info->name, info->description, (unsigned long)FRAME_IP(ctx->frame));
    
    if (!info->recoverable) {
        debuglog_printf("NMI: This is a non-recoverable error\n");
    }
    
    if (info->requires_immediate_action) {
        debuglog_printf("NMI: Immediate action required\n");
    }
}

/*
 * Main NMI handler
 */
irq_return_t nmi_handler_c(struct interrupt_context *ctx)
{
    uint32_t old_state, source;
    uint64_t current_time = ctx->timestamp;
    unsigned long flags;
    
    /* Prevent recursive NMI handling */
    old_state = atomic_cmpxchg(&nmi.handler_state, NMI_STATE_NORMAL, NMI_STATE_IN_HANDLER);
    if (old_state != NMI_STATE_NORMAL) {
        atomic64_inc(&nmi.recursive_nmis);
        atomic_set(&nmi.handler_state, NMI_STATE_RECURSIVE);
        
        debuglog_printf("NMI: Recursive NMI detected! System may be critically unstable\n");
        panic("Recursive NMI - critical system failure");
        return IRQ_HANDLED;
    }
    
    spin_lock_irqsave(&nmi.lock, flags);
    
    /* Update statistics */
    atomic64_inc(&nmi.total_nmis);
    nmi.last_nmi_time = current_time;
    nmi.last_nmi_rip = FRAME_IP(ctx->frame);
    
    /* Check for flood condition */
    if (nmi_is_flood_condition(current_time)) {
        spin_unlock_irqrestore(&nmi.lock, flags);
        atomic_set(&nmi.handler_state, NMI_STATE_NORMAL);
        
        debuglog_printf("NMI: Flood condition detected - disabling NMI handling\n");
        nmi_disable();
        panic("NMI flood detected - critical system instability");
        return IRQ_HANDLED;
    }
    
    /* Identify NMI source */
    source = nmi_identify_source();
    nmi.last_nmi_source = source;
    
    spin_unlock_irqrestore(&nmi.lock, flags);
    
    /* Log the event */
    if (nmi.log_all_nmis) {
        nmi_log_event(source, ctx);
    }
    
    /* Handle based on source */
    switch (source) {
        case NMI_SOURCE_MEMORY_PARITY:
            nmi_handle_memory_parity(ctx);
            break;
            
        case NMI_SOURCE_CHANNEL_CHECK:
            nmi_handle_channel_check(ctx);
            break;
            
        case NMI_SOURCE_WATCHDOG:
            nmi_handle_watchdog_timeout(ctx);
            break;
            
        case NMI_SOURCE_PCIe_ERROR:
            nmi_handle_pcie_error(ctx);
            break;
            
        case NMI_SOURCE_THERMAL:
            nmi_handle_thermal_event(ctx);
            break;
            
        case NMI_SOURCE_VOLTAGE:
            nmi_handle_voltage_event(ctx);
            break;
            
        case NMI_SOURCE_SOFTWARE:
            nmi_handle_software_nmi(ctx);
            break;
            
        case NMI_SOURCE_UNKNOWN:
        default:
            nmi_handle_unknown(ctx);
            break;
    }
    
    /* Reset handler state */
    atomic_set(&nmi.handler_state, NMI_STATE_NORMAL);
    
    return IRQ_HANDLED;
}

/*
 * Initialize NMI handling
 */
int nmi_init(void)
{
    debuglog_printf("NMI: Initializing Non-Maskable Interrupt handling\n");
    
    /* Clear statistics */
    atomic64_set(&nmi.total_nmis, 0);
    atomic64_set(&nmi.memory_parity_errors, 0);
    atomic64_set(&nmi.channel_check_errors, 0);
    atomic64_set(&nmi.watchdog_timeouts, 0);
    atomic64_set(&nmi.pcie_errors, 0);
    atomic64_set(&nmi.thermal_events, 0);
    atomic64_set(&nmi.voltage_events, 0);
    atomic64_set(&nmi.software_nmis, 0);
    atomic64_set(&nmi.unknown_nmis, 0);
    atomic64_set(&nmi.recursive_nmis, 0);
    
    nmi.last_nmi_time = 0;
    nmi.last_nmi_rip = 0;
    nmi.last_nmi_source = NMI_SOURCE_UNKNOWN;
    nmi.consecutive_nmis = 0;
    atomic_set(&nmi.handler_state, NMI_STATE_NORMAL);
    
    /* Register NMI handler (vector 2) */
    idt_register_handler(EXCEPTION_NMI, 
                        (interrupt_handler_t)nmi_handler_c, 
                        "NMI Handler");
    
    /* Enable NMI */
    nmi_enable();
    
    debuglog_printf("NMI: Initialization complete\n");
    return 0;
}

/*
 * Enable NMI
 */
void nmi_enable(void)
{
    uint8_t value;
    
    /* Clear NMI disable bit in CMOS index register */
    value = inb(NMI_STATUS_CONTROL_B);
    value &= ~NMI_DISABLE_BIT;
    outb(NMI_STATUS_CONTROL_B, value);
    
    nmi.enabled = true;
    debuglog_printf("NMI: Enabled\n");
}

/*
 * Disable NMI
 */
void nmi_disable(void)
{
    uint8_t value;
    
    /* Set NMI disable bit in CMOS index register */
    value = inb(NMI_STATUS_CONTROL_B);
    value |= NMI_DISABLE_BIT;
    outb(NMI_STATUS_CONTROL_B, value);
    
    nmi.enabled = false;
    atomic_set(&nmi.handler_state, NMI_STATE_DISABLED);
    debuglog_printf("NMI: Disabled\n");
}

/*
 * Generate software NMI (for testing)
 */
void nmi_trigger_software(void)
{
    debuglog_printf("NMI: Triggering software NMI\n");
    
    /* This would trigger an NMI through hardware-specific means */
    /* Implementation depends on the specific hardware platform */
    
    /* For debugging, we could call the handler directly */
    /* struct interrupt_context dummy_ctx = {0};
       nmi_handler(&dummy_ctx); */
}

/*
 * Configure NMI behavior
 */
void nmi_set_panic_on_error(bool panic_on_nmi)
{
    nmi.panic_on_nmi = panic_on_nmi;
    debuglog_printf("NMI: Panic on NMI set to %s\n", panic_on_nmi ? "enabled" : "disabled");
}

/*
 * Configure NMI logging
 */
void nmi_set_log_all(bool log_all)
{
    nmi.log_all_nmis = log_all;
    debuglog_printf("NMI: Log all NMIs set to %s\n", log_all ? "enabled" : "disabled");
}

/*
 * Get NMI statistics
 */
void nmi_get_stats(struct nmi_stats *stats)
{
    if (!stats) {
        return;
    }
    
    stats->enabled = nmi.enabled;
    stats->handler_state = atomic_read(&nmi.handler_state);
    stats->total_nmis = atomic64_read(&nmi.total_nmis);
    stats->memory_parity_errors = atomic64_read(&nmi.memory_parity_errors);
    stats->channel_check_errors = atomic64_read(&nmi.channel_check_errors);
    stats->watchdog_timeouts = atomic64_read(&nmi.watchdog_timeouts);
    stats->pcie_errors = atomic64_read(&nmi.pcie_errors);
    stats->thermal_events = atomic64_read(&nmi.thermal_events);
    stats->voltage_events = atomic64_read(&nmi.voltage_events);
    stats->software_nmis = atomic64_read(&nmi.software_nmis);
    stats->unknown_nmis = atomic64_read(&nmi.unknown_nmis);
    stats->recursive_nmis = atomic64_read(&nmi.recursive_nmis);
    stats->last_nmi_time = nmi.last_nmi_time;
    stats->last_nmi_rip = nmi.last_nmi_rip;
    stats->last_nmi_source = nmi.last_nmi_source;
    stats->consecutive_nmis = nmi.consecutive_nmis;
}

/*
 * Check if NMI is available
 */
bool nmi_is_available(void)
{
    return nmi.enabled && (atomic_read(&nmi.handler_state) != NMI_STATE_DISABLED);
}
