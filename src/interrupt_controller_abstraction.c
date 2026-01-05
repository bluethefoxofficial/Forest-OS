/*
 * interrupt_controller_abstraction.c - Multi-Architecture Interrupt Controller Abstraction for Forest OS
 * 
 * This module provides:
 * - Unified interface for interrupt controllers across architectures
 * - Dynamic controller detection and selection
 * - Architecture-specific implementations (x86, ARM, RISC-V)
 * - Controller capability management and reporting
 * - Initialization and configuration abstractions
 * - Performance optimization and caching
 * 
 * Supported Controllers:
 * - x86: 8259A PIC, Local APIC, I/O APIC, MSI/MSI-X
 * - ARM: GICv2, GICv3, GICv4, Legacy VIC
 * - RISC-V: PLIC, CLINT
 * - Generic: Platform-specific controllers
 * 
 * Copyright (c) 2024 Forest OS Project
 */

#include "interrupt.h"
#include "memory.h"
#include "smp.h"
#include "debug.h"
#include "time.h"
#include <string.h>

/* Controller architecture types */
typedef enum {
    ARCH_X86_32,
    ARCH_X86_64,
    ARCH_ARM_V7,
    ARCH_ARM_V8,
    ARCH_RISCV_32,
    ARCH_RISCV_64,
    ARCH_GENERIC
} controller_arch_t;

/* Controller types and capabilities */
typedef enum {
    CONTROLLER_TYPE_LEGACY_PIC,     /* 8259A PIC */
    CONTROLLER_TYPE_LOCAL_APIC,     /* x86 Local APIC */
    CONTROLLER_TYPE_IOAPIC,         /* x86 I/O APIC */
    CONTROLLER_TYPE_MSI,            /* Message Signaled Interrupts */
    CONTROLLER_TYPE_GIC_V2,         /* ARM GICv2 */
    CONTROLLER_TYPE_GIC_V3,         /* ARM GICv3 */
    CONTROLLER_TYPE_GIC_V4,         /* ARM GICv4 */
    CONTROLLER_TYPE_VIC,            /* ARM Legacy VIC */
    CONTROLLER_TYPE_PLIC,           /* RISC-V PLIC */
    CONTROLLER_TYPE_CLINT,          /* RISC-V CLINT */
    CONTROLLER_TYPE_GENERIC         /* Platform-specific */
} controller_type_t;

/* Controller capability flags */
#define CONTROLLER_CAP_BASIC           (1U << 0)   /* Basic enable/disable */
#define CONTROLLER_CAP_PRIORITY        (1U << 1)   /* Priority management */
#define CONTROLLER_CAP_AFFINITY        (1U << 2)   /* CPU affinity control */
#define CONTROLLER_CAP_TRIGGER_MODE    (1U << 3)   /* Edge/Level trigger */
#define CONTROLLER_CAP_POLARITY        (1U << 4)   /* Active high/low */
#define CONTROLLER_CAP_MSI             (1U << 5)   /* MSI support */
#define CONTROLLER_CAP_VIRTUALIZATION  (1U << 6)   /* Virtualization support */
#define CONTROLLER_CAP_POWER_MGMT      (1U << 7)   /* Power management */
#define CONTROLLER_CAP_SECURITY        (1U << 8)   /* Security features */
#define CONTROLLER_CAP_PERFORMANCE     (1U << 9)   /* Performance monitoring */

/* Controller state */
typedef enum {
    CONTROLLER_STATE_UNINITIALIZED,
    CONTROLLER_STATE_INITIALIZING,
    CONTROLLER_STATE_ACTIVE,
    CONTROLLER_STATE_SUSPENDED,
    CONTROLLER_STATE_ERROR,
    CONTROLLER_STATE_DISABLED
} controller_state_t;

/* Forward declaration of controller operations */
struct interrupt_controller;

/* Controller operation function pointers */
struct controller_operations {
    /* Core operations */
    int (*init)(struct interrupt_controller *ctrl);
    void (*cleanup)(struct interrupt_controller *ctrl);
    int (*enable_irq)(struct interrupt_controller *ctrl, int irq);
    int (*disable_irq)(struct interrupt_controller *ctrl, int irq);
    int (*acknowledge)(struct interrupt_controller *ctrl, int irq);
    int (*end_of_interrupt)(struct interrupt_controller *ctrl, int irq);
    
    /* Configuration */
    int (*set_priority)(struct interrupt_controller *ctrl, int irq, int priority);
    int (*get_priority)(struct interrupt_controller *ctrl, int irq);
    int (*set_affinity)(struct interrupt_controller *ctrl, int irq, uint32_t cpu_mask);
    int (*get_affinity)(struct interrupt_controller *ctrl, int irq, uint32_t *cpu_mask);
    int (*set_trigger_mode)(struct interrupt_controller *ctrl, int irq, bool edge_triggered);
    int (*set_polarity)(struct interrupt_controller *ctrl, int irq, bool active_high);
    
    /* Advanced features */
    int (*mask_irq)(struct interrupt_controller *ctrl, int irq);
    int (*unmask_irq)(struct interrupt_controller *ctrl, int irq);
    bool (*is_irq_pending)(struct interrupt_controller *ctrl, int irq);
    int (*clear_pending)(struct interrupt_controller *ctrl, int irq);
    int (*send_ipi)(struct interrupt_controller *ctrl, int cpu, int vector);
    
    /* MSI support */
    int (*allocate_msi)(struct interrupt_controller *ctrl, int *vector, uint32_t count);
    int (*free_msi)(struct interrupt_controller *ctrl, int vector, uint32_t count);
    int (*configure_msi)(struct interrupt_controller *ctrl, int vector, uint64_t address, uint32_t data);
    
    /* Power management */
    int (*suspend)(struct interrupt_controller *ctrl);
    int (*resume)(struct interrupt_controller *ctrl);
    int (*set_power_state)(struct interrupt_controller *ctrl, int state);
    
    /* Diagnostics */
    void (*dump_state)(struct interrupt_controller *ctrl);
    int (*self_test)(struct interrupt_controller *ctrl);
    void (*get_stats)(struct interrupt_controller *ctrl, void *stats);
};

/* Interrupt controller descriptor */
struct interrupt_controller {
    /* Identification */
    const char *name;
    const char *description;
    controller_type_t type;
    controller_arch_t architecture;
    uint32_t version;
    
    /* Capabilities and configuration */
    uint32_t capabilities;
    uint32_t max_irqs;
    uint32_t max_cpus;
    uint32_t priority_levels;
    uint32_t vector_base;
    
    /* Hardware information */
    uint64_t base_address;
    uint32_t mmio_size;
    int irq_base;
    bool memory_mapped;
    bool requires_eoi;
    
    /* Operations and state */
    const struct controller_operations *ops;
    controller_state_t state;
    void *private_data;
    spinlock_t lock;
    
    /* Statistics */
    struct {
        uint64_t irqs_handled;
        uint64_t irqs_enabled;
        uint64_t irqs_disabled;
        uint64_t eois_sent;
        uint64_t ipis_sent;
        uint64_t errors;
        uint64_t init_time_ns;
    } stats;
    
    /* Linked list for multiple controllers */
    struct interrupt_controller *next;
};

/* Controller registry and management */
struct controller_registry {
    struct interrupt_controller *controllers;
    struct interrupt_controller *primary_controller;
    struct interrupt_controller *secondary_controller;
    spinlock_t lock;
    
    /* Architecture detection */
    controller_arch_t detected_architecture;
    uint32_t num_controllers;
    uint32_t active_controllers;
    
    /* Global capabilities */
    uint32_t system_capabilities;
    uint32_t max_system_irqs;
    bool initialized;
};

static struct controller_registry registry = {0};

/* Architecture-specific controller definitions */
#ifdef ARCH_64BIT
static const controller_arch_t current_arch = ARCH_X86_64;
#else
static const controller_arch_t current_arch = ARCH_X86_32;
#endif

/* Forward declarations */
static int detect_available_controllers(void);
static struct interrupt_controller *create_controller(controller_type_t type);
static int initialize_controller(struct interrupt_controller *ctrl);
static void destroy_controller(struct interrupt_controller *ctrl);
static struct interrupt_controller *select_primary_controller(void);
static int register_controller(struct interrupt_controller *ctrl);
static void unregister_controller(struct interrupt_controller *ctrl);

/* x86-specific controller operations */
static const struct controller_operations pic_8259a_ops;
static const struct controller_operations local_apic_ops;
static const struct controller_operations ioapic_ops;
static const struct controller_operations msi_ops;

/* ===========================
 * INITIALIZATION
 * =========================== */

/**
 * Initialize the interrupt controller abstraction system
 */
int interrupt_controller_abstraction_init(void)
{
    int ret;
    
    if (registry.initialized) {
        return 0;
    }
    
    /* Initialize registry */
    memset(&registry, 0, sizeof(registry));
    spinlock_init(&registry.lock, "interrupt_controller_registry");
    registry.detected_architecture = current_arch;
    
    debug_printf("Initializing interrupt controller abstraction for %s\n",
                current_arch == ARCH_X86_64 ? "x86-64" : 
                current_arch == ARCH_X86_32 ? "x86-32" : "unknown");
    
    /* Detect available controllers */
    ret = detect_available_controllers();
    if (ret < 0) {
        debug_printf("Failed to detect interrupt controllers\n");
        return ret;
    }
    
    /* Select primary controller */
    registry.primary_controller = select_primary_controller();
    if (!registry.primary_controller) {
        debug_printf("No suitable primary interrupt controller found\n");
        return -ENODEV;
    }
    
    /* Initialize primary controller */
    ret = initialize_controller(registry.primary_controller);
    if (ret < 0) {
        debug_printf("Failed to initialize primary controller: %s\n",
                    registry.primary_controller->name);
        return ret;
    }
    
    /* Initialize secondary controllers if needed */
    struct interrupt_controller *ctrl = registry.controllers;
    while (ctrl) {
        if (ctrl != registry.primary_controller && 
            ctrl->state == CONTROLLER_STATE_UNINITIALIZED) {
            ret = initialize_controller(ctrl);
            if (ret == 0) {
                registry.active_controllers++;
            }
        }
        ctrl = ctrl->next;
    }
    
    registry.initialized = true;
    
    debug_printf("Interrupt controller abstraction initialized\n");
    debug_printf("Primary controller: %s (%u IRQs, capabilities: 0x%x)\n",
                registry.primary_controller->name,
                registry.primary_controller->max_irqs,
                registry.primary_controller->capabilities);
    debug_printf("Active controllers: %u/%u\n", 
                registry.active_controllers, registry.num_controllers);
    
    return 0;
}

/**
 * Cleanup interrupt controller abstraction
 */
void interrupt_controller_abstraction_cleanup(void)
{
    struct interrupt_controller *ctrl, *next;
    unsigned long flags;
    
    if (!registry.initialized) {
        return;
    }
    
    spin_lock_irqsave(&registry.lock, flags);
    
    ctrl = registry.controllers;
    while (ctrl) {
        next = ctrl->next;
        destroy_controller(ctrl);
        ctrl = next;
    }
    
    memset(&registry, 0, sizeof(registry));
    spin_unlock_irqrestore(&registry.lock, flags);
    
    debug_printf("Interrupt controller abstraction cleaned up\n");
}

/* ===========================
 * CONTROLLER DETECTION
 * =========================== */

/**
 * Detect available interrupt controllers on the current platform
 */
static int detect_available_controllers(void)
{
    struct interrupt_controller *ctrl;
    int detected = 0;
    
    switch (current_arch) {
        case ARCH_X86_32:
        case ARCH_X86_64:
            /* Detect x86 controllers */
            
            /* Always have legacy PIC as fallback */
            ctrl = create_controller(CONTROLLER_TYPE_LEGACY_PIC);
            if (ctrl) {
                register_controller(ctrl);
                detected++;
            }
            
            /* Check for Local APIC */
            if (apic_is_available()) {
                ctrl = create_controller(CONTROLLER_TYPE_LOCAL_APIC);
                if (ctrl) {
                    register_controller(ctrl);
                    detected++;
                }
            }
            
            /* Check for I/O APIC */
            if (ioapic_is_available()) {
                ctrl = create_controller(CONTROLLER_TYPE_IOAPIC);
                if (ctrl) {
                    register_controller(ctrl);
                    detected++;
                }
            }
            
            /* MSI support */
            ctrl = create_controller(CONTROLLER_TYPE_MSI);
            if (ctrl) {
                register_controller(ctrl);
                detected++;
            }
            
            break;
            
        case ARCH_ARM_V7:
        case ARCH_ARM_V8:
            /* ARM controller detection would go here */
            debug_printf("ARM interrupt controller detection not implemented\n");
            break;
            
        case ARCH_RISCV_32:
        case ARCH_RISCV_64:
            /* RISC-V controller detection would go here */
            debug_printf("RISC-V interrupt controller detection not implemented\n");
            break;
            
        default:
            debug_printf("Unknown architecture for controller detection\n");
            return -ENODEV;
    }
    
    if (detected == 0) {
        debug_printf("No interrupt controllers detected\n");
        return -ENODEV;
    }
    
    debug_printf("Detected %d interrupt controllers\n", detected);
    return detected;
}

/**
 * Select the primary interrupt controller based on capabilities and priority
 */
static struct interrupt_controller *select_primary_controller(void)
{
    struct interrupt_controller *ctrl, *best = NULL;
    int best_score = -1;
    
    /* Score controllers based on capabilities */
    ctrl = registry.controllers;
    while (ctrl) {
        int score = 0;
        
        /* Prefer controllers with more capabilities */
        if (ctrl->capabilities & CONTROLLER_CAP_PRIORITY) score += 10;
        if (ctrl->capabilities & CONTROLLER_CAP_AFFINITY) score += 8;
        if (ctrl->capabilities & CONTROLLER_CAP_MSI) score += 6;
        if (ctrl->capabilities & CONTROLLER_CAP_TRIGGER_MODE) score += 4;
        if (ctrl->capabilities & CONTROLLER_CAP_VIRTUALIZATION) score += 3;
        
        /* Architecture-specific preferences */
        switch (ctrl->type) {
            case CONTROLLER_TYPE_LOCAL_APIC:
                score += 50;  /* Preferred on x86 */
                break;
            case CONTROLLER_TYPE_IOAPIC:
                score += 40;  /* Good for SMP */
                break;
            case CONTROLLER_TYPE_LEGACY_PIC:
                score += 10;  /* Fallback only */
                break;
            case CONTROLLER_TYPE_MSI:
                score += 30;  /* Good for modern systems */
                break;
            default:
                break;
        }
        
        /* Prefer controllers with more IRQs */
        if (ctrl->max_irqs > 64) score += 5;
        
        if (score > best_score) {
            best_score = score;
            best = ctrl;
        }
        
        ctrl = ctrl->next;
    }
    
    if (best) {
        debug_printf("Selected primary controller: %s (score: %d)\n", 
                    best->name, best_score);
    }
    
    return best;
}

/* ===========================
 * CONTROLLER MANAGEMENT
 * =========================== */

/**
 * Create a controller instance for a specific type
 */
static struct interrupt_controller *create_controller(controller_type_t type)
{
    struct interrupt_controller *ctrl;
    
    ctrl = (struct interrupt_controller *)kmalloc(sizeof(*ctrl), GFP_KERNEL);
    if (!ctrl) {
        return NULL;
    }
    
    memset(ctrl, 0, sizeof(*ctrl));
    spinlock_init(&ctrl->lock, "interrupt_controller");
    ctrl->type = type;
    ctrl->architecture = current_arch;
    ctrl->state = CONTROLLER_STATE_UNINITIALIZED;
    
    /* Configure type-specific properties */
    switch (type) {
        case CONTROLLER_TYPE_LEGACY_PIC:
            ctrl->name = "8259A PIC";
            ctrl->description = "Legacy 8259A Programmable Interrupt Controller";
            ctrl->capabilities = CONTROLLER_CAP_BASIC;
            ctrl->max_irqs = 16;
            ctrl->max_cpus = 1;
            ctrl->priority_levels = 8;
            ctrl->ops = &pic_8259a_ops;
            ctrl->requires_eoi = true;
            break;
            
        case CONTROLLER_TYPE_LOCAL_APIC:
            ctrl->name = "Local APIC";
            ctrl->description = "x86 Local Advanced Programmable Interrupt Controller";
            ctrl->capabilities = CONTROLLER_CAP_BASIC | CONTROLLER_CAP_PRIORITY | 
                               CONTROLLER_CAP_AFFINITY | CONTROLLER_CAP_PERFORMANCE;
            ctrl->max_irqs = 256;
            ctrl->max_cpus = 255;
            ctrl->priority_levels = 16;
            ctrl->ops = &local_apic_ops;
            ctrl->memory_mapped = true;
            ctrl->requires_eoi = true;
            break;
            
        case CONTROLLER_TYPE_IOAPIC:
            ctrl->name = "I/O APIC";
            ctrl->description = "x86 I/O Advanced Programmable Interrupt Controller";
            ctrl->capabilities = CONTROLLER_CAP_BASIC | CONTROLLER_CAP_PRIORITY | 
                               CONTROLLER_CAP_AFFINITY | CONTROLLER_CAP_TRIGGER_MODE | 
                               CONTROLLER_CAP_POLARITY;
            ctrl->max_irqs = 24;  /* Typical I/O APIC */
            ctrl->max_cpus = 255;
            ctrl->priority_levels = 16;
            ctrl->ops = &ioapic_ops;
            ctrl->memory_mapped = true;
            break;
            
        case CONTROLLER_TYPE_MSI:
            ctrl->name = "MSI Controller";
            ctrl->description = "Message Signaled Interrupt Controller";
            ctrl->capabilities = CONTROLLER_CAP_BASIC | CONTROLLER_CAP_MSI | 
                               CONTROLLER_CAP_AFFINITY | CONTROLLER_CAP_PERFORMANCE;
            ctrl->max_irqs = 256;
            ctrl->max_cpus = 255;
            ctrl->ops = &msi_ops;
            break;
            
        default:
            kfree(ctrl);
            return NULL;
    }
    
    return ctrl;
}

/**
 * Initialize a specific controller
 */
static int initialize_controller(struct interrupt_controller *ctrl)
{
    unsigned long flags;
    uint64_t start_time;
    int ret;
    
    if (!ctrl || !ctrl->ops || !ctrl->ops->init) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&ctrl->lock, flags);
    
    if (ctrl->state != CONTROLLER_STATE_UNINITIALIZED) {
        spin_unlock_irqrestore(&ctrl->lock, flags);
        return -EBUSY;
    }
    
    ctrl->state = CONTROLLER_STATE_INITIALIZING;
    start_time = get_system_time_ns();
    
    spin_unlock_irqrestore(&ctrl->lock, flags);
    
    /* Call controller-specific initialization */
    ret = ctrl->ops->init(ctrl);
    
    spin_lock_irqsave(&ctrl->lock, flags);
    
    if (ret == 0) {
        ctrl->state = CONTROLLER_STATE_ACTIVE;
        ctrl->stats.init_time_ns = get_system_time_ns() - start_time;
        registry.active_controllers++;
    } else {
        ctrl->state = CONTROLLER_STATE_ERROR;
    }
    
    spin_unlock_irqrestore(&ctrl->lock, flags);
    
    if (ret == 0) {
        debug_printf("Initialized controller: %s (init time: %llu ns)\n",
                    ctrl->name, ctrl->stats.init_time_ns);
    } else {
        debug_printf("Failed to initialize controller: %s (error: %d)\n",
                    ctrl->name, ret);
    }
    
    return ret;
}

/**
 * Register a controller with the system
 */
static int register_controller(struct interrupt_controller *ctrl)
{
    unsigned long flags;
    
    if (!ctrl) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&registry.lock, flags);
    
    /* Add to linked list */
    ctrl->next = registry.controllers;
    registry.controllers = ctrl;
    registry.num_controllers++;
    
    /* Update system capabilities */
    registry.system_capabilities |= ctrl->capabilities;
    if (ctrl->max_irqs > registry.max_system_irqs) {
        registry.max_system_irqs = ctrl->max_irqs;
    }
    
    spin_unlock_irqrestore(&registry.lock, flags);
    
    debug_printf("Registered controller: %s\n", ctrl->name);
    return 0;
}

/* ===========================
 * PUBLIC API FUNCTIONS
 * =========================== */

/**
 * Enable an interrupt through the appropriate controller
 */
int interrupt_controller_enable_irq(int irq)
{
    struct interrupt_controller *ctrl;
    int ret = -ENODEV;
    
    if (!registry.initialized) {
        return -ENODEV;
    }
    
    /* Use primary controller for most interrupts */
    ctrl = registry.primary_controller;
    if (ctrl && ctrl->ops && ctrl->ops->enable_irq) {
        ret = ctrl->ops->enable_irq(ctrl, irq);
        if (ret == 0) {
            ctrl->stats.irqs_enabled++;
        }
    }
    
    /* Try secondary controller if primary fails */
    if (ret != 0 && registry.secondary_controller) {
        ctrl = registry.secondary_controller;
        if (ctrl && ctrl->ops && ctrl->ops->enable_irq) {
            ret = ctrl->ops->enable_irq(ctrl, irq);
            if (ret == 0) {
                ctrl->stats.irqs_enabled++;
            }
        }
    }
    
    return ret;
}

/**
 * Disable an interrupt through the appropriate controller
 */
int interrupt_controller_disable_irq(int irq)
{
    struct interrupt_controller *ctrl;
    int ret = -ENODEV;
    
    if (!registry.initialized) {
        return -ENODEV;
    }
    
    ctrl = registry.primary_controller;
    if (ctrl && ctrl->ops && ctrl->ops->disable_irq) {
        ret = ctrl->ops->disable_irq(ctrl, irq);
        if (ret == 0) {
            ctrl->stats.irqs_disabled++;
        }
    }
    
    return ret;
}

/**
 * Send End-of-Interrupt signal through the appropriate controller
 */
int interrupt_controller_eoi(int irq)
{
    struct interrupt_controller *ctrl;
    int ret = -ENODEV;
    
    if (!registry.initialized) {
        return -ENODEV;
    }
    
    ctrl = registry.primary_controller;
    if (ctrl && ctrl->ops && ctrl->ops->end_of_interrupt) {
        ret = ctrl->ops->end_of_interrupt(ctrl, irq);
        if (ret == 0) {
            ctrl->stats.eois_sent++;
        }
    }
    
    return ret;
}

/**
 * Set interrupt priority through the appropriate controller
 */
int interrupt_controller_set_priority(int irq, int priority)
{
    struct interrupt_controller *ctrl;
    
    if (!registry.initialized) {
        return -ENODEV;
    }
    
    ctrl = registry.primary_controller;
    if (ctrl && ctrl->ops && ctrl->ops->set_priority &&
        (ctrl->capabilities & CONTROLLER_CAP_PRIORITY)) {
        return ctrl->ops->set_priority(ctrl, irq, priority);
    }
    
    return -ENOTSUP;
}

/**
 * Set interrupt CPU affinity through the appropriate controller
 */
int interrupt_controller_set_affinity(int irq, uint32_t cpu_mask)
{
    struct interrupt_controller *ctrl;
    
    if (!registry.initialized) {
        return -ENODEV;
    }
    
    ctrl = registry.primary_controller;
    if (ctrl && ctrl->ops && ctrl->ops->set_affinity &&
        (ctrl->capabilities & CONTROLLER_CAP_AFFINITY)) {
        return ctrl->ops->set_affinity(ctrl, irq, cpu_mask);
    }
    
    return -ENOTSUP;
}

/**
 * Send Inter-Processor Interrupt
 */
int interrupt_controller_send_ipi(int cpu, int vector)
{
    struct interrupt_controller *ctrl;
    int ret = -ENODEV;
    
    if (!registry.initialized) {
        return -ENODEV;
    }
    
    ctrl = registry.primary_controller;
    if (ctrl && ctrl->ops && ctrl->ops->send_ipi) {
        ret = ctrl->ops->send_ipi(ctrl, cpu, vector);
        if (ret == 0) {
            ctrl->stats.ipis_sent++;
        }
    }
    
    return ret;
}

/**
 * Get system interrupt controller capabilities
 */
uint32_t interrupt_controller_get_capabilities(void)
{
    if (!registry.initialized) {
        return 0;
    }
    
    return registry.system_capabilities;
}

/**
 * Get maximum number of IRQs supported by the system
 */
uint32_t interrupt_controller_get_max_irqs(void)
{
    if (!registry.initialized) {
        return 0;
    }
    
    return registry.max_system_irqs;
}

/**
 * Dump controller abstraction state for debugging
 */
void interrupt_controller_dump_state(void)
{
    struct interrupt_controller *ctrl;
    unsigned long flags;
    
    if (!registry.initialized) {
        debug_printf("Controller abstraction not initialized\n");
        return;
    }
    
    spin_lock_irqsave(&registry.lock, flags);
    
    debug_printf("=== Interrupt Controller Abstraction State ===\n");
    debug_printf("Architecture: %d\n", registry.detected_architecture);
    debug_printf("Controllers: %u active, %u total\n", 
                registry.active_controllers, registry.num_controllers);
    debug_printf("System capabilities: 0x%x\n", registry.system_capabilities);
    debug_printf("Max system IRQs: %u\n", registry.max_system_irqs);
    
    if (registry.primary_controller) {
        debug_printf("Primary controller: %s\n", registry.primary_controller->name);
    }
    
    debug_printf("\nController Details:\n");
    ctrl = registry.controllers;
    while (ctrl) {
        debug_printf("  %s:\n", ctrl->name);
        debug_printf("    Type: %d, State: %d\n", ctrl->type, ctrl->state);
        debug_printf("    IRQs: %u, CPUs: %u, Priority levels: %u\n",
                    ctrl->max_irqs, ctrl->max_cpus, ctrl->priority_levels);
        debug_printf("    Capabilities: 0x%x\n", ctrl->capabilities);
        debug_printf("    Stats: %llu enabled, %llu disabled, %llu EOIs, %llu IPIs\n",
                    ctrl->stats.irqs_enabled, ctrl->stats.irqs_disabled,
                    ctrl->stats.eois_sent, ctrl->stats.ipis_sent);
        
        if (ctrl->ops && ctrl->ops->dump_state) {
            ctrl->ops->dump_state(ctrl);
        }
        
        ctrl = ctrl->next;
    }
    
    spin_unlock_irqrestore(&registry.lock, flags);
}

/* ===========================
 * CONTROLLER OPERATION STUBS
 * ===========================
 * 
 * These are placeholder implementations that would call into the
 * actual controller-specific code (PIC, APIC, etc.)
 */

/* 8259A PIC Operations */
static int pic_8259a_init_op(struct interrupt_controller *ctrl) {
    /* Call existing PIC initialization */
    return pic_8259a_init_advanced();
}

static int pic_8259a_enable_irq_op(struct interrupt_controller *ctrl, int irq) {
    pic_8259a_unmask_irq((uint8_t)irq);
    return 0;
}

static int pic_8259a_disable_irq_op(struct interrupt_controller *ctrl, int irq) {
    pic_8259a_mask_irq((uint8_t)irq);
    return 0;
}

static int pic_8259a_eoi_op(struct interrupt_controller *ctrl, int irq) {
    pic_8259a_send_eoi((uint8_t)irq);
    return 0;
}

static void pic_8259a_dump_state_op(struct interrupt_controller *ctrl) {
    pic_8259a_debug_dump();
}

static const struct controller_operations pic_8259a_ops = {
    .init = pic_8259a_init_op,
    .enable_irq = pic_8259a_enable_irq_op,
    .disable_irq = pic_8259a_disable_irq_op,
    .end_of_interrupt = pic_8259a_eoi_op,
    .dump_state = pic_8259a_dump_state_op,
};

/* Local APIC Operations */
static int local_apic_init_op(struct interrupt_controller *ctrl) {
    return local_apic_init();
}

static int local_apic_eoi_op(struct interrupt_controller *ctrl, int irq) {
    apic_send_eoi();
    return 0;
}

static int local_apic_send_ipi_op(struct interrupt_controller *ctrl, int cpu, int vector) {
    return apic_send_ipi((uint32_t)cpu, (uint32_t)vector, 0);
}

static const struct controller_operations local_apic_ops = {
    .init = local_apic_init_op,
    .end_of_interrupt = local_apic_eoi_op,
    .send_ipi = local_apic_send_ipi_op,
};

/* I/O APIC Operations */
static int ioapic_init_op(struct interrupt_controller *ctrl) {
    return ioapic_init_advanced();
}

static int ioapic_enable_irq_op(struct interrupt_controller *ctrl, int irq) {
    return ioapic_enable_irq((uint8_t)irq);
}

static int ioapic_disable_irq_op(struct interrupt_controller *ctrl, int irq) {
    return ioapic_disable_irq((uint8_t)irq);
}

static int ioapic_set_affinity_op(struct interrupt_controller *ctrl, int irq, uint32_t cpu_mask) {
    /* Convert mask to single CPU for now */
    int cpu = 0;
    while (cpu_mask && !(cpu_mask & 1)) {
        cpu_mask >>= 1;
        cpu++;
    }
    return ioapic_set_affinity((uint8_t)irq, (uint32_t)cpu);
}

static void ioapic_dump_state_op(struct interrupt_controller *ctrl) {
    ioapic_debug_dump();
}

static const struct controller_operations ioapic_ops = {
    .init = ioapic_init_op,
    .enable_irq = ioapic_enable_irq_op,
    .disable_irq = ioapic_disable_irq_op,
    .set_affinity = ioapic_set_affinity_op,
    .dump_state = ioapic_dump_state_op,
};

/* MSI Operations */
static int msi_init_op(struct interrupt_controller *ctrl) {
    return msi_init_advanced();
}

static const struct controller_operations msi_ops = {
    .init = msi_init_op,
};

/**
 * Destroy a controller instance
 */
static void destroy_controller(struct interrupt_controller *ctrl)
{
    if (!ctrl) {
        return;
    }
    
    if (ctrl->ops && ctrl->ops->cleanup) {
        ctrl->ops->cleanup(ctrl);
    }
    
    kfree(ctrl);
}