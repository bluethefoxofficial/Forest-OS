/*
 * interrupt_registration.c - Interrupt Handler Registration Framework for Forest OS
 * 
 * This module provides:
 * - High-level API for interrupt handler registration
 * - Handler lifecycle management and validation
 * - Support for threaded and fast interrupt handlers
 * - Shared interrupt handling with proper arbitration
 * - Handler chaining and priority management
 * - Automatic cleanup and resource management
 * - Statistics and debugging support
 * 
 * Supports both x86-32 and x86-64 architectures with full integration
 * of priority, nesting, and IRQ allocation systems.
 * 
 * Copyright (c) 2024 Forest OS Project
 */

#include "interrupt.h"
#include "memory.h"
#include "mm.h"
#include "smp.h"
#include "debug.h"
#include "time.h"
#include "thread.h"
#include <string.h>
#include <stdio.h>

/* Forward declarations for irq_management.c functions */
extern int irq_alloc_specific(int irq, const char *name, irq_handler_t handler, void *data, uint32_t flags);
extern int irq_free(int irq, const char *name);
extern int irq_enable(int irq);
extern int irq_disable(int irq);

/* Handler types and states */
typedef enum {
    HANDLER_TYPE_FAST,      /* Fast ISR, minimal processing */
    HANDLER_TYPE_THREADED,  /* Threaded handler for complex work */
    HANDLER_TYPE_SHARED,    /* Shared handler with arbitration */
    HANDLER_TYPE_ONESHOT,   /* One-shot handler, auto-unregister */
    HANDLER_TYPE_COALESCED  /* Coalesced handler for high-frequency */
} handler_type_t;

typedef enum {
    HANDLER_STATE_INACTIVE,
    HANDLER_STATE_ACTIVE,
    HANDLER_STATE_SUSPENDED,
    HANDLER_STATE_ERROR,
    HANDLER_STATE_REMOVING
} handler_state_t;

/* Extended handler registration structure */
struct interrupt_registration {
    /* Basic handler information */
    int vector;
    irq_handler_t fast_handler;     /* Fast ISR handler */
    irq_handler_t thread_handler;   /* Threaded handler */
    void *dev_id;
    const char *name;
    const char *driver_name;
    
    /* Handler configuration */
    handler_type_t type;
    handler_state_t state;
    uint32_t flags;
    uint8_t priority;
    uint64_t deadline_ns;
    
    /* Threading support */
    struct thread *handler_thread;
    struct semaphore thread_sem;
    volatile bool thread_should_exit;
    
    /* Statistics and monitoring */
    struct {
        uint64_t total_interrupts;
        uint64_t handled_interrupts;
        uint64_t unhandled_interrupts;
        uint64_t error_count;
        uint64_t max_latency_ns;
        uint64_t min_latency_ns;
        uint64_t avg_latency_ns;
        uint64_t total_time_ns;
        uint64_t last_interrupt_time;
        uint32_t consecutive_errors;
        uint32_t deadlines_missed;
    } stats;
    
    /* Lifecycle management */
    uint64_t registration_time;
    atomic_t reference_count;
    struct completion removal_completion;
    
    /* Chain management for shared handlers */
    struct interrupt_registration *next;
    struct interrupt_registration *prev;
    spinlock_t chain_lock;
    
    /* Runtime state */
    bool enabled;
    bool removal_pending;
    unsigned long last_error_flags;
};

/* Registration management */
static struct interrupt_registration *registrations[256] = {NULL};
static spinlock_t registration_lock;
static bool registration_framework_initialized = false;

/* Framework statistics */
struct registration_framework_stats {
    uint64_t total_registrations;
    uint64_t total_unregistrations;
    uint64_t registration_failures;
    uint64_t handler_errors;
    uint64_t threaded_handlers;
    uint64_t shared_handlers;
    uint32_t active_registrations;
    uint32_t peak_registrations;
};

static struct registration_framework_stats framework_stats = {0};

/* Debug support */
static bool registration_debug_enabled = false;

/* Forward declarations */
static struct interrupt_registration *create_registration(
    int vector, irq_handler_t fast_handler, irq_handler_t thread_handler,
    const char *name, void *dev_id, unsigned long flags);
static void destroy_registration(struct interrupt_registration *reg);
static irq_return_t registration_dispatcher(int irq, void *dev_id, struct interrupt_context *ctx);
static void *threaded_handler_main(void *arg);
static int validate_registration_params(int vector, irq_handler_t handler, const char *name);
static void update_handler_stats(struct interrupt_registration *reg, uint64_t start_time, irq_return_t result);
static void chain_add_handler(struct interrupt_registration *chain, struct interrupt_registration *new_handler);
static void chain_remove_handler(struct interrupt_registration *handler);

/* ===========================
 * FRAMEWORK INITIALIZATION
 * =========================== */

/**
 * Initialize the interrupt handler registration framework
 */
int interrupt_registration_init(void)
{
    int i;
    
    if (registration_framework_initialized) {
        return 0;
    }
    
    /* Initialize registration tracking */
    for (i = 0; i < 256; i++) {
        registrations[i] = NULL;
    }
    
    spinlock_init(&registration_lock, "interrupt_registration");
    memset(&framework_stats, 0, sizeof(framework_stats));
    
    registration_framework_initialized = true;
    debug_printf("Interrupt handler registration framework initialized\n");
    
    return 0;
}

/**
 * Cleanup the registration framework
 */
void interrupt_registration_cleanup(void)
{
    int i;
    unsigned long flags;
    struct interrupt_registration *reg, *next;
    
    if (!registration_framework_initialized) {
        return;
    }
    
    spin_lock_irqsave(&registration_lock, flags);
    
    /* Unregister all remaining handlers */
    for (i = 0; i < 256; i++) {
        reg = registrations[i];
        while (reg) {
            next = reg->next;
            reg->removal_pending = true;
            /* Signal threaded handlers to exit */
            if (reg->handler_thread) {
                reg->thread_should_exit = true;
                semaphore_up(&reg->thread_sem);
            }
            reg = next;
        }
    }
    
    registration_framework_initialized = false;
    spin_unlock_irqrestore(&registration_lock, flags);
    
    debug_printf("Interrupt registration framework cleaned up\n");
}

/* ===========================
 * HANDLER REGISTRATION
 * =========================== */

/**
 * Register a fast interrupt handler
 */
int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
                const char *name, void *dev_id)
{
    return request_threaded_irq_extended(irq, handler, NULL, flags, name, dev_id, NULL);
}

/**
 * Register a threaded interrupt handler
 */
int request_threaded_irq_extended(unsigned int irq, irq_handler_t handler,
                                 irq_handler_t thread_fn, unsigned long flags,
                                 const char *name, void *dev_id, const char *driver_name)
{
    struct interrupt_registration *reg;
    unsigned long irq_flags;
    int ret = 0;
    bool new_allocation = false;
    
    if (!registration_framework_initialized) {
        return -ENODEV;
    }
    
    /* Validate parameters */
    ret = validate_registration_params(irq, handler, name);
    if (ret < 0) {
        framework_stats.registration_failures++;
        return ret;
    }
    
    /* Create registration structure */
    reg = create_registration(irq, handler, thread_fn, name, dev_id, flags);
    if (!reg) {
        framework_stats.registration_failures++;
        return -ENOMEM;
    }
    
    reg->driver_name = driver_name;
    
    spin_lock_irqsave(&registration_lock, irq_flags);
    
    /* Check if this is a new IRQ allocation */
    if (!registrations[irq]) {
        /* Allocate the IRQ */
        ret = irq_alloc_specific(irq, name, registration_dispatcher, reg, flags);
        if (ret < 0) {
            spin_unlock_irqrestore(&registration_lock, irq_flags);
            destroy_registration(reg);
            framework_stats.registration_failures++;
            return ret;
        }
        new_allocation = true;
        
        /* This is the primary handler */
        registrations[irq] = reg;
        reg->type = thread_fn ? HANDLER_TYPE_THREADED : HANDLER_TYPE_FAST;
        
    } else {
        /* Check if sharing is allowed */
        if (!(flags & IRQF_SHARED) || !registrations[irq]) {
            spin_unlock_irqrestore(&registration_lock, irq_flags);
            destroy_registration(reg);
            framework_stats.registration_failures++;
            return -EBUSY;
        }
        
        /* Add to shared handler chain */
        reg->type = HANDLER_TYPE_SHARED;
        chain_add_handler(registrations[irq], reg);
        framework_stats.shared_handlers++;
    }
    
    /* Set priority */
    if (priority_mgr.initialized) {
        uint32_t priority_flags = PRIORITY_FLAG_PREEMPTIBLE;
        uint8_t priority = INTERRUPT_PRIORITY_NORMAL;
        
        if (flags & IRQF_DISABLED) {
            priority_flags |= PRIORITY_FLAG_CRITICAL;
            priority = INTERRUPT_PRIORITY_HIGH;
        }
        
        interrupt_set_priority(irq, priority, priority_flags);
        reg->priority = priority;
    }
    
    /* Start threaded handler if needed */
    if (thread_fn) {
        char thread_name[64];
        snprintf(thread_name, sizeof(thread_name), "irq/%d-%s", irq, name);
        
        reg->handler_thread = thread_create(thread_name, threaded_handler_main, reg);
        if (!reg->handler_thread) {
            /* Clean up on thread creation failure */
            if (new_allocation) {
                irq_free(irq, name);
                registrations[irq] = NULL;
            } else {
                chain_remove_handler(reg);
            }
            spin_unlock_irqrestore(&registration_lock, irq_flags);
            destroy_registration(reg);
            framework_stats.registration_failures++;
            return -ENOMEM;
        }
        
        thread_start(reg->handler_thread);
        framework_stats.threaded_handlers++;
    }
    
    /* Enable the IRQ if not disabled by default */
    if (!(flags & IRQF_DISABLED)) {
        irq_enable(irq);
        reg->enabled = true;
    }
    
    reg->state = HANDLER_STATE_ACTIVE;
    
    /* Update statistics */
    framework_stats.total_registrations++;
    framework_stats.active_registrations++;
    if (framework_stats.active_registrations > framework_stats.peak_registrations) {
        framework_stats.peak_registrations = framework_stats.active_registrations;
    }
    
    spin_unlock_irqrestore(&registration_lock, irq_flags);
    
    if (registration_debug_enabled) {
        debug_printf("Registered %s handler for IRQ %d: %s (%s)\n",
                    thread_fn ? "threaded" : "fast", irq, name,
                    new_allocation ? "new" : "shared");
    }
    
    return 0;
}

/**
 * Unregister an interrupt handler
 */
void free_irq_extended(unsigned int irq, void *dev_id)
{
    struct interrupt_registration *reg, *next;
    unsigned long flags;
    bool found = false;
    bool was_primary = false;
    
    if (!registration_framework_initialized || irq >= 256) {
        return;
    }
    
    spin_lock_irqsave(&registration_lock, flags);
    
    /* Find the handler to remove */
    reg = registrations[irq];
    while (reg) {
        next = reg->next;
        
        if (reg->dev_id == dev_id) {
            found = true;
            
            /* Mark for removal */
            reg->state = HANDLER_STATE_REMOVING;
            reg->removal_pending = true;
            
            /* Check if this is the primary handler */
            if (reg == registrations[irq]) {
                was_primary = true;
            }
            
            /* Remove from chain */
            if (was_primary && reg->next) {
                /* Promote next handler to primary */
                registrations[irq] = reg->next;
                reg->next->prev = NULL;
            } else if (was_primary) {
                /* This was the only handler */
                registrations[irq] = NULL;
            } else {
                chain_remove_handler(reg);
            }
            
            /* Stop threaded handler */
            if (reg->handler_thread) {
                reg->thread_should_exit = true;
                semaphore_up(&reg->thread_sem);
                
                /* Wait for thread to complete */
                spin_unlock_irqrestore(&registration_lock, flags);
                thread_join(reg->handler_thread, NULL);
                spin_lock_irqsave(&registration_lock, flags);
                
                thread_destroy(reg->handler_thread);
                reg->handler_thread = NULL;
            }
            
            break;
        }
        reg = next;
    }
    
    if (!found) {
        spin_unlock_irqrestore(&registration_lock, flags);
        return;
    }
    
    /* If this was the last handler, free the IRQ */
    if (was_primary && !registrations[irq]) {
        irq_disable(irq);
        irq_free(irq, reg->name);
    }
    
    /* Update statistics */
    framework_stats.total_unregistrations++;
    if (framework_stats.active_registrations > 0) {
        framework_stats.active_registrations--;
    }
    
    spin_unlock_irqrestore(&registration_lock, flags);
    
    /* Clean up the registration */
    destroy_registration(reg);
    
    if (registration_debug_enabled) {
        debug_printf("Unregistered handler for IRQ %d (dev_id=%p)\n", irq, dev_id);
    }
}

/* ===========================
 * HANDLER DISPATCHING
 * =========================== */

/**
 * Main interrupt dispatcher for registered handlers
 */
static irq_return_t registration_dispatcher(int irq, void *dev_id, struct interrupt_context *ctx)
{
    struct interrupt_registration *reg = (struct interrupt_registration *)dev_id;
    struct interrupt_registration *current;
    irq_return_t ret = IRQ_NONE;
    irq_return_t handler_ret;
    uint64_t start_time, latency;
    bool handled = false;
    
    if (!reg || irq >= 256) {
        return IRQ_NONE;
    }
    
    start_time = get_system_time_ns();
    
    /* Handle primary registration and chain */
    current = registrations[irq];
    while (current) {
        if (current->state != HANDLER_STATE_ACTIVE) {
            current = current->next;
            continue;
        }
        
        atomic_inc(&current->reference_count);
        current->stats.total_interrupts++;
        
        /* Call fast handler */
        if (current->fast_handler) {
            handler_ret = current->fast_handler(irq, current->dev_id, ctx);
            
            /* Update statistics */
            update_handler_stats(current, start_time, handler_ret);
            
            switch (handler_ret) {
                case IRQ_HANDLED:
                    current->stats.handled_interrupts++;
                    handled = true;
                    ret = IRQ_HANDLED;
                    break;
                    
                case IRQ_WAKE_THREAD:
                    if (current->handler_thread) {
                        semaphore_up(&current->thread_sem);
                        handled = true;
                        ret = IRQ_HANDLED;
                    }
                    current->stats.handled_interrupts++;
                    break;
                    
                case IRQ_NONE:
                    current->stats.unhandled_interrupts++;
                    break;
                    
                default:
                    current->stats.error_count++;
                    current->stats.consecutive_errors++;
                    framework_stats.handler_errors++;
                    break;
            }
        } else if (current->thread_handler) {
            /* Pure threaded handler */
            semaphore_up(&current->thread_sem);
            handled = true;
            ret = IRQ_HANDLED;
            current->stats.handled_interrupts++;
        }
        
        atomic_dec(&current->reference_count);
        
        /* For shared handlers, continue to next */
        if (current->type == HANDLER_TYPE_SHARED) {
            current = current->next;
        } else {
            break;
        }
    }
    
    /* Calculate latency */
    latency = get_system_time_ns() - start_time;
    
    /* Update global statistics */
    if (!handled) {
        atomic64_inc(&interrupt_mgr.total_interrupts);
    }
    
    return ret;
}

/**
 * Threaded handler main function
 */
static void *threaded_handler_main(void *arg)
{
    struct interrupt_registration *reg = (struct interrupt_registration *)arg;
    irq_return_t ret;
    uint64_t start_time;
    
    if (!reg || !reg->thread_handler) {
        return NULL;
    }
    
    debug_printf("Started threaded handler for IRQ %d: %s\n", reg->vector, reg->name);
    
    while (!reg->thread_should_exit) {
        /* Wait for interrupt signal */
        if (semaphore_down_timeout(&reg->thread_sem, 1000) != 0) {
            /* Timeout - check for exit condition */
            continue;
        }
        
        if (reg->thread_should_exit) {
            break;
        }
        
        /* Process the interrupt */
        start_time = get_system_time_ns();
        
        atomic_inc(&reg->reference_count);
        
        ret = reg->thread_handler(reg->vector, reg->dev_id, NULL);
        
        update_handler_stats(reg, start_time, ret);
        
        atomic_dec(&reg->reference_count);
    }
    
    debug_printf("Threaded handler exiting for IRQ %d: %s\n", reg->vector, reg->name);
    return NULL;
}

/* ===========================
 * HANDLER MANAGEMENT
 * =========================== */

/**
 * Enable a registered interrupt handler
 */
int interrupt_handler_enable(int irq, void *dev_id)
{
    struct interrupt_registration *reg;
    unsigned long flags;
    
    if (!registration_framework_initialized || irq >= 256) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&registration_lock, flags);
    
    reg = registrations[irq];
    while (reg) {
        if (reg->dev_id == dev_id) {
            if (reg->state == HANDLER_STATE_ACTIVE && !reg->enabled) {
                reg->enabled = true;
                reg->state = HANDLER_STATE_ACTIVE;
                irq_enable(irq);
                
                spin_unlock_irqrestore(&registration_lock, flags);
                return 0;
            }
            break;
        }
        reg = reg->next;
    }
    
    spin_unlock_irqrestore(&registration_lock, flags);
    return -ENOENT;
}

/**
 * Disable a registered interrupt handler
 */
int interrupt_handler_disable(int irq, void *dev_id)
{
    struct interrupt_registration *reg;
    unsigned long flags;
    
    if (!registration_framework_initialized || irq >= 256) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&registration_lock, flags);
    
    reg = registrations[irq];
    while (reg) {
        if (reg->dev_id == dev_id) {
            if (reg->state == HANDLER_STATE_ACTIVE && reg->enabled) {
                reg->enabled = false;
                reg->state = HANDLER_STATE_SUSPENDED;
                
                /* Only disable IRQ if no other handlers are active */
                bool others_enabled = false;
                struct interrupt_registration *check = registrations[irq];
                while (check) {
                    if (check != reg && check->enabled) {
                        others_enabled = true;
                        break;
                    }
                    check = check->next;
                }
                
                if (!others_enabled) {
                    irq_disable(irq);
                }
                
                spin_unlock_irqrestore(&registration_lock, flags);
                return 0;
            }
            break;
        }
        reg = reg->next;
    }
    
    spin_unlock_irqrestore(&registration_lock, flags);
    return -ENOENT;
}

/* ===========================
 * STATISTICS AND DEBUGGING
 * =========================== */

/**
 * Get registration framework statistics
 */
void interrupt_registration_get_stats(struct registration_framework_stats *stats)
{
    unsigned long flags;
    
    if (!registration_framework_initialized || !stats) {
        return;
    }
    
    spin_lock_irqsave(&registration_lock, flags);
    memcpy(stats, &framework_stats, sizeof(*stats));
    spin_unlock_irqrestore(&registration_lock, flags);
}

/**
 * Get handler statistics for a specific IRQ
 */
int interrupt_get_handler_stats(int irq, void *dev_id, struct interrupt_registration *stats)
{
    struct interrupt_registration *reg;
    unsigned long flags;
    
    if (!registration_framework_initialized || irq >= 256 || !stats) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&registration_lock, flags);
    
    reg = registrations[irq];
    while (reg) {
        if (reg->dev_id == dev_id) {
            memcpy(stats, reg, sizeof(*stats));
            spin_unlock_irqrestore(&registration_lock, flags);
            return 0;
        }
        reg = reg->next;
    }
    
    spin_unlock_irqrestore(&registration_lock, flags);
    return -ENOENT;
}

/**
 * Dump all registered handlers
 */
void interrupt_dump_registrations(void)
{
    int i;
    struct interrupt_registration *reg;
    unsigned long flags;
    
    if (!registration_framework_initialized) {
        debug_printf("Registration framework not initialized\n");
        return;
    }
    
    spin_lock_irqsave(&registration_lock, flags);
    
    debug_printf("=== Interrupt Handler Registrations ===\n");
    debug_printf("Framework Stats:\n");
    debug_printf("  Total registrations: %llu\n", framework_stats.total_registrations);
    debug_printf("  Total unregistrations: %llu\n", framework_stats.total_unregistrations);
    debug_printf("  Registration failures: %llu\n", framework_stats.registration_failures);
    debug_printf("  Handler errors: %llu\n", framework_stats.handler_errors);
    debug_printf("  Active registrations: %u\n", framework_stats.active_registrations);
    debug_printf("  Peak registrations: %u\n", framework_stats.peak_registrations);
    debug_printf("  Threaded handlers: %llu\n", framework_stats.threaded_handlers);
    debug_printf("  Shared handlers: %llu\n", framework_stats.shared_handlers);
    
    debug_printf("\nActive Handlers:\n");
    for (i = 0; i < 256; i++) {
        reg = registrations[i];
        if (reg) {
            debug_printf("IRQ %3d:\n", i);
            
            while (reg) {
                debug_printf("  %s: %s (%s)\n",
                            reg->name,
                            reg->state == HANDLER_STATE_ACTIVE ? "active" :
                            reg->state == HANDLER_STATE_SUSPENDED ? "suspended" :
                            reg->state == HANDLER_STATE_ERROR ? "error" : "unknown",
                            reg->type == HANDLER_TYPE_FAST ? "fast" :
                            reg->type == HANDLER_TYPE_THREADED ? "threaded" :
                            reg->type == HANDLER_TYPE_SHARED ? "shared" : "unknown");
                
                debug_printf("    Interrupts: %llu handled, %llu unhandled\n",
                            reg->stats.handled_interrupts, reg->stats.unhandled_interrupts);
                
                if (reg->stats.handled_interrupts > 0) {
                    debug_printf("    Latency: avg=%llu ns, min=%llu ns, max=%llu ns\n",
                                reg->stats.avg_latency_ns, reg->stats.min_latency_ns, reg->stats.max_latency_ns);
                }
                
                reg = reg->next;
            }
        }
    }
    
    spin_unlock_irqrestore(&registration_lock, flags);
}

/**
 * Enable or disable registration debugging
 */
void interrupt_registration_debug_enable(bool enable)
{
    registration_debug_enabled = enable;
    debug_printf("Registration debugging %s\n", enable ? "enabled" : "disabled");
}

/* ===========================
 * HELPER FUNCTIONS
 * =========================== */

/**
 * Create a new interrupt registration
 */
static struct interrupt_registration *create_registration(
    int vector, irq_handler_t fast_handler, irq_handler_t thread_handler,
    const char *name, void *dev_id, unsigned long flags)
{
    struct interrupt_registration *reg;
    
    reg = (struct interrupt_registration *)kmalloc(sizeof(*reg), GFP_KERNEL);
    if (!reg) {
        return NULL;
    }
    
    memset(reg, 0, sizeof(*reg));
    
    reg->vector = vector;
    reg->fast_handler = fast_handler;
    reg->thread_handler = thread_handler;
    reg->dev_id = dev_id;
    reg->name = name;
    reg->flags = flags;
    reg->state = HANDLER_STATE_INACTIVE;
    reg->registration_time = get_system_time_ns();
    
    atomic_set(&reg->reference_count, 0);
    spinlock_init(&reg->chain_lock, "handler_chain");
    semaphore_init(&reg->thread_sem, 0);
    init_completion(&reg->removal_completion);
    
    /* Initialize statistics with reasonable defaults */
    reg->stats.min_latency_ns = UINT64_MAX;
    
    return reg;
}

/**
 * Destroy an interrupt registration
 */
static void destroy_registration(struct interrupt_registration *reg)
{
    if (!reg) {
        return;
    }
    
    /* Wait for any ongoing references to complete */
    while (atomic_read(&reg->reference_count) > 0) {
        cpu_relax();
    }
    
    kfree(reg);
}

/**
 * Validate registration parameters
 */
static int validate_registration_params(int vector, irq_handler_t handler, const char *name)
{
    if (vector < 0 || vector >= 256) {
        return -EINVAL;
    }
    
    if (!handler) {
        return -EINVAL;
    }
    
    if (!name || strlen(name) == 0) {
        return -EINVAL;
    }
    
    return 0;
}

/**
 * Update handler statistics
 */
static void update_handler_stats(struct interrupt_registration *reg, uint64_t start_time, irq_return_t result)
{
    uint64_t end_time = get_system_time_ns();
    uint64_t latency = end_time - start_time;
    
    reg->stats.total_time_ns += latency;
    reg->stats.last_interrupt_time = end_time;
    
    if (latency < reg->stats.min_latency_ns) {
        reg->stats.min_latency_ns = latency;
    }
    
    if (latency > reg->stats.max_latency_ns) {
        reg->stats.max_latency_ns = latency;
    }
    
    /* Calculate rolling average */
    if (reg->stats.handled_interrupts > 0) {
        reg->stats.avg_latency_ns = reg->stats.total_time_ns / reg->stats.handled_interrupts;
    }
    
    /* Check deadline if set */
    if (reg->deadline_ns > 0 && latency > reg->deadline_ns) {
        reg->stats.deadlines_missed++;
    }
    
    /* Reset consecutive error count on success */
    if (result == IRQ_HANDLED || result == IRQ_WAKE_THREAD) {
        reg->stats.consecutive_errors = 0;
    }
}

/**
 * Add handler to shared chain
 */
static void chain_add_handler(struct interrupt_registration *chain, struct interrupt_registration *new_handler)
{
    struct interrupt_registration *current;
    
    if (!chain || !new_handler) {
        return;
    }
    
    /* Find end of chain */
    current = chain;
    while (current->next) {
        current = current->next;
    }
    
    /* Add to end */
    current->next = new_handler;
    new_handler->prev = current;
    new_handler->next = NULL;
}

/**
 * Remove handler from shared chain
 */
static void chain_remove_handler(struct interrupt_registration *handler)
{
    if (!handler) {
        return;
    }
    
    if (handler->prev) {
        handler->prev->next = handler->next;
    }
    
    if (handler->next) {
        handler->next->prev = handler->prev;
    }
    
    handler->prev = NULL;
    handler->next = NULL;
}