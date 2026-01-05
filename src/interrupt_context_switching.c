/*
 * interrupt_context_switching.c - Interrupt Context Switching and Stack Management for Forest OS
 * 
 * This module provides:
 * - Complete CPU context preservation and restoration
 * - Privilege level transition management (user/kernel)
 * - Interrupt stack switching and isolation
 * - Nested interrupt context management
 * - FPU/SSE state handling during interrupts
 * - Security boundaries and stack protection
 * - Performance optimization for context switches
 * 
 * Handles the complex task of safely switching execution contexts
 * during interrupt processing while maintaining system security
 * and performance.
 * 
 * Copyright (c) 2024 Forest OS Project
 */

#include "interrupt.h"
#include "memory.h"
#include "smp.h"
#include "debug.h"
#include "time.h"
#include "errno_defs.h"
// #include "process.h"  // TODO: process.h doesn't exist yet
#include <string.h>

/* Memory allocation constants */
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#ifndef GFP_KERNEL
#define GFP_KERNEL 0x01
#endif

#ifndef GFP_ATOMIC
#define GFP_ATOMIC 0x02
#endif

/* Architecture-aware stack pointer access */
#if ARCH_64BIT
#define FRAME_STACK_PTR(frame) ((frame).rsp)
#else
#define FRAME_STACK_PTR(frame) ((frame).useresp)
#endif

/* Context switching modes */
typedef enum {
    CONTEXT_SWITCH_MINIMAL,     /* Save/restore minimal registers */
    CONTEXT_SWITCH_STANDARD,    /* Standard register set */
    CONTEXT_SWITCH_FULL,        /* Full state including FPU/SSE */
    CONTEXT_SWITCH_EXTENDED     /* Extended state with AVX/etc. */
} context_switch_mode_t;

/* Privilege levels */
typedef enum {
    PRIVILEGE_USER = 3,         /* User mode (ring 3) */
    PRIVILEGE_KERNEL = 0        /* Kernel mode (ring 0) */
} privilege_level_t;

/* Extended CPU state flags */
#define STATE_FLAG_FPU          (1U << 0)   /* FPU state needs saving */
#define STATE_FLAG_SSE          (1U << 1)   /* SSE state needs saving */
#define STATE_FLAG_AVX          (1U << 2)   /* AVX state needs saving */
#define STATE_FLAG_AVX512       (1U << 3)   /* AVX-512 state needs saving */
#define STATE_FLAG_MPX          (1U << 4)   /* Memory Protection Extensions */
#define STATE_FLAG_PKRU         (1U << 5)   /* Protection Key Rights */

/* FPU/Extended state structure */
struct extended_state {
    /* FPU state (x87) */
    struct {
        uint16_t fcw;           /* FPU Control Word */
        uint16_t fsw;           /* FPU Status Word */
        uint8_t  ftw;           /* FPU Tag Word */
        uint8_t  reserved1;
        uint16_t fop;           /* FPU Opcode */
        union {
            struct {
                uint32_t fpu_ip;    /* FPU Instruction Pointer */
                uint16_t fpu_cs;    /* FPU Code Segment */
                uint16_t reserved2;
                uint32_t fpu_dp;    /* FPU Data Pointer */
                uint16_t fpu_ds;    /* FPU Data Segment */
                uint16_t reserved3;
            } ia32;
            struct {
                uint64_t fpu_ip;    /* FPU Instruction Pointer */
                uint64_t fpu_dp;    /* FPU Data Pointer */
            } x64;
        };
        uint8_t st_space[80];   /* 8 * 10 bytes for each FP-reg */
    } fpu;
    
    /* SSE state (XMM registers) */
    struct {
        uint32_t mxcsr;         /* MXCSR Register */
        uint32_t mxcsr_mask;    /* MXCSR Mask */
        uint8_t xmm_space[256]; /* 16 * 16 bytes for each XMM-reg */
    } sse;
    
    /* Extended state (XSAVE) */
    struct {
        uint64_t xstate_bv;     /* XSTATE_BV header */
        uint64_t xcomp_bv;      /* XCOMP_BV header */
        uint8_t reserved[48];   /* Reserved area */
        uint8_t extended[0];    /* Variable size extended state */
    } xsave;
} __attribute__((packed, aligned(64)));

/* Complete interrupt context */
struct complete_interrupt_context {
    /* Basic CPU state (already in interrupt_context) */
    struct interrupt_context basic;
    
    /* Extended state */
    struct extended_state *ext_state;
    uint32_t ext_state_flags;
    uint32_t ext_state_size;
    
    /* Privilege level information */
    privilege_level_t previous_privilege;
    privilege_level_t current_privilege;
    
    /* Stack information */
    void *user_stack_ptr;       /* User mode stack pointer */
    void *kernel_stack_ptr;     /* Kernel mode stack pointer */
    void *interrupt_stack_ptr;  /* Interrupt stack pointer */
    
    /* Context metadata */
    uint64_t context_id;        /* Unique context identifier */
    uint64_t switch_timestamp;  /* When context was switched */
    struct process *process;    /* Associated process (if any) */
    
    /* Nesting information */
    uint32_t nesting_level;
    struct complete_interrupt_context *parent_context;
    
    /* Security and validation */
    uint32_t context_checksum;  /* Context integrity checksum */
    bool context_valid;         /* Context validation flag */
} __attribute__((aligned(64)));

/* Per-CPU context switching state */
struct cpu_context_state {
    /* Current context stack */
    struct complete_interrupt_context *context_stack[MAX_INTERRUPT_NESTING_DEPTH];
    uint32_t context_stack_depth;
    
    /* Interrupt stacks */
    void *interrupt_stacks[4];  /* Different privilege levels */
    size_t stack_sizes[4];
    void *current_stack;
    
    /* Extended state management */
    struct extended_state *fpu_state_buffer;
    bool extended_state_enabled;
    uint64_t supported_features;
    context_switch_mode_t default_mode;
    
    /* Performance counters */
    struct {
        uint64_t total_switches;
        uint64_t user_to_kernel;
        uint64_t kernel_to_user;
        uint64_t nested_switches;
        uint64_t fpu_saves;
        uint64_t fpu_restores;
        uint64_t context_corruptions;
        uint64_t total_switch_time_ns;
        uint64_t max_switch_time_ns;
    } stats;
    
    /* Stack overflow detection */
    void *stack_guard_pages[4];
    uint64_t stack_overflow_count;
    bool stack_protection_enabled;
} __attribute__((aligned(64)));

/* Global context switching manager */
struct context_switch_manager {
    struct cpu_context_state cpu_states[NR_CPUS];
    
    /* Global configuration */
    struct {
        context_switch_mode_t default_mode;
        bool lazy_fpu_switching;
        bool stack_protection;
        bool context_validation;
        size_t default_stack_size;
        bool performance_monitoring;
    } config;
    
    /* Extended state capabilities */
    struct {
        bool xsave_supported;
        bool xsaveopt_supported;
        bool xsavec_supported;
        bool xsaves_supported;
        uint64_t xsave_area_size;
        uint64_t supported_xcr0;
    } capabilities;
    
    /* Global statistics */
    struct {
        uint64_t total_context_switches;
        uint64_t total_switch_time_ns;
        uint64_t privilege_violations;
        uint64_t stack_overflows;
        uint64_t context_corruptions;
        uint32_t peak_nesting_level;
    } global_stats;
    
    bool initialized;
    spinlock_t lock;
};

static struct context_switch_manager ctx_mgr = {0};

/* Forward declarations */
static int context_detect_capabilities(void);
static int context_setup_cpu_stacks(int cpu);
static void context_setup_extended_state(int cpu);
static struct complete_interrupt_context *context_allocate_context(void);
static void context_free_context(struct complete_interrupt_context *ctx);
static void context_save_extended_state(struct complete_interrupt_context *ctx);
static void context_restore_extended_state(struct complete_interrupt_context *ctx);
static uint32_t context_calculate_checksum(struct complete_interrupt_context *ctx);
static bool context_validate_context(struct complete_interrupt_context *ctx);
static void context_update_statistics(int cpu, uint64_t switch_time, privilege_level_t from, privilege_level_t to);

/* Assembly function declarations */
extern void save_cpu_context_asm(struct cpu_registers *regs);
extern void restore_cpu_context_asm(struct cpu_registers *regs);
extern void switch_to_kernel_stack_asm(void *stack_ptr);
extern void switch_to_user_stack_asm(void *stack_ptr);

/* ===========================
 * INITIALIZATION
 * =========================== */

/**
 * Initialize interrupt context switching and stack management
 */
int interrupt_context_switching_init(void)
{
    int cpu, ret;
    
    if (ctx_mgr.initialized) {
        return 0;
    }
    
    memset(&ctx_mgr, 0, sizeof(ctx_mgr));
    spinlock_init(&ctx_mgr.lock, "context_manager");
    
    /* Detect CPU capabilities */
    ret = context_detect_capabilities();
    if (ret < 0) {
        debug_printf("Failed to detect context switching capabilities\n");
        return ret;
    }
    
    /* Set default configuration */
    ctx_mgr.config.default_mode = CONTEXT_SWITCH_STANDARD;
    ctx_mgr.config.lazy_fpu_switching = true;
    ctx_mgr.config.stack_protection = true;
    ctx_mgr.config.context_validation = true;
    ctx_mgr.config.default_stack_size = 16 * 1024; /* 16KB */
    ctx_mgr.config.performance_monitoring = true;
    
    debug_printf("Initializing interrupt context switching\n");
    debug_printf("Extended state support: XSAVE=%s, size=%llu bytes\n",
                ctx_mgr.capabilities.xsave_supported ? "yes" : "no",
                ctx_mgr.capabilities.xsave_area_size);
    
    /* Initialize per-CPU context switching */
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        ret = context_setup_cpu_stacks(cpu);
        if (ret < 0) {
            debug_printf("Failed to setup stacks for CPU %d: %d\n", cpu, ret);
            goto cleanup;
        }
        
        context_setup_extended_state(cpu);
        ctx_mgr.cpu_states[cpu].default_mode = ctx_mgr.config.default_mode;
        ctx_mgr.cpu_states[cpu].stack_protection_enabled = ctx_mgr.config.stack_protection;
    }
    
    ctx_mgr.initialized = true;
    
    debug_printf("Interrupt context switching initialized for %d CPUs\n", NR_CPUS);
    debug_printf("Default mode: %d, Stack protection: %s, FPU lazy: %s\n",
                ctx_mgr.config.default_mode,
                ctx_mgr.config.stack_protection ? "enabled" : "disabled",
                ctx_mgr.config.lazy_fpu_switching ? "enabled" : "disabled");
    
    return 0;
    
cleanup:
    for (int cleanup_cpu = 0; cleanup_cpu < cpu; cleanup_cpu++) {
        /* Cleanup allocated resources */
        struct cpu_context_state *state = &ctx_mgr.cpu_states[cleanup_cpu];
        
        for (int stack = 0; stack < 4; stack++) {
            if (state->interrupt_stacks[stack]) {
                kfree(state->interrupt_stacks[stack]);
                state->interrupt_stacks[stack] = NULL;
            }
            if (state->stack_guard_pages[stack]) {
                kfree(state->stack_guard_pages[stack]);
                state->stack_guard_pages[stack] = NULL;
            }
        }
        
        if (state->fpu_state_buffer) {
            kfree(state->fpu_state_buffer);
            state->fpu_state_buffer = NULL;
        }
    }
    return ret;
}

/**
 * Cleanup context switching system
 */
void interrupt_context_switching_cleanup(void)
{
    int cpu, stack;
    
    if (!ctx_mgr.initialized) {
        return;
    }
    
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        struct cpu_context_state *state = &ctx_mgr.cpu_states[cpu];
        
        /* Free interrupt stacks */
        for (stack = 0; stack < 4; stack++) {
            if (state->interrupt_stacks[stack]) {
                kfree(state->interrupt_stacks[stack]);
                state->interrupt_stacks[stack] = NULL;
            }
            if (state->stack_guard_pages[stack]) {
                kfree(state->stack_guard_pages[stack]);
                state->stack_guard_pages[stack] = NULL;
            }
        }
        
        /* Free FPU state buffer */
        if (state->fpu_state_buffer) {
            kfree(state->fpu_state_buffer);
            state->fpu_state_buffer = NULL;
        }
        
        /* Free any remaining contexts in stack */
        for (uint32_t level = 0; level < state->context_stack_depth; level++) {
            if (state->context_stack[level]) {
                context_free_context(state->context_stack[level]);
                state->context_stack[level] = NULL;
            }
        }
        state->context_stack_depth = 0;
    }
    
    ctx_mgr.initialized = false;
    debug_printf("Interrupt context switching cleaned up\n");
}

/* ===========================
 * CAPABILITY DETECTION
 * =========================== */

/**
 * Detect CPU context switching capabilities
 */
static int context_detect_capabilities(void)
{
    uint32_t eax, ebx, ecx, edx;
    
    /* Check for XSAVE support */
    cpuid(1, &eax, &ebx, &ecx, &edx);
    if (ecx & (1 << 26)) {  /* XSAVE feature flag */
        ctx_mgr.capabilities.xsave_supported = true;
        
        /* Get XSAVE area size and features */
        cpuid(0xD, &eax, &ebx, &ecx, &edx);
        ctx_mgr.capabilities.xsave_area_size = ebx;
        
        /* Check for extended XSAVE features */
        cpuid(0xD, &eax, &ebx, &ecx, &edx);
        if (eax & (1 << 0)) ctx_mgr.capabilities.xsaveopt_supported = true;
        if (eax & (1 << 1)) ctx_mgr.capabilities.xsavec_supported = true;
        if (eax & (1 << 3)) ctx_mgr.capabilities.xsaves_supported = true;
        
        /* Get supported XCR0 features */
        cpuid(0xD, &eax, &ebx, &ecx, &edx);
        ctx_mgr.capabilities.supported_xcr0 = ((uint64_t)edx << 32) | eax;
    }
    
    debug_printf("Context capabilities: XSAVE=%s, area_size=%llu, XCR0=0x%llx\n",
                ctx_mgr.capabilities.xsave_supported ? "yes" : "no",
                ctx_mgr.capabilities.xsave_area_size,
                ctx_mgr.capabilities.supported_xcr0);
    
    return 0;
}

/* ===========================
 * STACK MANAGEMENT
 * =========================== */

/**
 * Setup interrupt stacks for a CPU
 */
static int context_setup_cpu_stacks(int cpu)
{
    struct cpu_context_state *state;
    int stack_level;
    size_t stack_size = ctx_mgr.config.default_stack_size;
    
    if (cpu >= NR_CPUS) {
        return -EINVAL;
    }
    
    state = &ctx_mgr.cpu_states[cpu];
    
    /* Allocate interrupt stacks for different privilege levels */
    for (stack_level = 0; stack_level < 4; stack_level++) {
        /* Allocate stack with guard pages */
        void *stack_memory = kmalloc(stack_size + 2 * PAGE_SIZE, GFP_KERNEL);
        if (!stack_memory) {
            debug_printf("Failed to allocate interrupt stack %d for CPU %d\n", stack_level, cpu);
            goto cleanup;
        }
        
        /* Setup guard pages (simplified - would use memory protection) */
        if (ctx_mgr.config.stack_protection) {
            state->stack_guard_pages[stack_level] = kmalloc(PAGE_SIZE, GFP_KERNEL);
            if (!state->stack_guard_pages[stack_level]) {
                kfree(stack_memory);
                goto cleanup;
            }
            memset(state->stack_guard_pages[stack_level], 0xCC, PAGE_SIZE); /* Guard pattern */
        }
        
        /* Stack pointer points to top (grows down) */
        state->interrupt_stacks[stack_level] = (void *)((uintptr_t)stack_memory + stack_size);
        state->stack_sizes[stack_level] = stack_size;
        
        debug_printf("CPU %d: Allocated interrupt stack %d: %p (size %zu)\n",
                    cpu, stack_level, state->interrupt_stacks[stack_level], stack_size);
    }
    
    state->current_stack = state->interrupt_stacks[PRIVILEGE_KERNEL];
    
    return 0;
    
cleanup:
    for (int cleanup_level = 0; cleanup_level < stack_level; cleanup_level++) {
        if (state->interrupt_stacks[cleanup_level]) {
            kfree((void *)((uintptr_t)state->interrupt_stacks[cleanup_level] - stack_size));
            state->interrupt_stacks[cleanup_level] = NULL;
        }
        if (state->stack_guard_pages[cleanup_level]) {
            kfree(state->stack_guard_pages[cleanup_level]);
            state->stack_guard_pages[cleanup_level] = NULL;
        }
    }
    return -ENOMEM;
}

/**
 * Setup extended state handling for a CPU
 */
static void context_setup_extended_state(int cpu)
{
    struct cpu_context_state *state;
    
    if (cpu >= NR_CPUS) {
        return;
    }
    
    state = &ctx_mgr.cpu_states[cpu];
    
    /* Allocate FPU state buffer if extended state is supported */
    if (ctx_mgr.capabilities.xsave_supported) {
        size_t buffer_size = ctx_mgr.capabilities.xsave_area_size;
        if (buffer_size == 0) {
            buffer_size = sizeof(struct extended_state);
        }
        
        state->fpu_state_buffer = (struct extended_state *)kmalloc(buffer_size, GFP_KERNEL);
        if (state->fpu_state_buffer) {
            memset(state->fpu_state_buffer, 0, buffer_size);
            state->extended_state_enabled = true;
            state->supported_features = ctx_mgr.capabilities.supported_xcr0;
            
            debug_printf("CPU %d: Extended state buffer allocated (%zu bytes)\n", cpu, buffer_size);
        } else {
            debug_printf("CPU %d: Failed to allocate extended state buffer\n", cpu);
            state->extended_state_enabled = false;
        }
    }
}

/* ===========================
 * CONTEXT ALLOCATION
 * =========================== */

/**
 * Allocate a complete interrupt context
 */
static struct complete_interrupt_context *context_allocate_context(void)
{
    struct complete_interrupt_context *ctx;
    static uint64_t next_context_id = 1;
    
    ctx = (struct complete_interrupt_context *)kmalloc(sizeof(*ctx), GFP_ATOMIC);
    if (!ctx) {
        return NULL;
    }
    
    memset(ctx, 0, sizeof(*ctx));
    
    /* Allocate extended state if needed */
    if (ctx_mgr.capabilities.xsave_supported) {
        size_t ext_size = ctx_mgr.capabilities.xsave_area_size;
        if (ext_size == 0) {
            ext_size = sizeof(struct extended_state);
        }
        
        ctx->ext_state = (struct extended_state *)kmalloc(ext_size, GFP_ATOMIC);
        if (ctx->ext_state) {
            memset(ctx->ext_state, 0, ext_size);
            ctx->ext_state_size = ext_size;
        }
    }
    
    /* Initialize context metadata */
    ctx->context_id = atomic64_inc_return((atomic64_t *)&next_context_id);
    ctx->switch_timestamp = get_system_time_ns();
    ctx->context_valid = true;
    
    return ctx;
}

/**
 * Free a complete interrupt context
 */
static void context_free_context(struct complete_interrupt_context *ctx)
{
    if (!ctx) {
        return;
    }
    
    if (ctx->ext_state) {
        kfree(ctx->ext_state);
    }
    
    kfree(ctx);
}

/* ===========================
 * CONTEXT SWITCHING
 * =========================== */

/**
 * Enter interrupt context with full context switching
 */
int interrupt_context_enter(int vector, struct interrupt_context *int_ctx, privilege_level_t from_privilege)
{
    struct cpu_context_state *state;
    struct complete_interrupt_context *ctx;
    uint64_t start_time;
    int cpu;
    
    if (!ctx_mgr.initialized) {
        return -ENODEV;
    }
    
    cpu = smp_get_current_cpu();
    state = &ctx_mgr.cpu_states[cpu];
    start_time = get_system_time_ns();
    
    /* Check nesting level */
    if (state->context_stack_depth >= MAX_INTERRUPT_NESTING_DEPTH) {
        debug_printf("Maximum interrupt nesting depth reached on CPU %d\n", cpu);
        return -EOVERFLOW;
    }
    
    /* Allocate new context */
    ctx = context_allocate_context();
    if (!ctx) {
        return -ENOMEM;
    }
    
    /* Copy basic interrupt context */
    memcpy(&ctx->basic, int_ctx, sizeof(struct interrupt_context));
    
    /* Setup context information */
    ctx->previous_privilege = from_privilege;
    ctx->current_privilege = PRIVILEGE_KERNEL;
    ctx->nesting_level = state->context_stack_depth;
    
    /* Set parent context if nested */
    if (state->context_stack_depth > 0) {
        ctx->parent_context = state->context_stack[state->context_stack_depth - 1];
    }
    
    /* Save extended state if needed */
    if (state->extended_state_enabled && 
        (ctx_mgr.config.default_mode >= CONTEXT_SWITCH_FULL || 
         !ctx_mgr.config.lazy_fpu_switching)) {
        context_save_extended_state(ctx);
    }
    
    /* Switch to appropriate interrupt stack if privilege level changed */
    if (from_privilege == PRIVILEGE_USER) {
        ctx->user_stack_ptr = (void *)FRAME_STACK_PTR(int_ctx->frame);
        ctx->kernel_stack_ptr = state->interrupt_stacks[PRIVILEGE_KERNEL];

        /* Switch to kernel stack */
        state->current_stack = ctx->kernel_stack_ptr;
        switch_to_kernel_stack_asm(ctx->kernel_stack_ptr);

        state->stats.user_to_kernel++;
    } else {
        ctx->kernel_stack_ptr = (void *)FRAME_STACK_PTR(int_ctx->frame);
        state->stats.nested_switches++;
    }
    
    ctx->interrupt_stack_ptr = state->current_stack;
    
    /* Calculate context checksum for validation */
    ctx->context_checksum = context_calculate_checksum(ctx);
    
    /* Push context onto stack */
    state->context_stack[state->context_stack_depth] = ctx;
    state->context_stack_depth++;
    
    /* Update statistics */
    uint64_t switch_time = get_system_time_ns() - start_time;
    context_update_statistics(cpu, switch_time, from_privilege, PRIVILEGE_KERNEL);
    
    /* Update global statistics */
    if (state->context_stack_depth > ctx_mgr.global_stats.peak_nesting_level) {
        ctx_mgr.global_stats.peak_nesting_level = state->context_stack_depth;
    }
    
    return 0;
}

/**
 * Exit interrupt context with full context restoration
 */
int interrupt_context_exit(int vector)
{
    struct cpu_context_state *state;
    struct complete_interrupt_context *ctx;
    uint64_t start_time;
    int cpu;
    
    if (!ctx_mgr.initialized) {
        return -ENODEV;
    }
    
    cpu = smp_get_current_cpu();
    state = &ctx_mgr.cpu_states[cpu];
    start_time = get_system_time_ns();
    
    /* Check if we have contexts to restore */
    if (state->context_stack_depth == 0) {
        debug_printf("No interrupt context to exit on CPU %d\n", cpu);
        return -EINVAL;
    }
    
    /* Get current context */
    ctx = state->context_stack[state->context_stack_depth - 1];
    
    /* Validate context integrity */
    if (ctx_mgr.config.context_validation && !context_validate_context(ctx)) {
        debug_printf("Context corruption detected on CPU %d, vector %d\n", cpu, vector);
        state->stats.context_corruptions++;
        ctx_mgr.global_stats.context_corruptions++;
        /* Continue with restoration anyway to prevent system hang */
    }
    
    /* Restore extended state if it was saved */
    if (ctx->ext_state) {
        context_restore_extended_state(ctx);
    }
    
    /* Restore stack if privilege level changed */
    if (ctx->previous_privilege == PRIVILEGE_USER) {
        /* Switch back to user stack */
        switch_to_user_stack_asm(ctx->user_stack_ptr);
        state->current_stack = ctx->user_stack_ptr;
        state->stats.kernel_to_user++;
    } else {
        /* Restore kernel stack */
        state->current_stack = ctx->kernel_stack_ptr;
    }
    
    /* Pop context from stack */
    state->context_stack_depth--;
    state->context_stack[state->context_stack_depth] = NULL;
    
    /* Update statistics */
    uint64_t switch_time = get_system_time_ns() - start_time;
    context_update_statistics(cpu, switch_time, PRIVILEGE_KERNEL, ctx->previous_privilege);
    
    /* Free context */
    context_free_context(ctx);
    
    return 0;
}

/**
 * Switch to interrupt stack for current privilege level
 */
void *interrupt_context_switch_stack(privilege_level_t privilege)
{
    struct cpu_context_state *state;
    void *old_stack;
    int cpu;
    
    if (!ctx_mgr.initialized || privilege > 3) {
        return NULL;
    }
    
    cpu = smp_get_current_cpu();
    state = &ctx_mgr.cpu_states[cpu];
    
    old_stack = state->current_stack;
    state->current_stack = state->interrupt_stacks[privilege];
    
    return old_stack;
}

/* ===========================
 * EXTENDED STATE HANDLING
 * =========================== */

/**
 * Save extended CPU state (FPU/SSE/AVX)
 */
static void context_save_extended_state(struct complete_interrupt_context *ctx)
{
    int cpu = smp_get_current_cpu();
    struct cpu_context_state *state = &ctx_mgr.cpu_states[cpu];
    
    if (!ctx->ext_state || !state->extended_state_enabled) {
        return;
    }
    
    /* Determine what state needs to be saved */
    ctx->ext_state_flags = 0;
    
    if (ctx_mgr.capabilities.supported_xcr0 & (1ULL << 0)) {  /* x87 FPU */
        ctx->ext_state_flags |= STATE_FLAG_FPU;
    }
    if (ctx_mgr.capabilities.supported_xcr0 & (1ULL << 1)) {  /* SSE */
        ctx->ext_state_flags |= STATE_FLAG_SSE;
    }
    if (ctx_mgr.capabilities.supported_xcr0 & (1ULL << 2)) {  /* AVX */
        ctx->ext_state_flags |= STATE_FLAG_AVX;
    }
    
    /* Use XSAVE if supported, otherwise manual save */
    if (ctx_mgr.capabilities.xsave_supported) {
        uint64_t mask = ctx_mgr.capabilities.supported_xcr0;
        
        if (ctx_mgr.capabilities.xsaves_supported) {
            /* Use XSAVES (most efficient) */
            asm volatile("xsaves %0" : "=m"(*ctx->ext_state) : "a"((uint32_t)mask), "d"((uint32_t)(mask >> 32)) : "memory");
        } else if (ctx_mgr.capabilities.xsavec_supported) {
            /* Use XSAVEC (compacted format) */
            asm volatile("xsavec %0" : "=m"(*ctx->ext_state) : "a"((uint32_t)mask), "d"((uint32_t)(mask >> 32)) : "memory");
        } else {
            /* Use standard XSAVE */
            asm volatile("xsave %0" : "=m"(*ctx->ext_state) : "a"((uint32_t)mask), "d"((uint32_t)(mask >> 32)) : "memory");
        }
    } else {
        /* Manual save for older processors */
        if (ctx->ext_state_flags & STATE_FLAG_FPU) {
            asm volatile("fnsave %0; fwait" : "=m"(ctx->ext_state->fpu) :: "memory");
        }
        if (ctx->ext_state_flags & STATE_FLAG_SSE) {
            asm volatile("stmxcsr %0" : "=m"(ctx->ext_state->sse.mxcsr) :: "memory");
            /* Save XMM registers manually if needed */
        }
    }
    
    state->stats.fpu_saves++;
}

/**
 * Restore extended CPU state (FPU/SSE/AVX)
 */
static void context_restore_extended_state(struct complete_interrupt_context *ctx)
{
    int cpu = smp_get_current_cpu();
    struct cpu_context_state *state = &ctx_mgr.cpu_states[cpu];
    
    if (!ctx->ext_state || !state->extended_state_enabled) {
        return;
    }
    
    /* Use XRSTOR if supported, otherwise manual restore */
    if (ctx_mgr.capabilities.xsave_supported) {
        uint64_t mask = ctx_mgr.capabilities.supported_xcr0;
        
        if (ctx_mgr.capabilities.xsaves_supported) {
            /* Use XRSTORS */
            asm volatile("xrstors %0" :: "m"(*ctx->ext_state), "a"((uint32_t)mask), "d"((uint32_t)(mask >> 32)) : "memory");
        } else {
            /* Use standard XRSTOR */
            asm volatile("xrstor %0" :: "m"(*ctx->ext_state), "a"((uint32_t)mask), "d"((uint32_t)(mask >> 32)) : "memory");
        }
    } else {
        /* Manual restore for older processors */
        if (ctx->ext_state_flags & STATE_FLAG_FPU) {
            asm volatile("frstor %0" :: "m"(ctx->ext_state->fpu) : "memory");
        }
        if (ctx->ext_state_flags & STATE_FLAG_SSE) {
            asm volatile("ldmxcsr %0" :: "m"(ctx->ext_state->sse.mxcsr) : "memory");
            /* Restore XMM registers manually if needed */
        }
    }
    
    state->stats.fpu_restores++;
}

/* ===========================
 * CONTEXT VALIDATION
 * =========================== */

/**
 * Calculate context checksum for integrity checking
 */
static uint32_t context_calculate_checksum(struct complete_interrupt_context *ctx)
{
    uint32_t checksum = 0;
    uint8_t *data = (uint8_t *)&ctx->basic;
    size_t size = sizeof(ctx->basic);
    
    /* Simple checksum calculation */
    for (size_t i = 0; i < size; i++) {
        checksum = ((checksum << 1) | (checksum >> 31)) ^ data[i];
    }
    
    /* Include context metadata */
    checksum ^= (uint32_t)ctx->context_id;
    checksum ^= (uint32_t)(ctx->switch_timestamp & 0xFFFFFFFF);
    checksum ^= ctx->ext_state_flags;
    
    return checksum;
}

/**
 * Validate context integrity
 */
static bool context_validate_context(struct complete_interrupt_context *ctx)
{
    uint32_t current_checksum, expected_checksum;
    
    if (!ctx || !ctx->context_valid) {
        return false;
    }
    
    expected_checksum = ctx->context_checksum;
    
    /* Temporarily clear checksum for calculation */
    ctx->context_checksum = 0;
    current_checksum = context_calculate_checksum(ctx);
    ctx->context_checksum = expected_checksum;
    
    if (current_checksum != expected_checksum) {
        debug_printf("Context checksum mismatch: expected 0x%x, got 0x%x\n",
                    expected_checksum, current_checksum);
        return false;
    }
    
    /* Additional validation checks */
    if (ctx->nesting_level > MAX_INTERRUPT_NESTING_DEPTH) {
        debug_printf("Invalid nesting level: %u\n", ctx->nesting_level);
        return false;
    }
    
    if (ctx->previous_privilege > 3 || ctx->current_privilege > 3) {
        debug_printf("Invalid privilege levels: prev=%d, curr=%d\n",
                    ctx->previous_privilege, ctx->current_privilege);
        return false;
    }
    
    return true;
}

/* ===========================
 * STATISTICS AND MONITORING
 * =========================== */

/**
 * Update context switching statistics
 */
static void context_update_statistics(int cpu, uint64_t switch_time, 
                                    privilege_level_t from, privilege_level_t to)
{
    struct cpu_context_state *state;
    
    if (cpu >= NR_CPUS) {
        return;
    }
    
    state = &ctx_mgr.cpu_states[cpu];
    
    state->stats.total_switches++;
    state->stats.total_switch_time_ns += switch_time;
    
    if (switch_time > state->stats.max_switch_time_ns) {
        state->stats.max_switch_time_ns = switch_time;
    }
    
    /* Update global statistics */
    ctx_mgr.global_stats.total_context_switches++;
    ctx_mgr.global_stats.total_switch_time_ns += switch_time;
}

/* ===========================
 * PUBLIC API
 * =========================== */

/**
 * Get current interrupt context
 */
struct complete_interrupt_context *interrupt_get_current_context(void)
{
    struct cpu_context_state *state;
    int cpu;
    
    if (!ctx_mgr.initialized) {
        return NULL;
    }
    
    cpu = smp_get_current_cpu();
    state = &ctx_mgr.cpu_states[cpu];
    
    if (state->context_stack_depth == 0) {
        return NULL;
    }
    
    return state->context_stack[state->context_stack_depth - 1];
}

/**
 * Get context nesting level
 */
uint32_t interrupt_get_context_nesting_level(void)
{
    struct cpu_context_state *state;
    int cpu;
    
    if (!ctx_mgr.initialized) {
        return 0;
    }
    
    cpu = smp_get_current_cpu();
    state = &ctx_mgr.cpu_states[cpu];
    
    return state->context_stack_depth;
}

/**
 * Configure context switching mode
 */
int interrupt_context_configure(context_switch_mode_t mode, bool lazy_fpu, bool stack_protection)
{
    if (!ctx_mgr.initialized) {
        return -ENODEV;
    }
    
    ctx_mgr.config.default_mode = mode;
    ctx_mgr.config.lazy_fpu_switching = lazy_fpu;
    ctx_mgr.config.stack_protection = stack_protection;
    
    /* Update per-CPU configuration */
    for (int cpu = 0; cpu < NR_CPUS; cpu++) {
        ctx_mgr.cpu_states[cpu].default_mode = mode;
        ctx_mgr.cpu_states[cpu].stack_protection_enabled = stack_protection;
    }
    
    debug_printf("Context switching configured: mode=%d, lazy_fpu=%s, stack_protection=%s\n",
                mode, lazy_fpu ? "yes" : "no", stack_protection ? "yes" : "no");
    
    return 0;
}

/**
 * Get context switching statistics
 */
void interrupt_context_get_stats(int cpu, struct cpu_context_state *stats)
{
    if (!ctx_mgr.initialized || cpu >= NR_CPUS || !stats) {
        return;
    }
    
    memcpy(stats, &ctx_mgr.cpu_states[cpu], sizeof(*stats));
}

/**
 * Dump context switching status
 */
void interrupt_context_dump_status(void)
{
    int cpu;
    uint64_t total_switches = 0, total_time = 0;
    
    if (!ctx_mgr.initialized) {
        debug_printf("Context switching not initialized\n");
        return;
    }
    
    debug_printf("=== Interrupt Context Switching Status ===\n");
    
    debug_printf("Configuration:\n");
    debug_printf("  Default mode: %d\n", ctx_mgr.config.default_mode);
    debug_printf("  Lazy FPU switching: %s\n", ctx_mgr.config.lazy_fpu_switching ? "enabled" : "disabled");
    debug_printf("  Stack protection: %s\n", ctx_mgr.config.stack_protection ? "enabled" : "disabled");
    debug_printf("  Context validation: %s\n", ctx_mgr.config.context_validation ? "enabled" : "disabled");
    debug_printf("  Default stack size: %zu bytes\n", ctx_mgr.config.default_stack_size);
    
    debug_printf("\nCapabilities:\n");
    debug_printf("  XSAVE supported: %s\n", ctx_mgr.capabilities.xsave_supported ? "yes" : "no");
    debug_printf("  XSAVEOPT: %s, XSAVEC: %s, XSAVES: %s\n",
                ctx_mgr.capabilities.xsaveopt_supported ? "yes" : "no",
                ctx_mgr.capabilities.xsavec_supported ? "yes" : "no",
                ctx_mgr.capabilities.xsaves_supported ? "yes" : "no");
    debug_printf("  XSAVE area size: %llu bytes\n", ctx_mgr.capabilities.xsave_area_size);
    debug_printf("  Supported XCR0: 0x%llx\n", ctx_mgr.capabilities.supported_xcr0);
    
    debug_printf("\nGlobal Statistics:\n");
    debug_printf("  Total context switches: %llu\n", ctx_mgr.global_stats.total_context_switches);
    debug_printf("  Total switch time: %llu ns\n", ctx_mgr.global_stats.total_switch_time_ns);
    debug_printf("  Privilege violations: %llu\n", ctx_mgr.global_stats.privilege_violations);
    debug_printf("  Stack overflows: %llu\n", ctx_mgr.global_stats.stack_overflows);
    debug_printf("  Context corruptions: %llu\n", ctx_mgr.global_stats.context_corruptions);
    debug_printf("  Peak nesting level: %u\n", ctx_mgr.global_stats.peak_nesting_level);
    
    debug_printf("\nPer-CPU Statistics:\n");
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        struct cpu_context_state *state = &ctx_mgr.cpu_states[cpu];
        
        total_switches += state->stats.total_switches;
        total_time += state->stats.total_switch_time_ns;
        
        if (state->stats.total_switches > 0) {
            uint64_t avg_time = state->stats.total_switch_time_ns / state->stats.total_switches;
            
            debug_printf("  CPU %d: %llu switches, avg=%llu ns, max=%llu ns\n",
                        cpu, state->stats.total_switches, avg_time, state->stats.max_switch_time_ns);
            debug_printf("    User->Kernel: %llu, Kernel->User: %llu, Nested: %llu\n",
                        state->stats.user_to_kernel, state->stats.kernel_to_user, state->stats.nested_switches);
            debug_printf("    FPU saves: %llu, FPU restores: %llu\n",
                        state->stats.fpu_saves, state->stats.fpu_restores);
            debug_printf("    Stack depth: %u, Corruptions: %llu, Overflows: %llu\n",
                        state->context_stack_depth, state->stats.context_corruptions, state->stack_overflow_count);
        }
    }
    
    if (total_switches > 0) {
        debug_printf("\nOverall: %llu switches, %llu ns average\n", 
                    total_switches, total_time / total_switches);
    }
}