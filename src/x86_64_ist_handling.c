/*
 * x86_64_ist_handling.c - x86-64 Long Mode Interrupt Handling with IST Support for Forest OS
 * 
 * This module provides:
 * - x86-64 specific interrupt handling optimizations
 * - Interrupt Stack Table (IST) setup and management
 * - Critical interrupt isolation using separate stacks
 * - Enhanced security for exception handling
 * - Stack corruption protection mechanisms
 * - Integration with TSS (Task State Segment)
 * 
 * The IST mechanism in x86-64 provides separate stacks for critical
 * interrupts, preventing stack corruption issues and enhancing system
 * security and stability.
 * 
 * Copyright (c) 2024 Forest OS Project
 */

#include "include/interrupt.h"
#include "include/memory.h"
#include "include/smp.h"
#include "include/debug.h"
#include "include/time.h"
#include "include/string.h"
#include "include/debuglog.h"

#define debug_printf debuglog_printf

/* Simplified stubs to allow 64-bit builds to compile while IST support is unfinished */
int ist_init_global(void) { return 0; }
int ist_init_cpu(uint32_t cpu_id) { (void)cpu_id; return 0; }
void ist_cleanup_cpu(uint32_t cpu_id) { (void)cpu_id; }
int ist_allocate_stacks(uint32_t cpu_id) { (void)cpu_id; return 0; }
void ist_free_stacks(uint32_t cpu_id) { (void)cpu_id; }
int ist_setup_tss_stacks(uint32_t cpu_id) { (void)cpu_id; return 0; }
uintptr_t ist_get_stack_top(uint32_t cpu_id, uint8_t ist_index) { (void)cpu_id; (void)ist_index; return 0; }
void *ist_get_stack_base(uint32_t cpu_id, uint8_t ist_index) { (void)cpu_id; (void)ist_index; return NULL; }
size_t ist_get_stack_size(void) { return 0; }
void ist_double_fault_handler(void) {}
void ist_nmi_handler(void) {}
void ist_machine_check_handler(void) {}
void ist_debug_handler(void) {}
int ist_configure_idt_entry(uint8_t vector, uint8_t ist_index) { (void)vector; (void)ist_index; return 0; }
int ist_set_handler_stack(uint8_t vector, uint8_t ist_index) { (void)vector; (void)ist_index; return 0; }
bool ist_is_initialized(uint32_t cpu_id) { (void)cpu_id; return false; }
void ist_dump_stacks(uint32_t cpu_id) { (void)cpu_id; }
uint32_t ist_get_current_cpu(void) { return 0; }
bool ist_check_stack_overflow(uint32_t cpu_id, uint8_t ist_index) { (void)cpu_id; (void)ist_index; return false; }
void ist_install_guard_pages(uint32_t cpu_id) { (void)cpu_id; }
int ist_update_tss(uint32_t cpu_id) { (void)cpu_id; return 0; }
void ist_load_tss(uint32_t cpu_id) { (void)cpu_id; }
#ifdef CONFIG_SMP
int ist_init_all_cpus(void) { return 0; }
void ist_cleanup_all_cpus(void) {}
#endif

#if 0

#if ARCH_64BIT

/* IST Stack Types */
typedef enum {
    IST_DOUBLE_FAULT = 1,       /* IST1: Double fault handler */
    IST_NMI = 2,                /* IST2: Non-maskable interrupt */
    IST_MACHINE_CHECK = 3,      /* IST3: Machine check exception */
    IST_DEBUG = 4,              /* IST4: Debug exception */
    IST_CRITICAL = 5,           /* IST5: Critical interrupts */
    IST_SECURITY = 6,           /* IST6: Security exceptions */
    IST_RESERVED = 7            /* IST7: Reserved for future use */
} ist_stack_type_t;

/* IST Stack Configuration */
#define IST_STACK_SIZE          (16 * 1024)    /* 16KB per IST stack */
#define IST_STACK_ALIGNMENT     16              /* Stack alignment requirement */
#define IST_STACKS_PER_CPU      7               /* Number of IST stacks per CPU */

/* x86-64 TSS Structure */
struct x86_64_tss {
    uint32_t reserved1;         /* 0x00: Reserved */
    uint64_t rsp0;              /* 0x04: Ring 0 stack pointer */
    uint64_t rsp1;              /* 0x0C: Ring 1 stack pointer */
    uint64_t rsp2;              /* 0x14: Ring 2 stack pointer */
    uint64_t reserved2;         /* 0x1C: Reserved */
    uint64_t ist1;              /* 0x24: IST Stack 1 */
    uint64_t ist2;              /* 0x2C: IST Stack 2 */
    uint64_t ist3;              /* 0x34: IST Stack 3 */
    uint64_t ist4;              /* 0x3C: IST Stack 4 */
    uint64_t ist5;              /* 0x44: IST Stack 5 */
    uint64_t ist6;              /* 0x4C: IST Stack 6 */
    uint64_t ist7;              /* 0x54: IST Stack 7 */
    uint64_t reserved3;         /* 0x5C: Reserved */
    uint16_t reserved4;         /* 0x64: Reserved */
    uint16_t iopb_offset;       /* 0x66: I/O permission bitmap offset */
} __attribute__((packed));

/* Per-CPU IST Information */
struct cpu_ist_info {
    struct x86_64_tss *tss;             /* Task State Segment */
    void *ist_stacks[IST_STACKS_PER_CPU]; /* IST stack pointers */
    uint64_t stack_sizes[IST_STACKS_PER_CPU]; /* Stack sizes */
    bool stacks_allocated;              /* Stacks allocation status */
    
    /* Stack usage tracking */
    uint64_t stack_usage_max[IST_STACKS_PER_CPU];
    uint64_t stack_usage_current[IST_STACKS_PER_CPU];
    uint64_t stack_overflow_count[IST_STACKS_PER_CPU];
    
    /* Exception statistics */
    struct {
        uint64_t double_faults;
        uint64_t nmi_count;
        uint64_t machine_checks;
        uint64_t debug_exceptions;
        uint64_t critical_interrupts;
        uint64_t security_exceptions;
        uint64_t stack_switches;
        uint64_t ist_violations;
    } stats;
    
    /* Security and integrity */
    uint64_t stack_canaries[IST_STACKS_PER_CPU];
    bool stack_integrity_check;
    uint32_t tss_selector;              /* GDT selector for TSS */
} __attribute__((aligned(64)));

/* Global IST Management */
struct ist_manager {
    struct cpu_ist_info cpu_info[NR_CPUS];
    
    /* Global configuration */
    struct {
        bool enabled;
        bool stack_protection;
        bool canary_checks;
        bool usage_tracking;
        uint32_t stack_size;
    } config;
    
    /* System statistics */
    struct {
        uint64_t total_ist_interrupts;
        uint64_t total_stack_switches;
        uint64_t stack_overflow_total;
        uint64_t integrity_failures;
        uint32_t peak_stack_usage;
    } global_stats;
    
    bool initialized;
    spinlock_t lock;
};

static struct ist_manager ist_mgr = {0};

/* Stack canary pattern for integrity checking */
#define IST_STACK_CANARY        0xDEADBEEFCAFEBABEULL
#define IST_GUARD_PATTERN       0x5555555555555555ULL

/* Forward declarations */
static int ist_allocate_stacks_for_cpu(int cpu);
static void ist_free_stacks_for_cpu(int cpu);
static int ist_setup_tss_for_cpu(int cpu);
static void ist_configure_idt_entries(void);
static bool ist_check_stack_integrity(int cpu, int ist_index);
static void ist_update_stack_usage(int cpu, int ist_index);
static void ist_handle_stack_overflow(int cpu, int ist_index);
static uint16_t ist_allocate_tss_selector(int cpu);

/* Critical interrupt handlers with IST */
extern void double_fault_ist_handler(void);
extern void nmi_ist_handler(void);
extern void machine_check_ist_handler(void);
extern void debug_ist_handler(void);

/* ===========================
 * INITIALIZATION
 * =========================== */

/**
 * Initialize x86-64 IST interrupt handling
 */
int x86_64_ist_init(void)
{
    int cpu, ret;
    
    if (ist_mgr.initialized) {
        return 0;
    }
    
#ifndef ARCH_64BIT
    debug_printf("IST support requires x86-64 architecture\n");
    return -ENOTSUP;
#endif
    
    memset(&ist_mgr, 0, sizeof(ist_mgr));
    spinlock_init(&ist_mgr.lock, "ist_manager");
    
    /* Set default configuration */
    ist_mgr.config.enabled = true;
    ist_mgr.config.stack_protection = true;
    ist_mgr.config.canary_checks = true;
    ist_mgr.config.usage_tracking = true;
    ist_mgr.config.stack_size = IST_STACK_SIZE;
    
    debug_printf("Initializing x86-64 IST interrupt handling\n");
    
    /* Initialize IST for all CPUs */
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        ret = ist_allocate_stacks_for_cpu(cpu);
        if (ret < 0) {
            debug_printf("Failed to allocate IST stacks for CPU %d: %d\n", cpu, ret);
            goto cleanup;
        }
        
        ret = ist_setup_tss_for_cpu(cpu);
        if (ret < 0) {
            debug_printf("Failed to setup TSS for CPU %d: %d\n", cpu, ret);
            goto cleanup;
        }
    }
    
    /* Configure IDT entries to use IST */
    ist_configure_idt_entries();
    
    ist_mgr.initialized = true;
    
    debug_printf("x86-64 IST interrupt handling initialized\n");
    debug_printf("IST stacks per CPU: %d (%u bytes each)\n", 
                IST_STACKS_PER_CPU, ist_mgr.config.stack_size);
    debug_printf("Stack protection: %s, Canary checks: %s\n",
                ist_mgr.config.stack_protection ? "enabled" : "disabled",
                ist_mgr.config.canary_checks ? "enabled" : "disabled");
    
    return 0;
    
cleanup:
    for (int cleanup_cpu = 0; cleanup_cpu < cpu; cleanup_cpu++) {
        ist_free_stacks_for_cpu(cleanup_cpu);
    }
    return ret;
}

/**
 * Cleanup x86-64 IST interrupt handling
 */
void x86_64_ist_cleanup(void)
{
    int cpu;
    
    if (!ist_mgr.initialized) {
        return;
    }
    
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        ist_free_stacks_for_cpu(cpu);
    }
    
    ist_mgr.initialized = false;
    debug_printf("x86-64 IST interrupt handling cleaned up\n");
}

/* ===========================
 * STACK MANAGEMENT
 * =========================== */

/**
 * Allocate IST stacks for a specific CPU
 */
static int ist_allocate_stacks_for_cpu(int cpu)
{
    struct cpu_ist_info *cpu_info;
    int i;
    void *stack_memory;
    uint64_t *canary_ptr;
    
    if (cpu >= NR_CPUS) {
        return -EINVAL;
    }
    
    cpu_info = &ist_mgr.cpu_info[cpu];
    
    if (cpu_info->stacks_allocated) {
        return 0; /* Already allocated */
    }
    
    /* Allocate stacks with guard pages if possible */
    for (i = 0; i < IST_STACKS_PER_CPU; i++) {
        /* Allocate stack memory with extra space for guards */
        size_t total_size = ist_mgr.config.stack_size + 2 * PAGE_SIZE;
        
        stack_memory = kmalloc(total_size, GFP_KERNEL);
        if (!stack_memory) {
            debug_printf("Failed to allocate IST stack %d for CPU %d\n", i, cpu);
            goto cleanup_partial;
        }
        
        /* Align stack pointer */
        uintptr_t stack_addr = (uintptr_t)stack_memory;
        stack_addr = (stack_addr + IST_STACK_ALIGNMENT - 1) & ~(IST_STACK_ALIGNMENT - 1);
        
        /* Set up guard pages (if memory management supports it) */
        if (ist_mgr.config.stack_protection) {
            /* Place canaries at stack boundaries */
            canary_ptr = (uint64_t *)(stack_addr);
            *canary_ptr = IST_STACK_CANARY;
            
            canary_ptr = (uint64_t *)(stack_addr + ist_mgr.config.stack_size - sizeof(uint64_t));
            *canary_ptr = IST_STACK_CANARY;
        }
        
        /* Stack grows down, so pointer is at the top */
        cpu_info->ist_stacks[i] = (void *)(stack_addr + ist_mgr.config.stack_size - sizeof(uint64_t));
        cpu_info->stack_sizes[i] = ist_mgr.config.stack_size;
        cpu_info->stack_canaries[i] = IST_STACK_CANARY;
        
        debug_printf("Allocated IST stack %d for CPU %d: %p (size %llu)\n",
                    i + 1, cpu, cpu_info->ist_stacks[i], cpu_info->stack_sizes[i]);
    }
    
    cpu_info->stacks_allocated = true;
    cpu_info->stack_integrity_check = ist_mgr.config.canary_checks;
    
    return 0;
    
cleanup_partial:
    /* Free any stacks that were successfully allocated */
    for (int j = 0; j < i; j++) {
        if (cpu_info->ist_stacks[j]) {
            kfree(cpu_info->ist_stacks[j]);
            cpu_info->ist_stacks[j] = NULL;
        }
    }
    return -ENOMEM;
}

/**
 * Free IST stacks for a specific CPU
 */
static void ist_free_stacks_for_cpu(int cpu)
{
    struct cpu_ist_info *cpu_info;
    int i;
    
    if (cpu >= NR_CPUS) {
        return;
    }
    
    cpu_info = &ist_mgr.cpu_info[cpu];
    
    if (!cpu_info->stacks_allocated) {
        return;
    }
    
    for (i = 0; i < IST_STACKS_PER_CPU; i++) {
        if (cpu_info->ist_stacks[i]) {
            /* Check stack integrity before freeing */
            if (cpu_info->stack_integrity_check) {
                ist_check_stack_integrity(cpu, i);
            }
            
            /* Calculate base address for free */
            uintptr_t stack_base = (uintptr_t)cpu_info->ist_stacks[i] - 
                                  cpu_info->stack_sizes[i] + sizeof(uint64_t);
            
            kfree((void *)stack_base);
            cpu_info->ist_stacks[i] = NULL;
            cpu_info->stack_sizes[i] = 0;
        }
    }
    
    /* Free TSS if allocated */
    if (cpu_info->tss) {
        kfree(cpu_info->tss);
        cpu_info->tss = NULL;
    }
    
    cpu_info->stacks_allocated = false;
    
    debug_printf("Freed IST stacks for CPU %d\n", cpu);
}

/**
 * Setup TSS (Task State Segment) for a specific CPU
 */
static int ist_setup_tss_for_cpu(int cpu)
{
    struct cpu_ist_info *cpu_info;
    struct x86_64_tss *tss;
    
    if (cpu >= NR_CPUS) {
        return -EINVAL;
    }
    
    cpu_info = &ist_mgr.cpu_info[cpu];
    
    /* Allocate TSS */
    tss = (struct x86_64_tss *)kmalloc(sizeof(struct x86_64_tss), GFP_KERNEL);
    if (!tss) {
        return -ENOMEM;
    }
    
    memset(tss, 0, sizeof(struct x86_64_tss));
    
    /* Setup IST stack pointers in TSS */
    tss->ist1 = (uint64_t)cpu_info->ist_stacks[IST_DOUBLE_FAULT - 1];
    tss->ist2 = (uint64_t)cpu_info->ist_stacks[IST_NMI - 1];
    tss->ist3 = (uint64_t)cpu_info->ist_stacks[IST_MACHINE_CHECK - 1];
    tss->ist4 = (uint64_t)cpu_info->ist_stacks[IST_DEBUG - 1];
    tss->ist5 = (uint64_t)cpu_info->ist_stacks[IST_CRITICAL - 1];
    tss->ist6 = (uint64_t)cpu_info->ist_stacks[IST_SECURITY - 1];
    tss->ist7 = (uint64_t)cpu_info->ist_stacks[IST_RESERVED - 1];
    
    /* Set RSP0 for privilege level transitions */
    /* This would typically be set to kernel stack */
    tss->rsp0 = 0; /* Would be set during context switch */
    
    /* Configure I/O permission bitmap (no I/O permissions) */
    tss->iopb_offset = sizeof(struct x86_64_tss);
    
    cpu_info->tss = tss;
    
    /* Allocate TSS selector in GDT */
    cpu_info->tss_selector = ist_allocate_tss_selector(cpu);
    
    /* Load TSS for current CPU */
    if (cpu == smp_get_current_cpu()) {
        /* This would load the TSS using the LTR instruction */
        /* asm volatile("ltr %0" :: "r"(cpu_info->tss_selector)); */
    }
    
    debug_printf("Setup TSS for CPU %d: TSS=%p, selector=0x%x\n",
                cpu, tss, cpu_info->tss_selector);
    
    return 0;
}

/**
 * Configure IDT entries to use IST stacks
 */
static void ist_configure_idt_entries(void)
{
    /* Configure critical exception handlers to use IST stacks */
    
    /* Double Fault - IST1 */
    idt_set_gate_enhanced(EXCEPTION_DOUBLE_FAULT, 
                         (void *)double_fault_ist_handler,
                         0x08,  /* Kernel code segment */
                         IDT_GATE_INTERRUPT32 | IDT_PRESENT,
                         IST_DOUBLE_FAULT);
    
    /* NMI - IST2 */
    idt_set_gate_enhanced(EXCEPTION_NMI,
                         (void *)nmi_ist_handler,
                         0x08,
                         IDT_GATE_INTERRUPT32 | IDT_PRESENT,
                         IST_NMI);
    
    /* Machine Check - IST3 */
    idt_set_gate_enhanced(EXCEPTION_MACHINE_CHECK,
                         (void *)machine_check_ist_handler,
                         0x08,
                         IDT_GATE_INTERRUPT32 | IDT_PRESENT,
                         IST_MACHINE_CHECK);
    
    /* Debug Exception - IST4 */
    idt_set_gate_enhanced(EXCEPTION_DEBUG,
                         (void *)debug_ist_handler,
                         0x08,
                         IDT_GATE_TRAP32 | IDT_PRESENT,  /* Trap gate for debug */
                         IST_DEBUG);
    
    debug_printf("Configured IDT entries with IST stacks\n");
}

/* ===========================
 * INTERRUPT HANDLING
 * =========================== */

/**
 * IST-aware double fault handler
 */
void ist_double_fault_handler(struct interrupt_context *ctx)
{
    int cpu = smp_get_current_cpu();
    struct cpu_ist_info *cpu_info = &ist_mgr.cpu_info[cpu];
    
    /* Update statistics */
    cpu_info->stats.double_faults++;
    cpu_info->stats.stack_switches++;
    ist_mgr.global_stats.total_ist_interrupts++;
    ist_mgr.global_stats.total_stack_switches++;
    
    /* Check stack integrity */
    if (ist_mgr.config.canary_checks) {
        ist_check_stack_integrity(cpu, IST_DOUBLE_FAULT - 1);
    }
    
    /* Update stack usage tracking */
    if (ist_mgr.config.usage_tracking) {
        ist_update_stack_usage(cpu, IST_DOUBLE_FAULT - 1);
    }
    
    debug_printf("DOUBLE FAULT: CPU %d, RIP=0x%llx, RSP=0x%llx\n",
                cpu, ctx->frame.rip, ctx->frame.rsp);
    debug_printf("Using IST stack: %p\n", cpu_info->ist_stacks[IST_DOUBLE_FAULT - 1]);
    
    /* Call original double fault handler */
    handle_double_fault(ctx);
}

/**
 * IST-aware NMI handler
 */
void ist_nmi_handler(struct interrupt_context *ctx)
{
    int cpu = smp_get_current_cpu();
    struct cpu_ist_info *cpu_info = &ist_mgr.cpu_info[cpu];
    
    /* Update statistics */
    cpu_info->stats.nmi_count++;
    cpu_info->stats.stack_switches++;
    ist_mgr.global_stats.total_ist_interrupts++;
    ist_mgr.global_stats.total_stack_switches++;
    
    /* Check stack integrity */
    if (ist_mgr.config.canary_checks) {
        ist_check_stack_integrity(cpu, IST_NMI - 1);
    }
    
    /* Call NMI handler */
    nmi_handler_c(ctx);
}

/**
 * IST-aware machine check handler
 */
void ist_machine_check_handler(struct interrupt_context *ctx)
{
    int cpu = smp_get_current_cpu();
    struct cpu_ist_info *cpu_info = &ist_mgr.cpu_info[cpu];
    
    /* Update statistics */
    cpu_info->stats.machine_checks++;
    cpu_info->stats.stack_switches++;
    ist_mgr.global_stats.total_ist_interrupts++;
    ist_mgr.global_stats.total_stack_switches++;
    
    debug_printf("MACHINE CHECK: CPU %d, RIP=0x%llx\n", cpu, ctx->frame.rip);
    
    /* Machine check handling would go here */
    /* This is highly CPU-specific and would read MCE MSRs */
}

/**
 * IST-aware debug exception handler
 */
void ist_debug_handler(struct interrupt_context *ctx)
{
    int cpu = smp_get_current_cpu();
    struct cpu_ist_info *cpu_info = &ist_mgr.cpu_info[cpu];
    
    /* Update statistics */
    cpu_info->stats.debug_exceptions++;
    cpu_info->stats.stack_switches++;
    ist_mgr.global_stats.total_ist_interrupts++;
    ist_mgr.global_stats.total_stack_switches++;
    
    debug_printf("DEBUG EXCEPTION: CPU %d, RIP=0x%llx, DR6=0x%llx\n",
                cpu, ctx->frame.rip, read_dr6());
    
    /* Debug exception handling would go here */
}

/* ===========================
 * STACK INTEGRITY AND MONITORING
 * =========================== */

/**
 * Check IST stack integrity
 */
static bool ist_check_stack_integrity(int cpu, int ist_index)
{
    struct cpu_ist_info *cpu_info;
    uint64_t *canary_ptr;
    uintptr_t stack_base;
    bool integrity_ok = true;
    
    if (cpu >= NR_CPUS || ist_index >= IST_STACKS_PER_CPU) {
        return false;
    }
    
    cpu_info = &ist_mgr.cpu_info[cpu];
    
    if (!cpu_info->ist_stacks[ist_index]) {
        return false;
    }
    
    /* Check bottom canary */
    stack_base = (uintptr_t)cpu_info->ist_stacks[ist_index] - 
                 cpu_info->stack_sizes[ist_index] + sizeof(uint64_t);
    canary_ptr = (uint64_t *)stack_base;
    
    if (*canary_ptr != IST_STACK_CANARY) {
        debug_printf("IST INTEGRITY VIOLATION: CPU %d, IST %d, bottom canary corrupted (0x%llx)\n",
                    cpu, ist_index + 1, *canary_ptr);
        integrity_ok = false;
    }
    
    /* Check top canary */
    canary_ptr = (uint64_t *)((uintptr_t)cpu_info->ist_stacks[ist_index]);
    if (*canary_ptr != IST_STACK_CANARY) {
        debug_printf("IST INTEGRITY VIOLATION: CPU %d, IST %d, top canary corrupted (0x%llx)\n",
                    cpu, ist_index + 1, *canary_ptr);
        integrity_ok = false;
    }
    
    if (!integrity_ok) {
        cpu_info->stats.ist_violations++;
        ist_mgr.global_stats.integrity_failures++;
    }
    
    return integrity_ok;
}

/**
 * Update IST stack usage tracking
 */
static void ist_update_stack_usage(int cpu, int ist_index)
{
    struct cpu_ist_info *cpu_info;
    uint64_t current_rsp, stack_top, stack_usage;
    
    if (cpu >= NR_CPUS || ist_index >= IST_STACKS_PER_CPU) {
        return;
    }
    
    cpu_info = &ist_mgr.cpu_info[cpu];
    
    if (!cpu_info->ist_stacks[ist_index]) {
        return;
    }
    
    /* Get current stack pointer */
    asm volatile("movq %%rsp, %0" : "=r"(current_rsp));
    
    stack_top = (uint64_t)cpu_info->ist_stacks[ist_index];
    
    /* Check if we're actually on the IST stack */
    uint64_t stack_bottom = stack_top - cpu_info->stack_sizes[ist_index];
    
    if (current_rsp >= stack_bottom && current_rsp <= stack_top) {
        stack_usage = stack_top - current_rsp;
        
        cpu_info->stack_usage_current[ist_index] = stack_usage;
        
        if (stack_usage > cpu_info->stack_usage_max[ist_index]) {
            cpu_info->stack_usage_max[ist_index] = stack_usage;
        }
        
        if (stack_usage > ist_mgr.global_stats.peak_stack_usage) {
            ist_mgr.global_stats.peak_stack_usage = stack_usage;
        }
        
        /* Check for potential overflow */
        if (stack_usage > (cpu_info->stack_sizes[ist_index] * 90 / 100)) {
            debug_printf("IST STACK WARNING: CPU %d, IST %d, high usage: %llu/%llu bytes (90%%+)\n",
                        cpu, ist_index + 1, stack_usage, cpu_info->stack_sizes[ist_index]);
        }
        
        if (stack_usage >= cpu_info->stack_sizes[ist_index]) {
            ist_handle_stack_overflow(cpu, ist_index);
        }
    }
}

/**
 * Handle IST stack overflow
 */
static void ist_handle_stack_overflow(int cpu, int ist_index)
{
    struct cpu_ist_info *cpu_info;
    
    if (cpu >= NR_CPUS || ist_index >= IST_STACKS_PER_CPU) {
        return;
    }
    
    cpu_info = &ist_mgr.cpu_info[cpu];
    
    cpu_info->stack_overflow_count[ist_index]++;
    ist_mgr.global_stats.stack_overflow_total++;
    
    debug_printf("IST STACK OVERFLOW: CPU %d, IST %d (%s)\n", 
                cpu, ist_index + 1,
                ist_index == (IST_DOUBLE_FAULT - 1) ? "Double Fault" :
                ist_index == (IST_NMI - 1) ? "NMI" :
                ist_index == (IST_MACHINE_CHECK - 1) ? "Machine Check" :
                ist_index == (IST_DEBUG - 1) ? "Debug" : "Unknown");
    
    /* This is a critical error - could trigger panic */
    debug_printf("CRITICAL: IST stack overflow detected - system stability compromised\n");
}

/* ===========================
 * UTILITY FUNCTIONS
 * =========================== */

/**
 * Allocate TSS selector in GDT
 */
static uint16_t ist_allocate_tss_selector(int cpu)
{
    /* This would allocate a TSS descriptor in the GDT */
    /* For now, return a placeholder selector */
    return 0x40 + (cpu * 8);  /* Example: base 0x40, 8 bytes per entry */
}

/**
 * Read DR6 debug register
 */
static uint64_t read_dr6(void)
{
    uint64_t dr6;
    asm volatile("movq %%dr6, %0" : "=r"(dr6));
    return dr6;
}

/* ===========================
 * PUBLIC API
 * =========================== */

/**
 * Get IST information for a CPU
 */
int ist_get_cpu_info(int cpu, struct cpu_ist_info *info)
{
    if (!ist_mgr.initialized || cpu >= NR_CPUS || !info) {
        return -EINVAL;
    }
    
    memcpy(info, &ist_mgr.cpu_info[cpu], sizeof(*info));
    return 0;
}

/**
 * Get IST stack pointer for a specific stack
 */
void *ist_get_stack_pointer(int cpu, ist_stack_type_t stack_type)
{
    if (!ist_mgr.initialized || cpu >= NR_CPUS || 
        stack_type < 1 || stack_type > IST_STACKS_PER_CPU) {
        return NULL;
    }
    
    return ist_mgr.cpu_info[cpu].ist_stacks[stack_type - 1];
}

/**
 * Check if IST is enabled
 */
bool ist_is_enabled(void)
{
    return ist_mgr.initialized && ist_mgr.config.enabled;
}

/**
 * Configure IST stack protection
 */
int ist_configure_protection(bool stack_protection, bool canary_checks)
{
    if (!ist_mgr.initialized) {
        return -ENODEV;
    }
    
    ist_mgr.config.stack_protection = stack_protection;
    ist_mgr.config.canary_checks = canary_checks;
    
    /* Update per-CPU configuration */
    for (int cpu = 0; cpu < NR_CPUS; cpu++) {
        ist_mgr.cpu_info[cpu].stack_integrity_check = canary_checks;
    }
    
    debug_printf("IST protection configured: stack_protection=%s, canary_checks=%s\n",
                stack_protection ? "enabled" : "disabled",
                canary_checks ? "enabled" : "disabled");
    
    return 0;
}

/**
 * Dump IST status and statistics
 */
void ist_dump_status(void)
{
    int cpu, ist;
    struct cpu_ist_info *cpu_info;
    
    if (!ist_mgr.initialized) {
        debug_printf("IST not initialized\n");
        return;
    }
    
    debug_printf("=== x86-64 IST Status ===\n");
    debug_printf("Configuration:\n");
    debug_printf("  Enabled: %s\n", ist_mgr.config.enabled ? "yes" : "no");
    debug_printf("  Stack protection: %s\n", ist_mgr.config.stack_protection ? "yes" : "no");
    debug_printf("  Canary checks: %s\n", ist_mgr.config.canary_checks ? "yes" : "no");
    debug_printf("  Usage tracking: %s\n", ist_mgr.config.usage_tracking ? "yes" : "no");
    debug_printf("  Stack size: %u bytes\n", ist_mgr.config.stack_size);
    
    debug_printf("\nGlobal Statistics:\n");
    debug_printf("  Total IST interrupts: %llu\n", ist_mgr.global_stats.total_ist_interrupts);
    debug_printf("  Total stack switches: %llu\n", ist_mgr.global_stats.total_stack_switches);
    debug_printf("  Stack overflows: %llu\n", ist_mgr.global_stats.stack_overflow_total);
    debug_printf("  Integrity failures: %llu\n", ist_mgr.global_stats.integrity_failures);
    debug_printf("  Peak stack usage: %u bytes\n", ist_mgr.global_stats.peak_stack_usage);
    
    debug_printf("\nPer-CPU Information:\n");
    for (cpu = 0; cpu < NR_CPUS; cpu++) {
        cpu_info = &ist_mgr.cpu_info[cpu];
        
        if (!cpu_info->stacks_allocated) {
            continue;
        }
        
        debug_printf("CPU %d:\n", cpu);
        debug_printf("  TSS: %p (selector 0x%x)\n", cpu_info->tss, cpu_info->tss_selector);
        debug_printf("  Statistics:\n");
        debug_printf("    Double faults: %llu\n", cpu_info->stats.double_faults);
        debug_printf("    NMIs: %llu\n", cpu_info->stats.nmi_count);
        debug_printf("    Machine checks: %llu\n", cpu_info->stats.machine_checks);
        debug_printf("    Debug exceptions: %llu\n", cpu_info->stats.debug_exceptions);
        debug_printf("    IST violations: %llu\n", cpu_info->stats.ist_violations);
        
        debug_printf("  IST Stacks:\n");
        for (ist = 0; ist < IST_STACKS_PER_CPU; ist++) {
            if (cpu_info->ist_stacks[ist]) {
                debug_printf("    IST%d: %p (%llu bytes, max used: %llu)\n",
                            ist + 1, cpu_info->ist_stacks[ist],
                            cpu_info->stack_sizes[ist],
                            cpu_info->stack_usage_max[ist]);
            }
        }
    }
}

#else /* !ARCH_64BIT */

/* Stub functions for non-x86-64 architectures */
int x86_64_ist_init(void) { 
    debug_printf("IST support requires x86-64 architecture\n");
    return -ENOTSUP; 
}
void x86_64_ist_cleanup(void) {}
int ist_get_cpu_info(int cpu, void *info) { return -ENOTSUP; }
void *ist_get_stack_pointer(int cpu, int stack_type) { return NULL; }
bool ist_is_enabled(void) { return false; }
int ist_configure_protection(bool stack_protection, bool canary_checks) { return -ENOTSUP; }
void ist_dump_status(void) { debug_printf("IST not supported on this architecture\n"); }

#endif /* ARCH_64BIT */

#endif /* 0 */
