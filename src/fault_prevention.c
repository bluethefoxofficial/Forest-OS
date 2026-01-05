#include "fault_prevention.h"
#include "interrupt_management.h"
#include "x86_64_ist_handling.h"
#include "memory.h"  // Changed from memory_management.h
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Architecture detection */
#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH_64BIT 1
    #define ARCH_32BIT 0
    typedef uint64_t reg_t;
#else
    #define ARCH_64BIT 0
    #define ARCH_32BIT 1
    typedef uint32_t reg_t;
#endif

#define FAULT_STACK_SIZE 8192
#define MAX_NESTED_FAULTS 3
#define MAX_FAULT_HISTORY 64
#define FAULT_GUARD_PATTERN 0xDEADBEEFCAFEBABE

typedef enum {
    FAULT_TYPE_DIVIDE_ERROR = 0,
    FAULT_TYPE_DEBUG = 1,
    FAULT_TYPE_BREAKPOINT = 3,
    FAULT_TYPE_OVERFLOW = 4,
    FAULT_TYPE_BOUNDS_RANGE = 5,
    FAULT_TYPE_INVALID_OPCODE = 6,
    FAULT_TYPE_DEVICE_NOT_AVAILABLE = 7,
    FAULT_TYPE_DOUBLE_FAULT = 8,
    FAULT_TYPE_INVALID_TSS = 10,
    FAULT_TYPE_SEGMENT_NOT_PRESENT = 11,
    FAULT_TYPE_STACK_SEGMENT_FAULT = 12,
    FAULT_TYPE_GENERAL_PROTECTION = 13,
    FAULT_TYPE_PAGE_FAULT = 14,
    FAULT_TYPE_X87_FPU_ERROR = 16,
    FAULT_TYPE_ALIGNMENT_CHECK = 17,
    FAULT_TYPE_MACHINE_CHECK = 18,
    FAULT_TYPE_SIMD_FLOATING_POINT = 19,
    FAULT_TYPE_VIRTUALIZATION = 20,
    FAULT_TYPE_CONTROL_PROTECTION = 21
} fault_type_t;

typedef struct {
    uint64_t rip;
    uint64_t rsp;
    uint64_t error_code;
    fault_type_t fault_type;
    uint64_t timestamp;
    uint32_t cpu_id;
    bool recoverable;
} fault_record_t;

typedef struct {
    uint8_t *stack_base;
    uint8_t *stack_top;
    size_t stack_size;
    uint64_t guard_pattern;
    bool in_use;
    uint32_t allocation_count;
} fault_stack_info_t;

typedef struct {
    fault_stack_info_t double_fault_stack;
    fault_stack_info_t nmi_stack;
    fault_stack_info_t machine_check_stack;
    
    fault_record_t fault_history[MAX_FAULT_HISTORY];
    size_t fault_history_index;
    uint32_t fault_count;
    
    uint32_t nested_fault_depth;
    bool double_fault_occurred;
    bool triple_fault_imminent;
    bool system_critical_error;
    
    fault_prevention_config_t config;
    bool initialized;
} fault_prevention_context_t;

static fault_prevention_context_t fault_ctx = {0};

static void* allocate_fault_stack(size_t size) {
    void *stack_memory = mm_allocate_pages((size + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!stack_memory) {
        return NULL;
    }
    
    memset(stack_memory, 0xCC, size);
    
    uint64_t *guard_area = (uint64_t*)((uint8_t*)stack_memory + size - sizeof(uint64_t));
    *guard_area = FAULT_GUARD_PATTERN;
    
    return stack_memory;
}

static bool check_stack_integrity(fault_stack_info_t *stack_info) {
    if (!stack_info || !stack_info->stack_base) {
        return false;
    }
    
    uint64_t *guard_area = (uint64_t*)(stack_info->stack_base + 
                                     stack_info->stack_size - sizeof(uint64_t));
    return *guard_area == FAULT_GUARD_PATTERN;
}

static void setup_fault_stacks(void) {
    fault_ctx.double_fault_stack.stack_size = FAULT_STACK_SIZE;
    fault_ctx.double_fault_stack.stack_base = allocate_fault_stack(FAULT_STACK_SIZE);
    fault_ctx.double_fault_stack.stack_top = 
        fault_ctx.double_fault_stack.stack_base + FAULT_STACK_SIZE - 16;
    fault_ctx.double_fault_stack.guard_pattern = FAULT_GUARD_PATTERN;
    
    fault_ctx.nmi_stack.stack_size = FAULT_STACK_SIZE;
    fault_ctx.nmi_stack.stack_base = allocate_fault_stack(FAULT_STACK_SIZE);
    fault_ctx.nmi_stack.stack_top = 
        fault_ctx.nmi_stack.stack_base + FAULT_STACK_SIZE - 16;
    fault_ctx.nmi_stack.guard_pattern = FAULT_GUARD_PATTERN;
    
    fault_ctx.machine_check_stack.stack_size = FAULT_STACK_SIZE;
    fault_ctx.machine_check_stack.stack_base = allocate_fault_stack(FAULT_STACK_SIZE);
    fault_ctx.machine_check_stack.stack_top = 
        fault_ctx.machine_check_stack.stack_base + FAULT_STACK_SIZE - 16;
    fault_ctx.machine_check_stack.guard_pattern = FAULT_GUARD_PATTERN;
}

static void record_fault(fault_type_t type, uint64_t rip, uint64_t rsp, 
                        uint64_t error_code, bool recoverable) {
    size_t index = fault_ctx.fault_history_index;
    fault_ctx.fault_history[index] = (fault_record_t){
        .fault_type = type,
        .rip = rip,
        .rsp = rsp,
        .error_code = error_code,
        .timestamp = rdtsc(),
        .cpu_id = get_current_cpu_id(),
        .recoverable = recoverable
    };
    
    fault_ctx.fault_history_index = (index + 1) % MAX_FAULT_HISTORY;
    fault_ctx.fault_count++;
}

static bool is_fault_rate_excessive(void) {
    if (fault_ctx.fault_count < 10) {
        return false;
    }
    
    uint64_t current_time = rdtsc();
    uint64_t time_window = current_time - (1000000000ULL); // 1 second
    
    size_t recent_faults = 0;
    for (size_t i = 0; i < MAX_FAULT_HISTORY; i++) {
        if (fault_ctx.fault_history[i].timestamp > time_window) {
            recent_faults++;
        }
    }
    
    return recent_faults > fault_ctx.config.max_faults_per_second;
}

static void handle_divide_error(interrupt_context_t *context) {
    record_fault(FAULT_TYPE_DIVIDE_ERROR, context->rip, context->rsp, 0, true);
    
    if (fault_ctx.config.handle_divide_by_zero) {
        context->rax = 0;
        context->rip += 2;
        return;
    }
    
    panic("Divide by zero error at RIP: 0x%lx", context->rip);
}

static void handle_invalid_opcode(interrupt_context_t *context) {
    record_fault(FAULT_TYPE_INVALID_OPCODE, context->rip, context->rsp, 0, false);
    
    if (fault_ctx.config.emulate_invalid_instructions) {
        uint8_t *instruction = (uint8_t*)context->rip;
        
        if (instruction[0] == 0x0F && instruction[1] == 0x0B) {
            context->rip += 2;
            return;
        }
        
        if (instruction[0] >= 0xD8 && instruction[0] <= 0xDF) {
            context->rip += 2;
            return;
        }
    }
    
    panic("Invalid opcode at RIP: 0x%lx", context->rip);
}

static void handle_general_protection_fault(interrupt_context_t *context) {
    record_fault(FAULT_TYPE_GENERAL_PROTECTION, context->rip, context->rsp, 
                context->error_code, false);
    
    fault_ctx.nested_fault_depth++;
    
    if (fault_ctx.nested_fault_depth >= MAX_NESTED_FAULTS) {
        fault_ctx.triple_fault_imminent = true;
        system_emergency_shutdown("Triple fault prevention triggered");
        return;
    }
    
    if (context->error_code == 0 && fault_ctx.config.attempt_gpf_recovery) {
        context->rip += 1;
        fault_ctx.nested_fault_depth--;
        return;
    }
    
    panic("General Protection Fault - Error Code: 0x%lx, RIP: 0x%lx", 
          context->error_code, context->rip);
}

static void fault_prevention_handle_double_fault(interrupt_context_t *context) {
    fault_ctx.double_fault_occurred = true;
    fault_ctx.nested_fault_depth = MAX_NESTED_FAULTS;
    
    record_fault(FAULT_TYPE_DOUBLE_FAULT, context->rip, context->rsp, 
                context->error_code, false);
    
    if (!check_stack_integrity(&fault_ctx.double_fault_stack)) {
        fault_ctx.system_critical_error = true;
        system_emergency_shutdown("Double fault stack corruption detected");
        return;
    }
    
    if (is_fault_rate_excessive()) {
        fault_ctx.triple_fault_imminent = true;
        system_emergency_shutdown("Excessive fault rate - system unstable");
        return;
    }
    
    if (fault_ctx.config.enable_double_fault_recovery) {
        fault_prevention_emergency_recovery();
        return;
    }
    
    system_emergency_shutdown("Double fault occurred - system halted");
}

static void fault_prevention_handle_page_fault(interrupt_context_t *context) {
    reg_t fault_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r" (fault_address));
    
    record_fault(FAULT_TYPE_PAGE_FAULT, context->rip, context->rsp, 
                context->error_code, true);
    
    bool present = (context->error_code & 1) != 0;
    bool write = (context->error_code & 2) != 0;
    bool user = (context->error_code & 4) != 0;
    bool reserved = (context->error_code & 8) != 0;
    bool instruction_fetch = (context->error_code & 16) != 0;
    
    if (reserved) {
        panic("Page fault due to reserved bit violation at 0x%lx", fault_address);
    }
    
    if (fault_ctx.config.handle_null_pointer_access && fault_address < 0x1000) {
        context->rax = 0;
        context->rip += instruction_fetch ? 1 : 4;
        return;
    }
    
    if (fault_ctx.config.auto_allocate_pages && !present && user) {
        if (mm_handle_page_fault(fault_address, write, user)) {
            return;
        }
    }
    
    panic("Page fault at 0x%lx - RIP: 0x%lx, Error: 0x%lx", 
          fault_address, context->rip, context->error_code);
}

static void handle_machine_check(interrupt_context_t *context) {
    record_fault(FAULT_TYPE_MACHINE_CHECK, context->rip, context->rsp, 0, false);
    
    uint64_t mcg_status;
    __asm__ volatile ("rdmsr" : "=A" (mcg_status) : "c" (0x17A));
    
    if (mcg_status & (1ULL << 2)) {
        fault_ctx.system_critical_error = true;
    }
    
    if (fault_ctx.config.enable_machine_check_recovery && !(mcg_status & (1ULL << 2))) {
        __asm__ volatile ("wrmsr" : : "c" (0x17A), "A" (0));
        return;
    }
    
    system_emergency_shutdown("Machine Check Exception - Hardware Error");
}

fault_prevention_error_t fault_prevention_init(const fault_prevention_config_t *config) {
    if (!config) {
        return FAULT_PREV_ERROR_INVALID_PARAMS;
    }
    
    memset(&fault_ctx, 0, sizeof(fault_ctx));
    fault_ctx.config = *config;
    
    setup_fault_stacks();
    
    if (!fault_ctx.double_fault_stack.stack_base || 
        !fault_ctx.nmi_stack.stack_base || 
        !fault_ctx.machine_check_stack.stack_base) {
        return FAULT_PREV_ERROR_STACK_ALLOCATION_FAILED;
    }
    
    interrupt_register_handler(0, handle_divide_error, NULL);
    interrupt_register_handler(6, handle_invalid_opcode, NULL);
    interrupt_register_handler(8, handle_double_fault, NULL);
    interrupt_register_handler(13, handle_general_protection_fault, NULL);
    interrupt_register_handler(14, handle_page_fault, NULL);
    interrupt_register_handler(18, handle_machine_check, NULL);
    
    x86_64_ist_configure_stack(IST_DOUBLE_FAULT, 
                              fault_ctx.double_fault_stack.stack_top);
    x86_64_ist_configure_stack(IST_NMI, 
                              fault_ctx.nmi_stack.stack_top);
    x86_64_ist_configure_stack(IST_MACHINE_CHECK, 
                              fault_ctx.machine_check_stack.stack_top);
    
    fault_ctx.initialized = true;
    return FAULT_PREV_SUCCESS;
}

fault_prevention_error_t fault_prevention_enable_protection(void) {
    if (!fault_ctx.initialized) {
        return FAULT_PREV_ERROR_NOT_INITIALIZED;
    }

    reg_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r" (cr0));
    cr0 |= (1 << 18);
    __asm__ volatile ("mov %0, %%cr0" : : "r" (cr0));

    reg_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r" (cr4));
    cr4 |= (1 << 6);
    __asm__ volatile ("mov %0, %%cr4" : : "r" (cr4));

    if (fault_ctx.config.enable_stack_protection) {
        cr4 |= (1 << 22);
        __asm__ volatile ("mov %0, %%cr4" : : "r" (cr4));
    }

    return FAULT_PREV_SUCCESS;
}

fault_prevention_error_t fault_prevention_emergency_recovery(void) {
    if (!fault_ctx.initialized) {
        return FAULT_PREV_ERROR_NOT_INITIALIZED;
    }

    fault_ctx.nested_fault_depth = 0;

    reg_t rflags;
#if ARCH_64BIT
    __asm__ volatile ("pushfq; popq %0" : "=r" (rflags));
    rflags &= ~(1 << 9);
    __asm__ volatile ("pushq %0; popfq" : : "r" (rflags));
#else
    __asm__ volatile ("pushfl; popl %0" : "=r" (rflags));
    rflags &= ~(1 << 9);
    __asm__ volatile ("pushl %0; popfl" : : "r" (rflags));
#endif

    __asm__ volatile ("cli");

    if (fault_ctx.config.reset_fpu_state) {
        __asm__ volatile ("fninit");

        reg_t cr0_fpu;
        __asm__ volatile ("mov %%cr0, %0" : "=r" (cr0_fpu));
        cr0_fpu |= (1 << 2);
        cr0_fpu &= ~(1 << 1);
        __asm__ volatile ("mov %0, %%cr0" : : "r" (cr0_fpu));
    }

    if (fault_ctx.config.clear_debug_registers) {
        reg_t zero = 0;
        __asm__ volatile ("mov %0, %%dr0" : : "r" (zero) : "memory");
        __asm__ volatile ("mov %0, %%dr1" : : "r" (zero) : "memory");
        __asm__ volatile ("mov %0, %%dr2" : : "r" (zero) : "memory");
        __asm__ volatile ("mov %0, %%dr3" : : "r" (zero) : "memory");
        __asm__ volatile ("mov %0, %%dr6" : : "r" (zero) : "memory");
        __asm__ volatile ("mov %0, %%dr7" : : "r" (zero) : "memory");
    }

    __asm__ volatile ("sti");

    return FAULT_PREV_SUCCESS;
}

fault_prevention_error_t fault_prevention_get_statistics(fault_statistics_t *stats) {
    if (!fault_ctx.initialized || !stats) {
        return FAULT_PREV_ERROR_INVALID_PARAMS;
    }
    
    memset(stats, 0, sizeof(fault_statistics_t));
    
    stats->total_faults = fault_ctx.fault_count;
    stats->double_faults = 0;
    stats->general_protection_faults = 0;
    stats->page_faults = 0;
    stats->invalid_opcode_faults = 0;
    stats->divide_error_faults = 0;
    stats->machine_check_exceptions = 0;
    
    for (size_t i = 0; i < MAX_FAULT_HISTORY && i < fault_ctx.fault_count; i++) {
        fault_record_t *record = &fault_ctx.fault_history[i];
        switch (record->fault_type) {
            case FAULT_TYPE_DOUBLE_FAULT:
                stats->double_faults++;
                break;
            case FAULT_TYPE_GENERAL_PROTECTION:
                stats->general_protection_faults++;
                break;
            case FAULT_TYPE_PAGE_FAULT:
                stats->page_faults++;
                break;
            case FAULT_TYPE_INVALID_OPCODE:
                stats->invalid_opcode_faults++;
                break;
            case FAULT_TYPE_DIVIDE_ERROR:
                stats->divide_error_faults++;
                break;
            case FAULT_TYPE_MACHINE_CHECK:
                stats->machine_check_exceptions++;
                break;
            default:
                break;
        }
    }
    
    stats->stack_overflows_prevented = 0;
    stats->recovery_attempts = 0;
    stats->system_critical_errors = fault_ctx.system_critical_error ? 1 : 0;
    
    return FAULT_PREV_SUCCESS;
}

fault_prevention_error_t fault_prevention_check_system_health(system_health_t *health) {
    if (!fault_ctx.initialized || !health) {
        return FAULT_PREV_ERROR_INVALID_PARAMS;
    }
    
    memset(health, 0, sizeof(system_health_t));
    
    health->system_stable = !fault_ctx.triple_fault_imminent && 
                           !fault_ctx.system_critical_error;
    
    health->double_fault_occurred = fault_ctx.double_fault_occurred;
    health->triple_fault_imminent = fault_ctx.triple_fault_imminent;
    health->excessive_fault_rate = is_fault_rate_excessive();
    
    health->stack_integrity_ok = 
        check_stack_integrity(&fault_ctx.double_fault_stack) &&
        check_stack_integrity(&fault_ctx.nmi_stack) &&
        check_stack_integrity(&fault_ctx.machine_check_stack);
    
    health->nested_fault_depth = fault_ctx.nested_fault_depth;
    health->fault_rate = fault_ctx.fault_count;
    
    if (health->nested_fault_depth >= MAX_NESTED_FAULTS - 1) {
        health->risk_level = RISK_CRITICAL;
    } else if (health->excessive_fault_rate || health->double_fault_occurred) {
        health->risk_level = RISK_HIGH;
    } else if (health->nested_fault_depth > 0) {
        health->risk_level = RISK_MEDIUM;
    } else {
        health->risk_level = RISK_LOW;
    }
    
    return FAULT_PREV_SUCCESS;
}

bool fault_prevention_is_initialized(void) {
    return fault_ctx.initialized;
}

bool fault_prevention_is_system_stable(void) {
    return fault_ctx.initialized && 
           !fault_ctx.triple_fault_imminent && 
           !fault_ctx.system_critical_error &&
           fault_ctx.nested_fault_depth < MAX_NESTED_FAULTS;
}

void fault_prevention_reset_counters(void) {
    if (!fault_ctx.initialized) {
        return;
    }
    
    memset(fault_ctx.fault_history, 0, sizeof(fault_ctx.fault_history));
    fault_ctx.fault_history_index = 0;
    fault_ctx.fault_count = 0;
    fault_ctx.nested_fault_depth = 0;
    fault_ctx.double_fault_occurred = false;
}

void system_emergency_shutdown(const char *reason) {
    __asm__ volatile ("cli");
    
    if (fault_ctx.config.emergency_reboot) {
        __asm__ volatile ("mov $0x64, %al; out %al, $0x64");
        
        outb(0xCF9, 0x02);
        outb(0xCF9, 0x06);
        
        while (1) {
            __asm__ volatile ("hlt");
        }
    } else {
        while (1) {
            __asm__ volatile ("cli; hlt");
        }
    }
}