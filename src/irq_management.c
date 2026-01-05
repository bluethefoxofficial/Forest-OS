/*
 * irq_management.c - Comprehensive IRQ Allocation and Management for Forest OS
 * 
 * This module provides:
 * - Dynamic IRQ allocation and deallocation
 * - IRQ sharing and conflict resolution
 * - IRQ descriptor management with statistics
 * - IRQ enable/disable control mechanisms
 * - Multi-architecture support (x86-32, x86-64)
 * - Integration with priority and nesting systems
 * 
 * Copyright (c) 2024 Forest OS Project
 */

#include "interrupt.h"
#include "memory.h"
#include "smp.h"
#include "debug.h"
#include "time.h"
#include "bitmap.h"
#include "mm.h"
#include <string.h>

/* Ensure IRQF_MSI_CAPABLE is defined */
#ifndef IRQF_MSI_CAPABLE
#define IRQF_MSI_CAPABLE        (1U << 12)
#endif

/* IRQ allocation bitmaps and structures */
static DECLARE_BITMAP(irq_allocated_bitmap, 256);
static DECLARE_BITMAP(irq_reserved_bitmap, 256);
static DECLARE_BITMAP(irq_shared_bitmap, 256);

/* IRQ allocation tracking */
struct irq_allocation_info {
    bool allocated;
    bool shared;
    bool msi_capable;
    int reference_count;
    const char *primary_owner;
    uint64_t allocation_time;
    uint32_t allocation_flags;
};

static struct irq_allocation_info irq_allocations[256];

/* IRQ range definitions */
#define IRQ_LEGACY_START     0
#define IRQ_LEGACY_END       15
#define IRQ_SYSTEM_START     16
#define IRQ_SYSTEM_END       31
#define IRQ_DEVICE_START     32
#define IRQ_DEVICE_END       127
#define IRQ_MSI_START        128
#define IRQ_MSI_END          223
#define IRQ_SPECIAL_START    224
#define IRQ_SPECIAL_END      255

/* IRQ allocation policies */
typedef enum {
    IRQ_ALLOC_POLICY_FIRST_AVAILABLE,
    IRQ_ALLOC_POLICY_PREFER_LOW,
    IRQ_ALLOC_POLICY_PREFER_HIGH,
    IRQ_ALLOC_POLICY_MSI_CAPABLE,
    IRQ_ALLOC_POLICY_SHARED_OK
} irq_alloc_policy_t;

/* IRQ management statistics */
struct irq_mgmt_stats {
    uint64_t total_allocations;
    uint64_t total_deallocations;
    uint64_t allocation_failures;
    uint64_t sharing_conflicts;
    uint64_t enable_count;
    uint64_t disable_count;
    uint32_t peak_allocated_irqs;
    uint32_t current_allocated_irqs;
    uint32_t shared_irqs;
};

static struct irq_mgmt_stats irq_stats = {0};
static spinlock_t irq_mgmt_lock;
static bool irq_mgmt_initialized = false;

/* Debug support */
static bool irq_debug_enabled = false;

/* Forward declarations */
static int irq_find_free_range(int start, int end, int count, irq_alloc_policy_t policy);
static bool irq_range_available(int start, int count);
static void irq_update_allocation_stats(bool allocating);
static void irq_reserve_system_ranges(void);
static bool irq_can_share(int irq);
static void irq_setup_descriptor(int irq, const char *name, irq_handler_t handler, void *data);

/* ===========================
 * CORE IRQ MANAGEMENT
 * =========================== */

/**
 * Initialize the IRQ allocation and management system
 */
int irq_management_init(void)
{
    int i;
    
    if (irq_mgmt_initialized) {
        return 0;  /* Already initialized */
    }
    
    /* Initialize allocation bitmaps */
    bitmap_zero(irq_allocated_bitmap, 256);
    bitmap_zero(irq_reserved_bitmap, 256);
    bitmap_zero(irq_shared_bitmap, 256);
    
    /* Initialize allocation tracking */
    memset(irq_allocations, 0, sizeof(irq_allocations));
    
    /* Initialize spinlock */
    spinlock_init(&irq_mgmt_lock, "irq_management");
    
    /* Reserve system and legacy IRQ ranges */
    irq_reserve_system_ranges();
    
    /* Initialize existing IRQ descriptors */
    for (i = 0; i < 256; i++) {
        if (!interrupt_mgr.irq_desc[i].action) {
            memset(&interrupt_mgr.irq_desc[i], 0, sizeof(struct irq_desc));
            spinlock_init(&interrupt_mgr.irq_desc[i].lock, "irq_desc");
            atomic_set(&interrupt_mgr.irq_desc[i].depth, 1);  /* Disabled by default */
            atomic_set(&interrupt_mgr.irq_desc[i].count, 0);
            atomic_set(&interrupt_mgr.irq_desc[i].unhandled, 0);
        }
    }
    
    /* Reset statistics */
    memset(&irq_stats, 0, sizeof(irq_stats));
    
    irq_mgmt_initialized = true;
    debug_printf("IRQ allocation and management system initialized\n");
    
    return 0;
}

/**
 * Cleanup IRQ management system
 */
void irq_management_cleanup(void)
{
    int i;
    unsigned long flags;
    
    if (!irq_mgmt_initialized) {
        return;
    }
    
    spin_lock_irqsave(&irq_mgmt_lock, flags);
    
    /* Free all allocated IRQs */
    for (i = 0; i < 256; i++) {
        if (test_bit(i, irq_allocated_bitmap)) {
            debug_printf("Warning: IRQ %d still allocated during cleanup\n", i);
        }
    }
    
    irq_mgmt_initialized = false;
    spin_unlock_irqrestore(&irq_mgmt_lock, flags);
    
    debug_printf("IRQ allocation system cleaned up\n");
}

/* ===========================
 * IRQ ALLOCATION
 * =========================== */

/**
 * Allocate a single IRQ with specific requirements
 */
int irq_alloc(const char *name, irq_handler_t handler, void *data, uint32_t flags)
{
    unsigned long irq_flags;
    int irq;
    irq_alloc_policy_t policy = IRQ_ALLOC_POLICY_FIRST_AVAILABLE;
    
    if (!irq_mgmt_initialized || !name || !handler) {
        return -1;
    }
    
    /* Determine allocation policy based on flags */
    if (flags & IRQF_MSI_CAPABLE) {
        policy = IRQ_ALLOC_POLICY_MSI_CAPABLE;
    } else if (flags & IRQF_SHARED) {
        policy = IRQ_ALLOC_POLICY_SHARED_OK;
    }
    
    spin_lock_irqsave(&irq_mgmt_lock, irq_flags);
    
    /* Find appropriate IRQ based on policy */
    switch (policy) {
        case IRQ_ALLOC_POLICY_MSI_CAPABLE:
            irq = irq_find_free_range(IRQ_MSI_START, IRQ_MSI_END, 1, policy);
            break;
        case IRQ_ALLOC_POLICY_SHARED_OK:
            irq = irq_find_free_range(IRQ_DEVICE_START, IRQ_DEVICE_END, 1, policy);
            break;
        default:
            irq = irq_find_free_range(IRQ_DEVICE_START, IRQ_DEVICE_END, 1, policy);
            break;
    }
    
    if (irq < 0) {
        irq_stats.allocation_failures++;
        spin_unlock_irqrestore(&irq_mgmt_lock, irq_flags);
        if (irq_debug_enabled) {
            debug_printf("IRQ allocation failed for '%s' (policy %d)\n", name, policy);
        }
        return -1;
    }
    
    /* Mark IRQ as allocated */
    set_bit(irq, irq_allocated_bitmap);
    
    /* Update allocation tracking */
    irq_allocations[irq].allocated = true;
    irq_allocations[irq].shared = (flags & IRQF_SHARED) != 0;
    irq_allocations[irq].msi_capable = (flags & IRQF_MSI_CAPABLE) != 0;
    irq_allocations[irq].reference_count = 1;
    irq_allocations[irq].primary_owner = name;
    irq_allocations[irq].allocation_time = get_system_time_ns();
    irq_allocations[irq].allocation_flags = flags;
    
    /* Setup IRQ descriptor */
    irq_setup_descriptor(irq, name, handler, data);
    
    /* Update statistics */
    irq_update_allocation_stats(true);
    
    spin_unlock_irqrestore(&irq_mgmt_lock, irq_flags);
    
    if (irq_debug_enabled) {
        debug_printf("Allocated IRQ %d for '%s' with flags 0x%x\n", irq, name, flags);
    }
    
    return irq;
}

/**
 * Allocate a range of consecutive IRQs
 */
int irq_alloc_range(const char *name, int count, int *irqs, uint32_t flags)
{
    unsigned long irq_flags;
    int start_irq, i;
    
    if (!irq_mgmt_initialized || !name || count <= 0 || !irqs) {
        return -1;
    }
    
    if (count > 32) {  /* Reasonable limit */
        return -1;
    }
    
    spin_lock_irqsave(&irq_mgmt_lock, irq_flags);
    
    /* Find consecutive free IRQs */
    start_irq = irq_find_free_range(IRQ_DEVICE_START, IRQ_DEVICE_END, count, 
                                   IRQ_ALLOC_POLICY_FIRST_AVAILABLE);
    
    if (start_irq < 0) {
        irq_stats.allocation_failures++;
        spin_unlock_irqrestore(&irq_mgmt_lock, irq_flags);
        return -1;
    }
    
    /* Allocate the entire range */
    for (i = 0; i < count; i++) {
        int irq = start_irq + i;
        
        set_bit(irq, irq_allocated_bitmap);
        
        irq_allocations[irq].allocated = true;
        irq_allocations[irq].shared = false;  /* Range allocations typically not shared */
        irq_allocations[irq].reference_count = 1;
        irq_allocations[irq].primary_owner = name;
        irq_allocations[irq].allocation_time = get_system_time_ns();
        irq_allocations[irq].allocation_flags = flags;
        
        irqs[i] = irq;
        
        /* Update statistics per IRQ */
        irq_update_allocation_stats(true);
    }
    
    spin_unlock_irqrestore(&irq_mgmt_lock, irq_flags);
    
    if (irq_debug_enabled) {
        debug_printf("Allocated IRQ range %d-%d (%d IRQs) for '%s'\n", 
                    start_irq, start_irq + count - 1, count, name);
    }
    
    return count;
}

/**
 * Allocate a specific IRQ number
 */
int irq_alloc_specific(int irq, const char *name, irq_handler_t handler, void *data, uint32_t flags)
{
    unsigned long irq_flags;
    bool can_share = false;
    
    if (!irq_mgmt_initialized || irq < 0 || irq >= 256 || !name || !handler) {
        return -1;
    }
    
    spin_lock_irqsave(&irq_mgmt_lock, irq_flags);
    
    /* Check if IRQ is reserved */
    if (test_bit(irq, irq_reserved_bitmap)) {
        spin_unlock_irqrestore(&irq_mgmt_lock, irq_flags);
        return -1;
    }
    
    /* Check if already allocated */
    if (test_bit(irq, irq_allocated_bitmap)) {
        /* Check if sharing is possible */
        can_share = (flags & IRQF_SHARED) && 
                   irq_allocations[irq].shared && 
                   irq_can_share(irq);
        
        if (!can_share) {
            irq_stats.sharing_conflicts++;
            spin_unlock_irqrestore(&irq_mgmt_lock, irq_flags);
            return -1;
        }
        
        /* Add to shared IRQ */
        irq_allocations[irq].reference_count++;
        set_bit(irq, irq_shared_bitmap);
        
        /* Add action to existing descriptor */
        /* This would chain the handler in the action list */
        
    } else {
        /* Fresh allocation */
        set_bit(irq, irq_allocated_bitmap);
        
        irq_allocations[irq].allocated = true;
        irq_allocations[irq].shared = (flags & IRQF_SHARED) != 0;
        irq_allocations[irq].reference_count = 1;
        irq_allocations[irq].primary_owner = name;
        irq_allocations[irq].allocation_time = get_system_time_ns();
        irq_allocations[irq].allocation_flags = flags;
        
        irq_setup_descriptor(irq, name, handler, data);
        irq_update_allocation_stats(true);
    }
    
    spin_unlock_irqrestore(&irq_mgmt_lock, irq_flags);
    
    if (irq_debug_enabled) {
        debug_printf("Allocated specific IRQ %d for '%s' (%s)\n", 
                    irq, name, can_share ? "shared" : "exclusive");
    }
    
    return 0;
}

/**
 * Free an allocated IRQ
 */
int irq_free(int irq, const char *name)
{
    unsigned long flags;
    bool was_shared = false;
    
    if (!irq_mgmt_initialized || irq < 0 || irq >= 256 || !name) {
        return -1;
    }
    
    spin_lock_irqsave(&irq_mgmt_lock, flags);
    
    if (!test_bit(irq, irq_allocated_bitmap)) {
        spin_unlock_irqrestore(&irq_mgmt_lock, flags);
        return -1;  /* Not allocated */
    }
    
    /* Check reference count for shared IRQs */
    if (irq_allocations[irq].reference_count > 1) {
        irq_allocations[irq].reference_count--;
        was_shared = true;
    } else {
        /* Last reference - completely free the IRQ */
        clear_bit(irq, irq_allocated_bitmap);
        clear_bit(irq, irq_shared_bitmap);
        
        /* Clear allocation tracking */
        memset(&irq_allocations[irq], 0, sizeof(irq_allocations[irq]));
        
        /* Disable the IRQ */
        if (interrupt_mgr.irq_chips[irq] && interrupt_mgr.irq_chips[irq]->disable) {
            interrupt_mgr.irq_chips[irq]->disable(irq);
        }
        
        /* Clear descriptor action */
        interrupt_mgr.irq_desc[irq].action = NULL;
        
        irq_update_allocation_stats(false);
    }
    
    spin_unlock_irqrestore(&irq_mgmt_lock, flags);
    
    if (irq_debug_enabled) {
        debug_printf("Freed IRQ %d from '%s' (%s)\n", 
                    irq, name, was_shared ? "shared reference" : "complete");
    }
    
    return 0;
}

/* ===========================
 * IRQ CONTROL
 * =========================== */

/**
 * Enable an IRQ
 */
int irq_enable(int irq)
{
    unsigned long flags;
    struct irq_desc *desc;
    struct irq_chip *chip;
    
    if (!irq_mgmt_initialized || irq < 0 || irq >= 256) {
        return -1;
    }
    
    if (!test_bit(irq, irq_allocated_bitmap)) {
        return -1;  /* Not allocated */
    }
    
    desc = &interrupt_mgr.irq_desc[irq];
    chip = interrupt_mgr.irq_chips[irq];
    
    spin_lock_irqsave(&desc->lock, flags);
    
    /* Decrease disable depth */
    if (atomic_read(&desc->depth) > 0) {
        if (atomic_dec_and_test(&desc->depth)) {
            /* Actually enable the IRQ */
            if (chip && chip->enable) {
                chip->enable(irq);
            }
            
            irq_stats.enable_count++;
            
            if (irq_debug_enabled) {
                debug_printf("Enabled IRQ %d\n", irq);
            }
        }
    }
    
    spin_unlock_irqrestore(&desc->lock, flags);
    return 0;
}

/**
 * Disable an IRQ
 */
int irq_disable(int irq)
{
    unsigned long flags;
    struct irq_desc *desc;
    struct irq_chip *chip;
    
    if (!irq_mgmt_initialized || irq < 0 || irq >= 256) {
        return -1;
    }
    
    if (!test_bit(irq, irq_allocated_bitmap)) {
        return -1;  /* Not allocated */
    }
    
    desc = &interrupt_mgr.irq_desc[irq];
    chip = interrupt_mgr.irq_chips[irq];
    
    spin_lock_irqsave(&desc->lock, flags);
    
    /* Increase disable depth */
    if (atomic_inc_return(&desc->depth) == 1) {
        /* Actually disable the IRQ */
        if (chip && chip->disable) {
            chip->disable(irq);
        }
        
        irq_stats.disable_count++;
        
        if (irq_debug_enabled) {
            debug_printf("Disabled IRQ %d\n", irq);
        }
    }
    
    spin_unlock_irqrestore(&desc->lock, flags);
    return 0;
}

/**
 * Check if an IRQ is enabled
 */
bool irq_is_enabled(int irq)
{
    if (!irq_mgmt_initialized || irq < 0 || irq >= 256) {
        return false;
    }
    
    if (!test_bit(irq, irq_allocated_bitmap)) {
        return false;
    }
    
    return atomic_read(&interrupt_mgr.irq_desc[irq].depth) == 0;
}

/**
 * Get IRQ allocation information
 */
int irq_get_allocation_info(int irq, struct irq_allocation_info *info)
{
    unsigned long flags;
    
    if (!irq_mgmt_initialized || irq < 0 || irq >= 256 || !info) {
        return -1;
    }
    
    spin_lock_irqsave(&irq_mgmt_lock, flags);
    memcpy(info, &irq_allocations[irq], sizeof(*info));
    spin_unlock_irqrestore(&irq_mgmt_lock, flags);
    
    return 0;
}

/* ===========================
 * STATISTICS AND DEBUGGING
 * =========================== */

/**
 * Get IRQ management statistics
 */
void irq_get_management_stats(struct irq_mgmt_stats *stats)
{
    unsigned long flags;
    
    if (!irq_mgmt_initialized || !stats) {
        return;
    }
    
    spin_lock_irqsave(&irq_mgmt_lock, flags);
    memcpy(stats, &irq_stats, sizeof(*stats));
    spin_unlock_irqrestore(&irq_mgmt_lock, flags);
}

/**
 * Dump IRQ allocation state
 */
void irq_dump_allocations(void)
{
    int i, allocated_count = 0, shared_count = 0;
    unsigned long flags;
    
    if (!irq_mgmt_initialized) {
        debug_printf("IRQ management not initialized\n");
        return;
    }
    
    spin_lock_irqsave(&irq_mgmt_lock, flags);
    
    debug_printf("=== IRQ Allocation State ===\n");
    debug_printf("Total allocations: %llu\n", irq_stats.total_allocations);
    debug_printf("Total deallocations: %llu\n", irq_stats.total_deallocations);
    debug_printf("Allocation failures: %llu\n", irq_stats.allocation_failures);
    debug_printf("Sharing conflicts: %llu\n", irq_stats.sharing_conflicts);
    debug_printf("Enable operations: %llu\n", irq_stats.enable_count);
    debug_printf("Disable operations: %llu\n", irq_stats.disable_count);
    
    debug_printf("\nAllocated IRQs:\n");
    for (i = 0; i < 256; i++) {
        if (test_bit(i, irq_allocated_bitmap)) {
            allocated_count++;
            if (test_bit(i, irq_shared_bitmap)) {
                shared_count++;
            }
            
            debug_printf("  IRQ %3d: %s (refs=%d, %s) - %s\n", 
                        i,
                        irq_allocations[i].primary_owner ? irq_allocations[i].primary_owner : "unknown",
                        irq_allocations[i].reference_count,
                        test_bit(i, irq_shared_bitmap) ? "shared" : "exclusive",
                        irq_is_enabled(i) ? "enabled" : "disabled");
        }
    }
    
    debug_printf("\nSummary: %d allocated, %d shared\n", allocated_count, shared_count);
    
    spin_unlock_irqrestore(&irq_mgmt_lock, flags);
}

/**
 * Enable or disable IRQ debugging
 */
void irq_debug_enable(bool enable)
{
    irq_debug_enabled = enable;
    debug_printf("IRQ debugging %s\n", enable ? "enabled" : "disabled");
}

/* ===========================
 * HELPER FUNCTIONS
 * =========================== */

/**
 * Find free IRQ range based on policy
 */
static int irq_find_free_range(int start, int end, int count, irq_alloc_policy_t policy)
{
    int i, found = 0;
    int best_irq = -1;
    
    if (start < 0 || end >= 256 || start > end || count <= 0) {
        return -1;
    }
    
    switch (policy) {
        case IRQ_ALLOC_POLICY_FIRST_AVAILABLE:
        case IRQ_ALLOC_POLICY_SHARED_OK:
            for (i = start; i <= end - count + 1; i++) {
                if (irq_range_available(i, count)) {
                    return i;
                }
            }
            break;
            
        case IRQ_ALLOC_POLICY_PREFER_LOW:
            /* Same as first available for now */
            return irq_find_free_range(start, end, count, IRQ_ALLOC_POLICY_FIRST_AVAILABLE);
            
        case IRQ_ALLOC_POLICY_PREFER_HIGH:
            for (i = end - count + 1; i >= start; i--) {
                if (irq_range_available(i, count)) {
                    return i;
                }
            }
            break;
            
        case IRQ_ALLOC_POLICY_MSI_CAPABLE:
            /* Look in MSI range first */
            for (i = IRQ_MSI_START; i <= IRQ_MSI_END - count + 1; i++) {
                if (irq_range_available(i, count)) {
                    return i;
                }
            }
            /* Fall back to device range */
            return irq_find_free_range(IRQ_DEVICE_START, IRQ_DEVICE_END, count, 
                                     IRQ_ALLOC_POLICY_FIRST_AVAILABLE);
    }
    
    return -1;
}

/**
 * Check if IRQ range is available
 */
static bool irq_range_available(int start, int count)
{
    int i;
    
    for (i = 0; i < count; i++) {
        int irq = start + i;
        
        if (irq >= 256) {
            return false;
        }
        
        if (test_bit(irq, irq_reserved_bitmap) || 
            test_bit(irq, irq_allocated_bitmap)) {
            return false;
        }
    }
    
    return true;
}

/**
 * Update allocation statistics
 */
static void irq_update_allocation_stats(bool allocating)
{
    if (allocating) {
        irq_stats.total_allocations++;
        irq_stats.current_allocated_irqs++;
        if (irq_stats.current_allocated_irqs > irq_stats.peak_allocated_irqs) {
            irq_stats.peak_allocated_irqs = irq_stats.current_allocated_irqs;
        }
    } else {
        irq_stats.total_deallocations++;
        if (irq_stats.current_allocated_irqs > 0) {
            irq_stats.current_allocated_irqs--;
        }
    }
}

/**
 * Reserve system IRQ ranges
 */
static void irq_reserve_system_ranges(void)
{
    int i;
    
    /* Reserve CPU exception vectors (0-31) */
    for (i = 0; i < 32; i++) {
        set_bit(i, irq_reserved_bitmap);
    }
    
    /* Reserve special purpose vectors */
    set_bit(SYSCALL_VECTOR, irq_reserved_bitmap);
    set_bit(SPURIOUS_VECTOR_PRIMARY, irq_reserved_bitmap);
    set_bit(APIC_SPURIOUS_VECTOR, irq_reserved_bitmap);
}

/**
 * Check if IRQ can be shared
 */
static bool irq_can_share(int irq)
{
    /* Check if current handler allows sharing */
    if (interrupt_mgr.irq_desc[irq].action) {
        return (interrupt_mgr.irq_desc[irq].action->flags & IRQF_SHARED) != 0;
    }
    
    return false;
}

/**
 * Setup IRQ descriptor for allocated IRQ
 */
static void irq_setup_descriptor(int irq, const char *name, irq_handler_t handler, void *data)
{
    struct irq_desc *desc;
    struct irq_action *action;
    
    desc = &interrupt_mgr.irq_desc[irq];
    
    /* Allocate IRQ action structure */
    action = (struct irq_action *)kmalloc(sizeof(struct irq_action), GFP_KERNEL);
    if (!action) {
        return;
    }
    
    memset(action, 0, sizeof(*action));
    action->handler = handler;
    action->name = name;
    action->dev_id = data;
    action->flags = irq_allocations[irq].allocation_flags;
    atomic_set(&action->count, 0);
    
    desc->action = action;
    desc->name = name;
    
    /* Set default priority */
    if (priority_mgr.initialized) {
        interrupt_set_priority(irq, INTERRUPT_PRIORITY_NORMAL, 
                             PRIORITY_FLAG_PREEMPTIBLE);
    }
}