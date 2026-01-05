/*
 * interrupt_priority.c - Comprehensive Interrupt Priority and Nesting Support for Forest OS
 * 
 * This module provides:
 * - Multi-level interrupt priority management
 * - Interrupt nesting with preemption control
 * - Priority inheritance to prevent priority inversion
 * - Real-time deadline support
 * - Per-CPU context management
 * - Statistical tracking and debugging
 * 
 * Supports both x86-32 and x86-64 architectures with UEFI integration.
 * 
 * Copyright (c) 2024 Forest OS Project
 */

#include "interrupt.h"
#include "memory.h"
#include "smp.h"
#include "debug.h"
#include "time.h"
#include <string.h>

/* Priority flag mask for clearing priority bits before setting new ones */
#ifndef PRIORITY_FLAG_MASK
#define PRIORITY_FLAG_MASK  (PRIORITY_FLAG_PREEMPTIBLE | PRIORITY_FLAG_CRITICAL | \
                             PRIORITY_FLAG_ATOMIC | PRIORITY_FLAG_REALTIME | \
                             PRIORITY_FLAG_INHERIT_PRIORITY)
#endif

/* Architecture-aware stack pointer access */
#if ARCH_64BIT
#define FRAME_STACK_PTR(frame) ((frame).rsp)
#else
#define FRAME_STACK_PTR(frame) ((frame).useresp)
#endif

/* Global priority manager instance */
struct priority_manager priority_mgr = {0};

/* Debug and tracing support */
static bool priority_debug_enabled = false;
static uint64_t priority_trace_mask = 0;

/* Per-CPU thread-local storage for fast access */
static __thread int current_cpu = 0;
static __thread struct cpu_priority_state *local_cpu_state = NULL;

/* Forward declarations */
static void priority_manager_init_defaults(void);
static void cpu_priority_state_init(struct cpu_priority_state *state);
static bool priority_can_inherit(int vector);
static void priority_update_statistics(int cpu, int event_type);
static void priority_detect_inversion(int cpu);

/* ===========================
 * CORE PRIORITY MANAGEMENT
 * =========================== */

/**
 * Initialize the interrupt priority and nesting system
 */
int interrupt_priority_init(void)
{
    int cpu;
    
    if (priority_mgr.initialized) {
        return 0;  /* Already initialized */
    }
    
    /* Initialize global priority manager */
    memset(&priority_mgr, 0, sizeof(priority_mgr));
    spinlock_init(&priority_mgr.global_priority_lock, "global_priority");
    
    /* Initialize per-CPU states */
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        cpu_priority_state_init(&priority_mgr.cpu_states[cpu]);
    }
    
    /* Set up default priority levels for common interrupt vectors */
    priority_manager_init_defaults();
    
    /* Initialize atomic counters */
    atomic64_set(&priority_mgr.total_nested_interrupts, 0);
    atomic64_set(&priority_mgr.total_preemptions, 0);
    atomic64_set(&priority_mgr.priority_violations, 0);
    
    priority_mgr.initialized = true;
    
    /* Initialize current CPU */
    current_cpu = smp_get_current_cpu();
    local_cpu_state = &priority_mgr.cpu_states[current_cpu];
    
    debug_printf("Interrupt priority system initialized for %d CPUs\n", NR_CPUS);
    return 0;
}

/**
 * Initialize priority management for a specific CPU
 */
int interrupt_priority_init_cpu(int cpu)
{
    if (cpu >= NR_CPUS || cpu < 0) {
        return -1;
    }
    
    cpu_priority_state_init(&priority_mgr.cpu_states[cpu]);
    
    debug_printf("Priority management initialized for CPU %d\n", cpu);
    return 0;
}

/**
 * Clean up priority management system
 */
void interrupt_priority_cleanup(void)
{
    if (!priority_mgr.initialized) {
        return;
    }
    
    priority_mgr.initialized = false;
    debug_printf("Interrupt priority system cleaned up\n");
}

/* ===========================
 * PRIORITY CONFIGURATION
 * =========================== */

/**
 * Set priority for an interrupt vector
 */
int interrupt_set_priority(int vector, uint8_t priority, uint32_t flags)
{
    unsigned long irq_flags;
    
    if (vector < 0 || vector >= 256) {
        return -1;
    }
    
    spin_lock_irqsave(&priority_mgr.global_priority_lock, irq_flags);
    
    priority_mgr.vector_priorities[vector] = priority;
    
    /* Update IRQ descriptor if it exists */
    if (vector >= IRQ_BASE_OFFSET && interrupt_mgr.irq_desc[vector].action) {
        interrupt_mgr.irq_desc[vector].action->flags = 
            (interrupt_mgr.irq_desc[vector].action->flags & ~PRIORITY_FLAG_MASK) | flags;
    }
    
    spin_unlock_irqrestore(&priority_mgr.global_priority_lock, irq_flags);
    
    if (priority_debug_enabled) {
        debug_printf("Set vector %d priority to %u with flags 0x%x\n", 
                    vector, priority, flags);
    }
    
    return 0;
}

/**
 * Get priority for an interrupt vector
 */
uint8_t interrupt_get_priority(int vector)
{
    if (vector < 0 || vector >= 256) {
        return INTERRUPT_PRIORITY_LOWEST;
    }
    
    return priority_mgr.vector_priorities[vector];
}

/**
 * Set real-time deadline for an interrupt vector
 */
int interrupt_set_realtime_deadline(int vector, uint64_t deadline_ns)
{
    if (vector < 0 || vector >= 256) {
        return -1;
    }
    
    /* This would be stored in extended vector information */
    /* For now, we set a flag that this vector has real-time constraints */
    return interrupt_set_priority(vector, 
                                interrupt_get_priority(vector),
                                PRIORITY_FLAG_REALTIME);
}

/* ===========================
 * NESTING CONTROL
 * =========================== */

/**
 * Check if a new interrupt can preempt the current one
 */
bool interrupt_can_preempt(int new_vector, int current_vector)
{
    struct cpu_priority_state *state;
    uint8_t new_priority, current_priority;
    uint32_t current_flags;
    
    if (!priority_mgr.initialized) {
        return false;  /* Conservative approach when not initialized */
    }
    
    /* Get current CPU state */
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    
    /* Check preemption count */
    if (atomic_read(&state->preemption_count) > 0) {
        return false;  /* Preemption disabled */
    }
    
    /* Get priorities */
    new_priority = interrupt_get_priority(new_vector);
    current_priority = (current_vector >= 0) ? 
                      interrupt_get_priority(current_vector) : 
                      state->nesting.current_priority;
    
    /* Higher priority (lower number) can always preempt */
    if (priority_is_higher(new_priority, current_priority)) {
        /* Check if current interrupt allows preemption */
        if (current_vector >= 0) {
            current_flags = interrupt_mgr.irq_desc[current_vector].action ? 
                           interrupt_mgr.irq_desc[current_vector].action->flags : 0;
            
            if (!can_be_preempted(current_flags)) {
                return false;
            }
        }
        
        /* Check nesting depth */
        if (state->nesting.nesting_level >= MAX_INTERRUPT_NESTING_DEPTH - 1) {
            return false;  /* Maximum nesting depth reached */
        }
        
        return true;
    }
    
    return false;
}

/**
 * Enter nested interrupt handling
 */
int interrupt_enter_nested(int vector, struct interrupt_context *ctx)
{
    struct cpu_priority_state *state;
    struct interrupt_nesting_context *nesting;
    unsigned long irq_flags;
    uint8_t vector_priority;
    int level;
    
    if (!priority_mgr.initialized || !ctx) {
        return -1;
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    nesting = &state->nesting;
    
    spin_lock_irqsave(&state->priority_lock, irq_flags);
    
    /* Check nesting depth */
    if (nesting->nesting_level >= MAX_INTERRUPT_NESTING_DEPTH) {
        spin_unlock_irqrestore(&state->priority_lock, irq_flags);
        return -1;  /* Too deep */
    }
    
    level = nesting->nesting_level;
    vector_priority = interrupt_get_priority(vector);
    
    /* Save current context on nesting stack */
    nesting->contexts[level] = ctx;
    nesting->priorities[level] = nesting->current_priority;
    nesting->stack_pointers[level] = (void *)(uintptr_t)FRAME_STACK_PTR(ctx->frame);  /* Save stack pointer */
    
    /* Update nesting state */
    nesting->current_priority = vector_priority;
    nesting->nesting_level++;
    nesting->entry_timestamp = get_system_time_ns();
    
    /* Update statistics */
    state->stats.total_nesting_events++;
    if (nesting->nesting_level > state->stats.max_nesting_depth) {
        state->stats.max_nesting_depth = nesting->nesting_level;
    }
    
    atomic64_inc(&priority_mgr.total_nested_interrupts);
    
    spin_unlock_irqrestore(&state->priority_lock, irq_flags);
    
    if (priority_debug_enabled) {
        debug_printf("CPU%d: Entered nested interrupt vector %d (level %d, priority %u)\n",
                    smp_get_current_cpu(), vector, level, vector_priority);
    }
    
    return level;
}

/**
 * Exit nested interrupt handling
 */
void interrupt_exit_nested(int vector)
{
    struct cpu_priority_state *state;
    struct interrupt_nesting_context *nesting;
    unsigned long irq_flags;
    int level;
    uint64_t duration;
    
    if (!priority_mgr.initialized) {
        return;
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    nesting = &state->nesting;
    
    spin_lock_irqsave(&state->priority_lock, irq_flags);
    
    if (nesting->nesting_level == 0) {
        spin_unlock_irqrestore(&state->priority_lock, irq_flags);
        return;  /* Not in nested state */
    }
    
    level = nesting->nesting_level - 1;
    
    /* Calculate interrupt duration */
    duration = get_system_time_ns() - nesting->entry_timestamp;
    
    /* Restore previous priority and context */
    nesting->current_priority = nesting->priorities[level];
    nesting->nesting_level--;
    
    /* Clear nesting stack entry */
    nesting->contexts[level] = NULL;
    nesting->stack_pointers[level] = NULL;
    nesting->priorities[level] = 0;
    
    spin_unlock_irqrestore(&state->priority_lock, irq_flags);
    
    if (priority_debug_enabled) {
        debug_printf("CPU%d: Exited nested interrupt vector %d (level %d, duration %llu ns)\n",
                    smp_get_current_cpu(), vector, level, duration);
    }
}

/**
 * Get current interrupt nesting level
 */
int interrupt_get_nesting_level(void)
{
    struct cpu_priority_state *state;
    
    if (!priority_mgr.initialized) {
        return 0;
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    return state->nesting.nesting_level;
}

/**
 * Check if currently in nested interrupt
 */
bool interrupt_is_nested(void)
{
    return interrupt_get_nesting_level() > 0;
}

/* ===========================
 * PRIORITY INHERITANCE
 * =========================== */

/**
 * Temporarily boost interrupt priority to avoid priority inversion
 */
int interrupt_boost_priority(int vector, uint8_t new_priority, uint64_t duration_ns)
{
    struct cpu_priority_state *state;
    unsigned long irq_flags;
    
    if (!priority_mgr.initialized) {
        return -1;
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    
    spin_lock_irqsave(&state->priority_lock, irq_flags);
    
    /* Save original priority if not already boosted */
    if (state->nesting.original_priority == 0) {
        state->nesting.original_priority = state->nesting.current_priority;
    }
    
    /* Set new priority */
    state->nesting.current_priority = new_priority;
    state->last_priority_boost = get_system_time_ns();
    
    /* Update statistics */
    state->stats.priority_inheritances++;
    
    spin_unlock_irqrestore(&state->priority_lock, irq_flags);
    
    if (priority_debug_enabled) {
        debug_printf("CPU%d: Boosted vector %d priority to %u for %llu ns\n",
                    smp_get_current_cpu(), vector, new_priority, duration_ns);
    }
    
    return 0;
}

/**
 * Inherit priority from a higher-priority interrupt
 */
void interrupt_inherit_priority(int vector, uint8_t inherited_priority)
{
    if (priority_can_inherit(vector)) {
        interrupt_boost_priority(vector, inherited_priority, 0);
    }
}

/**
 * Restore original priority after boost
 */
void interrupt_restore_priority(int vector)
{
    struct cpu_priority_state *state;
    unsigned long irq_flags;
    
    if (!priority_mgr.initialized) {
        return;
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    
    spin_lock_irqsave(&state->priority_lock, irq_flags);
    
    if (state->nesting.original_priority != 0) {
        state->nesting.current_priority = state->nesting.original_priority;
        state->nesting.original_priority = 0;
    }
    
    spin_unlock_irqrestore(&state->priority_lock, irq_flags);
    
    if (priority_debug_enabled) {
        debug_printf("CPU%d: Restored original priority for vector %d\n",
                    smp_get_current_cpu(), vector);
    }
}

/**
 * Detect and resolve priority inversion
 */
void interrupt_resolve_priority_inversion(void)
{
    int cpu;
    struct cpu_priority_state *state;
    
    if (!priority_mgr.initialized) {
        return;
    }
    
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        state = &priority_mgr.cpu_states[cpu];
        if (state->priority_inversion_detected) {
            priority_detect_inversion(cpu);
            state->priority_inversion_detected = false;
        }
    }
}

/* ===========================
 * PREEMPTION CONTROL
 * =========================== */

/**
 * Disable interrupt preemption
 */
void interrupt_preempt_disable(void)
{
    struct cpu_priority_state *state;
    
    if (!priority_mgr.initialized) {
        return;
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    atomic_inc(&state->preemption_count);
}

/**
 * Enable interrupt preemption
 */
void interrupt_preempt_enable(void)
{
    struct cpu_priority_state *state;
    
    if (!priority_mgr.initialized) {
        return;
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    
    if (atomic_read(&state->preemption_count) > 0) {
        atomic_dec(&state->preemption_count);
    }
}

/**
 * Check if preemption is enabled
 */
bool interrupt_preemptible(void)
{
    struct cpu_priority_state *state;
    
    if (!priority_mgr.initialized) {
        return false;
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    return atomic_read(&state->preemption_count) == 0;
}

/**
 * Get current preemption count
 */
int interrupt_preempt_count(void)
{
    struct cpu_priority_state *state;
    
    if (!priority_mgr.initialized) {
        return 0;
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    return atomic_read(&state->preemption_count);
}

/* ===========================
 * REAL-TIME SUPPORT
 * =========================== */

/**
 * Set deadline for real-time interrupt
 */
int interrupt_set_deadline(int vector, uint64_t deadline_ns)
{
    struct cpu_priority_state *state;
    unsigned long irq_flags;
    
    if (!priority_mgr.initialized) {
        return -1;
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    
    spin_lock_irqsave(&state->priority_lock, irq_flags);
    state->nesting.deadline = deadline_ns;
    spin_unlock_irqrestore(&state->priority_lock, irq_flags);
    
    return 0;
}

/**
 * Check if interrupt deadline is being met
 */
bool interrupt_check_deadline(int vector)
{
    struct cpu_priority_state *state;
    uint64_t current_time, deadline;
    
    if (!priority_mgr.initialized) {
        return true;  /* No deadline enforcement */
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    deadline = state->nesting.deadline;
    
    if (deadline == 0) {
        return true;  /* No deadline set */
    }
    
    current_time = get_system_time_ns();
    return current_time <= deadline;
}

/**
 * Handle missed deadline
 */
void interrupt_deadline_missed(int vector)
{
    struct cpu_priority_state *state;
    
    if (!priority_mgr.initialized) {
        return;
    }
    
    state = &priority_mgr.cpu_states[smp_get_current_cpu()];
    state->stats.deadline_misses++;
    
    if (priority_debug_enabled) {
        debug_printf("CPU%d: Deadline missed for vector %d\n",
                    smp_get_current_cpu(), vector);
    }
}

/* ===========================
 * STATISTICS AND DEBUGGING
 * =========================== */

/**
 * Get priority statistics for a specific CPU
 */
void interrupt_priority_get_stats(int cpu, struct cpu_priority_state *stats)
{
    if (!priority_mgr.initialized || cpu >= NR_CPUS || !stats) {
        return;
    }
    
    memcpy(stats, &priority_mgr.cpu_states[cpu], sizeof(*stats));
}

/**
 * Dump priority system state for debugging
 */
void interrupt_priority_dump_state(void)
{
    int cpu;
    struct cpu_priority_state *state;
    
    if (!priority_mgr.initialized) {
        debug_printf("Priority system not initialized\n");
        return;
    }
    
    debug_printf("=== Interrupt Priority System State ===\n");
    debug_printf("Total nested interrupts: %llu\n", 
                atomic64_read(&priority_mgr.total_nested_interrupts));
    debug_printf("Total preemptions: %llu\n", 
                atomic64_read(&priority_mgr.total_preemptions));
    debug_printf("Priority violations: %llu\n", 
                atomic64_read(&priority_mgr.priority_violations));
    
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        state = &priority_mgr.cpu_states[cpu];
        debug_printf("\nCPU %d:\n", cpu);
        debug_printf("  Current priority: %u\n", state->nesting.current_priority);
        debug_printf("  Nesting level: %u\n", state->nesting.nesting_level);
        debug_printf("  Preemption count: %d\n", atomic_read(&state->preemption_count));
        debug_printf("  Nesting events: %llu\n", state->stats.total_nesting_events);
        debug_printf("  Max nesting depth: %llu\n", state->stats.max_nesting_depth);
        debug_printf("  Priority inversions: %llu\n", state->stats.priority_inversions);
        debug_printf("  Preemptions: %llu\n", state->stats.preemptions);
        debug_printf("  Deadline misses: %llu\n", state->stats.deadline_misses);
    }
}

/**
 * Enable or disable priority debugging
 */
void interrupt_priority_debug_enable(bool enable)
{
    priority_debug_enabled = enable;
    debug_printf("Priority debugging %s\n", enable ? "enabled" : "disabled");
}

/**
 * Trace interrupt nesting events
 */
void interrupt_priority_trace_nesting(int vector, bool entering)
{
    if (!priority_debug_enabled) {
        return;
    }
    
    if (entering) {
        debug_printf("TRACE: CPU%d entering interrupt vector %d (priority %u)\n",
                    smp_get_current_cpu(), vector, interrupt_get_priority(vector));
    } else {
        debug_printf("TRACE: CPU%d exiting interrupt vector %d\n",
                    smp_get_current_cpu(), vector);
    }
}

/* ===========================
 * HELPER FUNCTIONS
 * =========================== */

/**
 * Initialize default priority mappings
 */
static void priority_manager_init_defaults(void)
{
    int i;
    
    /* Initialize all vectors to normal priority */
    for (i = 0; i < 256; i++) {
        priority_mgr.vector_priorities[i] = INTERRUPT_PRIORITY_NORMAL;
    }
    
    /* Set specific priorities for known interrupt types */
    priority_mgr.vector_priorities[EXCEPTION_NMI] = INTERRUPT_PRIORITY_NMI;
    priority_mgr.vector_priorities[EXCEPTION_MACHINE_CHECK] = INTERRUPT_PRIORITY_MACHINE_CHECK;
    priority_mgr.vector_priorities[EXCEPTION_DOUBLE_FAULT] = INTERRUPT_PRIORITY_DOUBLE_FAULT;
    
    /* Timer interrupts */
    priority_mgr.vector_priorities[IRQ_TIMER] = INTERRUPT_PRIORITY_TIMER;
    priority_mgr.vector_priorities[IRQ_RTC] = INTERRUPT_PRIORITY_TIMER;
    
    /* High priority I/O */
    priority_mgr.vector_priorities[IRQ_KEYBOARD] = INTERRUPT_PRIORITY_HIGH;
    priority_mgr.vector_priorities[IRQ_MOUSE] = INTERRUPT_PRIORITY_HIGH;
    
    /* Storage */
    priority_mgr.vector_priorities[IRQ_PRIMARY_HD] = INTERRUPT_PRIORITY_NORMAL;
    priority_mgr.vector_priorities[IRQ_SECONDARY_HD] = INTERRUPT_PRIORITY_NORMAL;
    priority_mgr.vector_priorities[IRQ_FLOPPY] = INTERRUPT_PRIORITY_LOW;
    
    /* Communications */
    priority_mgr.vector_priorities[IRQ_COM1] = INTERRUPT_PRIORITY_NORMAL;
    priority_mgr.vector_priorities[IRQ_COM2] = INTERRUPT_PRIORITY_NORMAL;
    
    /* Parallel ports */
    priority_mgr.vector_priorities[IRQ_LPT1] = INTERRUPT_PRIORITY_LOW;
    priority_mgr.vector_priorities[IRQ_LPT2] = INTERRUPT_PRIORITY_LOW;
}

/**
 * Initialize a CPU priority state structure
 */
static void cpu_priority_state_init(struct cpu_priority_state *state)
{
    memset(state, 0, sizeof(*state));
    
    spinlock_init(&state->priority_lock, "cpu_priority");
    atomic_set(&state->preemption_count, 0);
    
    /* Initialize nesting context */
    state->nesting.current_priority = INTERRUPT_PRIORITY_LOWEST;
    state->nesting.original_priority = 0;
    state->nesting.nesting_level = 0;
    state->nesting.flags = PRIORITY_FLAG_PREEMPTIBLE;
    state->nesting.deadline = 0;
}

/**
 * Check if a vector can inherit priority
 */
static bool priority_can_inherit(int vector)
{
    uint32_t flags = 0;
    
    if (vector >= IRQ_BASE_OFFSET && 
        interrupt_mgr.irq_desc[vector].action) {
        flags = interrupt_mgr.irq_desc[vector].action->flags;
    }
    
    return (flags & PRIORITY_FLAG_INHERIT_PRIORITY) != 0;
}

/**
 * Update statistics for priority events
 */
static void priority_update_statistics(int cpu, int event_type)
{
    struct cpu_priority_state *state;
    
    if (cpu >= NR_CPUS) {
        return;
    }
    
    state = &priority_mgr.cpu_states[cpu];
    
    switch (event_type) {
        case 0:  /* Preemption */
            state->stats.preemptions++;
            atomic64_inc(&priority_mgr.total_preemptions);
            break;
        case 1:  /* Priority inversion */
            state->stats.priority_inversions++;
            atomic64_inc(&priority_mgr.priority_violations);
            break;
        case 2:  /* Priority inheritance */
            state->stats.priority_inheritances++;
            break;
    }
}

/**
 * Detect priority inversion
 */
static void priority_detect_inversion(int cpu)
{
    struct cpu_priority_state *state;
    
    if (cpu >= NR_CPUS) {
        return;
    }
    
    state = &priority_mgr.cpu_states[cpu];
    
    /* Simple priority inversion detection */
    if (state->nesting.nesting_level > 1) {
        int i;
        for (i = 0; i < state->nesting.nesting_level - 1; i++) {
            if (priority_is_higher(state->nesting.priorities[i], 
                                 state->nesting.priorities[i + 1])) {
                /* Priority inversion detected */
                priority_update_statistics(cpu, 1);
                
                if (priority_debug_enabled) {
                    debug_printf("CPU%d: Priority inversion detected at nesting level %d\n", 
                               cpu, i);
                }
                break;
            }
        }
    }
}