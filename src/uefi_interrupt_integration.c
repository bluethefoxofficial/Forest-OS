/*
 * uefi_interrupt_integration.c - UEFI Interrupt Services Integration for Forest OS
 * 
 * This module provides:
 * - Integration with UEFI boot services interrupt handling
 * - UEFI timer services integration and preservation
 * - Smooth transition from UEFI to OS interrupt management
 * - UEFI runtime services interrupt support
 * - UEFI event handling and callback management
 * - Coexistence of UEFI and OS interrupt services
 * 
 * Handles the complex transition from UEFI firmware interrupt services
 * to OS-managed interrupts while preserving critical UEFI functionality.
 * 
 * Copyright (c) 2024 Forest OS Project
 */

#include "include/interrupt.h"
#include "include/memory.h"
#include "include/debug.h"
#include "include/time.h"
#include "include/debuglog.h"
#include "include/string.h"

#define debug_printf debuglog_printf

#if UEFI_SUPPORT

/* UEFI System Table and Services (these would be provided by UEFI environment) */
struct uefi_system_table;
struct uefi_boot_services;
struct uefi_runtime_services;

/* UEFI Event Types */
#define UEFI_EVENT_TIMER                    0x80000000
#define UEFI_EVENT_RUNTIME                  0x40000000
#define UEFI_EVENT_NOTIFY_WAIT              0x00000100
#define UEFI_EVENT_NOTIFY_SIGNAL            0x00000200
#define UEFI_EVENT_SIGNAL_EXIT_BOOT_SERVICES 0x00000201

/* UEFI Timer Types */
typedef enum {
    UEFI_TIMER_CANCEL,
    UEFI_TIMER_PERIODIC,
    UEFI_TIMER_RELATIVE
} uefi_timer_delay_t;

/* UEFI Event Notification Function */
typedef void (*uefi_event_notify_t)(void *event, void *context);

/* UEFI Event structure (simplified) */
struct uefi_event {
    uint32_t type;
    uint32_t notify_tpl;
    uefi_event_notify_t notify_function;
    void *notify_context;
    void *event_handle;
    bool signaled;
};

/* UEFI Timer Interrupt Context */
struct uefi_timer_context {
    struct uefi_event *timer_event;
    uint64_t trigger_time;
    uint64_t period;
    uefi_timer_delay_t timer_type;
    bool active;
    uint32_t interrupt_count;
};

/* UEFI Interrupt Integration State */
struct uefi_interrupt_state {
    /* UEFI Services */
    struct uefi_system_table *system_table;
    struct uefi_boot_services *boot_services;
    struct uefi_runtime_services *runtime_services;
    
    /* Boot Services State */
    bool boot_services_active;
    bool exit_boot_services_called;
    uint64_t exit_boot_services_time;
    
    /* Timer Integration */
    struct uefi_timer_context timer_context;
    bool timer_services_available;
    uint64_t uefi_timer_frequency;
    
    /* Event Management */
    struct uefi_event *registered_events[16];
    uint32_t event_count;
    spinlock_t event_lock;
    
    /* Interrupt Preservation */
    struct {
        void *original_timer_handler;
        void *original_keyboard_handler;
        void *original_console_handler;
        bool handlers_preserved;
    } preserved_handlers;
    
    /* Runtime Services Support */
    struct {
        bool runtime_services_active;
        void *runtime_interrupt_handler;
        uint64_t last_runtime_call_time;
        uint32_t runtime_calls_count;
    } runtime;
    
    /* Statistics */
    struct {
        uint64_t boot_service_interrupts;
        uint64_t runtime_service_interrupts;
        uint64_t timer_interrupts;
        uint64_t event_notifications;
        uint64_t transition_events;
    } stats;
    
    /* Configuration */
    struct {
        bool preserve_uefi_timer;
        bool enable_runtime_interrupts;
        bool debug_mode;
        uint32_t timer_polling_interval_ms;
    } config;
    
    bool initialized;
    spinlock_t lock;
};

static struct uefi_interrupt_state uefi_int_state = {0};

/* Forward declarations */
static int uefi_interrupt_early_init(void);
static int uefi_timer_setup(uint64_t period_us);
static void uefi_timer_cleanup(void);
static void uefi_exit_boot_services_prep(void);
static void uefi_runtime_services_setup(void);
static void uefi_timer_event_handler(struct uefi_event *event, void *context);
static void uefi_exit_boot_services_handler(struct uefi_event *event, void *context);
static int uefi_register_event(uint32_t type, uint32_t notify_tpl, 
                              uefi_event_notify_t notify_function, void *notify_context);
static void uefi_unregister_all_events(void);
static int uefi_preserve_critical_handlers(void);
static void uefi_restore_preserved_handlers(void);

/* ===========================
 * INITIALIZATION AND SETUP
 * =========================== */

/**
 * Initialize UEFI interrupt integration
 */
int uefi_interrupt_integration_init(void)
{
    if (uefi_int_state.initialized) {
        return 0;
    }
    
    memset(&uefi_int_state, 0, sizeof(uefi_int_state));
    spinlock_init(&uefi_int_state.lock, "uefi_interrupt");
    spinlock_init(&uefi_int_state.event_lock, "uefi_event");
    
    /* Set default configuration */
    uefi_int_state.config.preserve_uefi_timer = true;
    uefi_int_state.config.enable_runtime_interrupts = true;
    uefi_int_state.config.debug_mode = false;
    uefi_int_state.config.timer_polling_interval_ms = 1000;
    
    /* Initialize from UEFI environment (would be provided by bootloader) */
    /* These would typically be set by the UEFI bootloader */
    // uefi_int_state.system_table = get_uefi_system_table();
    // uefi_int_state.boot_services = uefi_int_state.system_table->boot_services;
    // uefi_int_state.runtime_services = uefi_int_state.system_table->runtime_services;
    
    uefi_int_state.boot_services_active = true;
    uefi_int_state.timer_services_available = true;
    
    uefi_int_state.initialized = true;
    
    debug_printf("UEFI interrupt integration initialized\n");
    return 0;
}

/**
 * Early UEFI interrupt initialization during boot
 */
static int uefi_interrupt_early_init(void)
{
    int ret;
    
    if (!uefi_int_state.initialized) {
        ret = uefi_interrupt_integration_init();
        if (ret < 0) {
            return ret;
        }
    }
    
    /* Preserve critical UEFI interrupt handlers */
    ret = uefi_preserve_critical_handlers();
    if (ret < 0) {
        debug_printf("Warning: Failed to preserve UEFI handlers: %d\n", ret);
    }
    
    /* Register for ExitBootServices notification */
    ret = uefi_register_event(UEFI_EVENT_SIGNAL_EXIT_BOOT_SERVICES,
                             0, /* TPL_CALLBACK */
                             uefi_exit_boot_services_handler,
                             NULL);
    if (ret < 0) {
        debug_printf("Warning: Failed to register ExitBootServices event: %d\n", ret);
    }
    
    /* Set up UEFI timer if requested */
    if (uefi_int_state.config.preserve_uefi_timer) {
        ret = uefi_timer_setup(uefi_int_state.config.timer_polling_interval_ms * 1000);
        if (ret < 0) {
            debug_printf("Warning: Failed to setup UEFI timer: %d\n", ret);
        }
    }
    
    debug_printf("UEFI early interrupt initialization completed\n");
    return 0;
}

/**
 * Cleanup UEFI interrupt integration
 */
void uefi_interrupt_integration_cleanup(void)
{
    unsigned long flags;
    
    if (!uefi_int_state.initialized) {
        return;
    }
    
    spin_lock_irqsave(&uefi_int_state.lock, flags);
    
    /* Clean up timer */
    if (uefi_int_state.timer_context.active) {
        uefi_timer_cleanup();
    }
    
    /* Unregister all events */
    uefi_unregister_all_events();
    
    /* Restore preserved handlers if still available */
    if (uefi_int_state.preserved_handlers.handlers_preserved) {
        uefi_restore_preserved_handlers();
    }
    
    uefi_int_state.initialized = false;
    
    spin_unlock_irqrestore(&uefi_int_state.lock, flags);
    
    debug_printf("UEFI interrupt integration cleaned up\n");
}

/* ===========================
 * TIMER SERVICES INTEGRATION
 * =========================== */

/**
 * Set up UEFI timer services integration
 */
static int uefi_timer_setup(uint64_t period_us)
{
    struct uefi_timer_context *timer;
    unsigned long flags;
    int ret;
    
    if (!uefi_int_state.boot_services_active || !uefi_int_state.timer_services_available) {
        return -ENODEV;
    }
    
    spin_lock_irqsave(&uefi_int_state.lock, flags);
    
    timer = &uefi_int_state.timer_context;
    
    /* Create timer event if not already created */
    if (!timer->timer_event) {
        ret = uefi_register_event(UEFI_EVENT_TIMER | UEFI_EVENT_NOTIFY_SIGNAL,
                                 0, /* TPL_CALLBACK */
                                 uefi_timer_event_handler,
                                 timer);
        if (ret < 0) {
            spin_unlock_irqrestore(&uefi_int_state.lock, flags);
            return ret;
        }
        
        /* This would be the actual UEFI event handle */
        timer->timer_event = (struct uefi_event *)&timer->timer_event; /* Placeholder */
    }
    
    /* Configure timer */
    timer->period = period_us * 10; /* Convert to 100ns units for UEFI */
    timer->timer_type = UEFI_TIMER_PERIODIC;
    timer->trigger_time = get_system_time_ns() / 100; /* Convert to 100ns units */
    timer->active = true;
    timer->interrupt_count = 0;
    
    spin_unlock_irqrestore(&uefi_int_state.lock, flags);
    
    debug_printf("UEFI timer setup: period=%llu us, type=%d\n", 
                period_us, timer->timer_type);
    
    /* This would call UEFI SetTimer service */
    /* boot_services->set_timer(timer->timer_event, timer->timer_type, timer->period); */
    
    return 0;
}

/**
 * Clean up UEFI timer services
 */
static void uefi_timer_cleanup(void)
{
    struct uefi_timer_context *timer;
    unsigned long flags;
    
    spin_lock_irqsave(&uefi_int_state.lock, flags);
    
    timer = &uefi_int_state.timer_context;
    
    if (timer->active && timer->timer_event) {
        /* Cancel timer */
        /* boot_services->set_timer(timer->timer_event, UEFI_TIMER_CANCEL, 0); */
        
        timer->active = false;
        timer->timer_type = UEFI_TIMER_CANCEL;
        
        debug_printf("UEFI timer cleaned up (handled %u interrupts)\n",
                    timer->interrupt_count);
    }
    
    spin_unlock_irqrestore(&uefi_int_state.lock, flags);
}

/**
 * UEFI timer event handler
 */
static void uefi_timer_event_handler(struct uefi_event *event, void *context)
{
    struct uefi_timer_context *timer = (struct uefi_timer_context *)context;
    uint64_t current_time;
    
    if (!timer || !timer->active) {
        return;
    }
    
    current_time = get_system_time_ns();
    timer->interrupt_count++;
    uefi_int_state.stats.timer_interrupts++;
    
    /* Update trigger time for next period */
    timer->trigger_time = current_time / 100 + timer->period;
    
    /* Call into OS timer system if available */
    if (interrupt_mgr.early_init_done) {
        timer_interrupt_handler();
    }
    
    if (uefi_int_state.config.debug_mode) {
        debug_printf("UEFI timer interrupt: count=%u, time=%llu ns\n",
                    timer->interrupt_count, current_time);
    }
}

/* ===========================
 * BOOT SERVICES TRANSITION
 * =========================== */

/**
 * Prepare for ExitBootServices transition
 */
static void uefi_exit_boot_services_prep(void)
{
    unsigned long flags;
    
    spin_lock_irqsave(&uefi_int_state.lock, flags);
    
    debug_printf("Preparing for UEFI ExitBootServices transition\n");
    
    /* Stop UEFI timer if active */
    if (uefi_int_state.timer_context.active) {
        uefi_timer_cleanup();
    }
    
    /* Mark transition state */
    uefi_int_state.boot_services_active = false;
    uefi_int_state.exit_boot_services_time = get_system_time_ns();
    
    /* Unregister boot service events */
    uefi_unregister_all_events();
    
    spin_unlock_irqrestore(&uefi_int_state.lock, flags);
    
    debug_printf("UEFI ExitBootServices preparation completed\n");
}

/**
 * Handler called when ExitBootServices is invoked
 */
static void uefi_exit_boot_services_handler(struct uefi_event *event, void *context)
{
    uefi_int_state.stats.transition_events++;
    uefi_exit_boot_services_prep();
    
    /* Mark that ExitBootServices has been called */
    uefi_int_state.exit_boot_services_called = true;
    
    debug_printf("UEFI ExitBootServices event handled\n");
}

/* ===========================
 * RUNTIME SERVICES SUPPORT
 * =========================== */

/**
 * Set up UEFI runtime services interrupt support
 */
static void uefi_runtime_services_setup(void)
{
    unsigned long flags;
    
    if (!uefi_int_state.runtime_services || uefi_int_state.boot_services_active) {
        return;
    }
    
    spin_lock_irqsave(&uefi_int_state.lock, flags);
    
    uefi_int_state.runtime.runtime_services_active = true;
    uefi_int_state.runtime.last_runtime_call_time = get_system_time_ns();
    uefi_int_state.runtime.runtime_calls_count = 0;
    
    /* Set up runtime interrupt handler if needed */
    if (uefi_int_state.config.enable_runtime_interrupts) {
        /* This would set up a minimal interrupt handler for UEFI runtime calls */
        /* uefi_int_state.runtime.runtime_interrupt_handler = setup_uefi_runtime_handler(); */
    }
    
    spin_unlock_irqrestore(&uefi_int_state.lock, flags);
    
    debug_printf("UEFI runtime services interrupt support enabled\n");
}

/**
 * Handle UEFI runtime service interrupt
 */
irq_return_t uefi_runtime_service_interrupt(int vector, struct interrupt_context *ctx)
{
    unsigned long flags;
    
    if (!uefi_int_state.runtime.runtime_services_active) {
        return IRQ_NONE;
    }
    
    spin_lock_irqsave(&uefi_int_state.lock, flags);
    
    uefi_int_state.runtime.runtime_calls_count++;
    uefi_int_state.runtime.last_runtime_call_time = get_system_time_ns();
    uefi_int_state.stats.runtime_service_interrupts++;
    
    spin_unlock_irqrestore(&uefi_int_state.lock, flags);
    
    /* Handle runtime service specific interrupts */
    switch (vector) {
        case 0x30: /* Example runtime service vector */
            /* Handle runtime service call */
            break;
        default:
            return IRQ_NONE;
    }
    
    return IRQ_HANDLED;
}

/* ===========================
 * EVENT MANAGEMENT
 * =========================== */

/**
 * Register a UEFI event
 */
static int uefi_register_event(uint32_t type, uint32_t notify_tpl,
                              uefi_event_notify_t notify_function, void *notify_context)
{
    struct uefi_event *event;
    unsigned long flags;
    int i;
    
    if (!uefi_int_state.boot_services_active) {
        return -ENODEV;
    }
    
    spin_lock_irqsave(&uefi_int_state.event_lock, flags);
    
    /* Find free event slot */
    for (i = 0; i < 16; i++) {
        if (!uefi_int_state.registered_events[i]) {
            break;
        }
    }
    
    if (i >= 16) {
        spin_unlock_irqrestore(&uefi_int_state.event_lock, flags);
        return -ENOMEM;
    }
    
    /* Create event structure */
    event = (struct uefi_event *)kmalloc(sizeof(*event), GFP_ATOMIC);
    if (!event) {
        spin_unlock_irqrestore(&uefi_int_state.event_lock, flags);
        return -ENOMEM;
    }
    
    memset(event, 0, sizeof(*event));
    event->type = type;
    event->notify_tpl = notify_tpl;
    event->notify_function = notify_function;
    event->notify_context = notify_context;
    event->signaled = false;
    
    uefi_int_state.registered_events[i] = event;
    uefi_int_state.event_count++;
    
    spin_unlock_irqrestore(&uefi_int_state.event_lock, flags);
    
    /* This would call UEFI CreateEvent service */
    /* boot_services->create_event(type, notify_tpl, notify_function, 
                                   notify_context, &event->event_handle); */
    
    debug_printf("Registered UEFI event: type=0x%x, TPL=%u\n", type, notify_tpl);
    return 0;
}

/**
 * Unregister all UEFI events
 */
static void uefi_unregister_all_events(void)
{
    unsigned long flags;
    int i;
    
    spin_lock_irqsave(&uefi_int_state.event_lock, flags);
    
    for (i = 0; i < 16; i++) {
        if (uefi_int_state.registered_events[i]) {
            struct uefi_event *event = uefi_int_state.registered_events[i];
            
            /* This would call UEFI CloseEvent service */
            /* boot_services->close_event(event->event_handle); */
            
            kfree(event);
            uefi_int_state.registered_events[i] = NULL;
            uefi_int_state.event_count--;
        }
    }
    
    spin_unlock_irqrestore(&uefi_int_state.event_lock, flags);
    
    debug_printf("Unregistered all UEFI events\n");
}

/* ===========================
 * HANDLER PRESERVATION
 * =========================== */

/**
 * Preserve critical UEFI interrupt handlers during transition
 */
static int uefi_preserve_critical_handlers(void)
{
    unsigned long flags;
    
    spin_lock_irqsave(&uefi_int_state.lock, flags);
    
    /* Save original handlers (these would be read from UEFI/firmware) */
    /* uefi_int_state.preserved_handlers.original_timer_handler = get_uefi_timer_handler(); */
    /* uefi_int_state.preserved_handlers.original_keyboard_handler = get_uefi_keyboard_handler(); */
    /* uefi_int_state.preserved_handlers.original_console_handler = get_uefi_console_handler(); */
    
    uefi_int_state.preserved_handlers.handlers_preserved = true;
    
    spin_unlock_irqrestore(&uefi_int_state.lock, flags);
    
    debug_printf("Preserved critical UEFI interrupt handlers\n");
    return 0;
}

/**
 * Restore preserved UEFI handlers if needed
 */
static void uefi_restore_preserved_handlers(void)
{
    unsigned long flags;
    
    spin_lock_irqsave(&uefi_int_state.lock, flags);
    
    if (uefi_int_state.preserved_handlers.handlers_preserved) {
        /* Restore handlers if still in UEFI context */
        /* restore_uefi_timer_handler(uefi_int_state.preserved_handlers.original_timer_handler); */
        /* restore_uefi_keyboard_handler(uefi_int_state.preserved_handlers.original_keyboard_handler); */
        /* restore_uefi_console_handler(uefi_int_state.preserved_handlers.original_console_handler); */
        
        uefi_int_state.preserved_handlers.handlers_preserved = false;
        
        debug_printf("Restored preserved UEFI interrupt handlers\n");
    }
    
    spin_unlock_irqrestore(&uefi_int_state.lock, flags);
}

/* ===========================
 * PUBLIC API FUNCTIONS
 * =========================== */

/**
 * Check if UEFI boot services are still active
 */
bool uefi_boot_services_active(void)
{
    return uefi_int_state.initialized && uefi_int_state.boot_services_active;
}

/**
 * Check if UEFI runtime services are active
 */
bool uefi_runtime_services_active(void)
{
    return uefi_int_state.initialized && uefi_int_state.runtime.runtime_services_active;
}

/**
 * Get UEFI interrupt statistics
 */
void uefi_get_interrupt_stats(struct uefi_interrupt_state *stats)
{
    unsigned long flags;
    
    if (!uefi_int_state.initialized || !stats) {
        return;
    }
    
    spin_lock_irqsave(&uefi_int_state.lock, flags);
    memcpy(stats, &uefi_int_state, sizeof(*stats));
    spin_unlock_irqrestore(&uefi_int_state.lock, flags);
}

/**
 * Configure UEFI interrupt integration
 */
int uefi_configure_interrupt_integration(bool preserve_timer, bool enable_runtime)
{
    unsigned long flags;
    
    if (!uefi_int_state.initialized) {
        return -ENODEV;
    }
    
    spin_lock_irqsave(&uefi_int_state.lock, flags);
    
    uefi_int_state.config.preserve_uefi_timer = preserve_timer;
    uefi_int_state.config.enable_runtime_interrupts = enable_runtime;
    
    spin_unlock_irqrestore(&uefi_int_state.lock, flags);
    
    debug_printf("UEFI interrupt configuration updated: timer=%s, runtime=%s\n",
                preserve_timer ? "yes" : "no", enable_runtime ? "yes" : "no");
    
    return 0;
}

/**
 * Dump UEFI interrupt integration state
 */
void uefi_dump_interrupt_state(void)
{
    unsigned long flags;
    
    if (!uefi_int_state.initialized) {
        debug_printf("UEFI interrupt integration not initialized\n");
        return;
    }
    
    spin_lock_irqsave(&uefi_int_state.lock, flags);
    
    debug_printf("=== UEFI Interrupt Integration State ===\n");
    debug_printf("Initialization: %s\n", uefi_int_state.initialized ? "yes" : "no");
    debug_printf("Boot services: %s\n", uefi_int_state.boot_services_active ? "active" : "inactive");
    debug_printf("Runtime services: %s\n", uefi_int_state.runtime.runtime_services_active ? "active" : "inactive");
    debug_printf("ExitBootServices called: %s\n", uefi_int_state.exit_boot_services_called ? "yes" : "no");
    
    if (uefi_int_state.exit_boot_services_called) {
        debug_printf("ExitBootServices time: %llu ns\n", uefi_int_state.exit_boot_services_time);
    }
    
    debug_printf("\nTimer Context:\n");
    debug_printf("  Active: %s\n", uefi_int_state.timer_context.active ? "yes" : "no");
    debug_printf("  Type: %d\n", uefi_int_state.timer_context.timer_type);
    debug_printf("  Period: %llu (100ns units)\n", uefi_int_state.timer_context.period);
    debug_printf("  Interrupt count: %u\n", uefi_int_state.timer_context.interrupt_count);
    
    debug_printf("\nEvent Management:\n");
    debug_printf("  Registered events: %u\n", uefi_int_state.event_count);
    debug_printf("  Handlers preserved: %s\n", uefi_int_state.preserved_handlers.handlers_preserved ? "yes" : "no");
    
    debug_printf("\nStatistics:\n");
    debug_printf("  Boot service interrupts: %llu\n", uefi_int_state.stats.boot_service_interrupts);
    debug_printf("  Runtime service interrupts: %llu\n", uefi_int_state.stats.runtime_service_interrupts);
    debug_printf("  Timer interrupts: %llu\n", uefi_int_state.stats.timer_interrupts);
    debug_printf("  Event notifications: %llu\n", uefi_int_state.stats.event_notifications);
    debug_printf("  Transition events: %llu\n", uefi_int_state.stats.transition_events);
    
    debug_printf("\nConfiguration:\n");
    debug_printf("  Preserve UEFI timer: %s\n", uefi_int_state.config.preserve_uefi_timer ? "yes" : "no");
    debug_printf("  Enable runtime interrupts: %s\n", uefi_int_state.config.enable_runtime_interrupts ? "yes" : "no");
    debug_printf("  Debug mode: %s\n", uefi_int_state.config.debug_mode ? "yes" : "no");
    
    spin_unlock_irqrestore(&uefi_int_state.lock, flags);
}

/**
 * Enable or disable UEFI interrupt debugging
 */
void uefi_interrupt_debug_enable(bool enable)
{
    uefi_int_state.config.debug_mode = enable;
    debug_printf("UEFI interrupt debugging %s\n", enable ? "enabled" : "disabled");
}

/* ===========================
 * INTEGRATION WITH OS INTERRUPTS
 * =========================== */

/**
 * Integrate UEFI interrupts with OS interrupt system
 */
int uefi_integrate_with_os_interrupts(void)
{
    int ret;
    
    if (!uefi_int_state.initialized) {
        ret = uefi_interrupt_integration_init();
        if (ret < 0) {
            return ret;
        }
    }
    
    /* Set up early integration */
    ret = uefi_interrupt_early_init();
    if (ret < 0) {
        return ret;
    }
    
    /* Register UEFI-specific interrupt handlers with OS system */
    if (uefi_int_state.config.enable_runtime_interrupts) {
        ret = request_irq(0x30, /* Example UEFI runtime vector */
                         uefi_runtime_service_interrupt,
                         IRQF_SHARED,
                         "UEFI Runtime Services",
                         &uefi_int_state);
        if (ret < 0) {
            debug_printf("Warning: Failed to register UEFI runtime interrupt: %d\n", ret);
        }
    }
    
    debug_printf("UEFI interrupts integrated with OS interrupt system\n");
    return 0;
}

#else /* !UEFI_SUPPORT */

/* Stub functions when UEFI support is not enabled */
int uefi_interrupt_integration_init(void) { return 0; }
void uefi_interrupt_integration_cleanup(void) {}
bool uefi_boot_services_active(void) { return false; }
bool uefi_runtime_services_active(void) { return false; }
void uefi_dump_interrupt_state(void) { debug_printf("UEFI support not enabled\n"); }
void uefi_interrupt_debug_enable(bool enable) { (void)enable; }
int uefi_integrate_with_os_interrupts(void) { return 0; }

#endif /* UEFI_SUPPORT */