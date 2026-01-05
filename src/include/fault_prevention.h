#ifndef FAULT_PREVENTION_H
#define FAULT_PREVENTION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    FAULT_PREV_SUCCESS = 0,
    FAULT_PREV_ERROR_INVALID_PARAMS,
    FAULT_PREV_ERROR_NOT_INITIALIZED,
    FAULT_PREV_ERROR_STACK_ALLOCATION_FAILED,
    FAULT_PREV_ERROR_HANDLER_REGISTRATION_FAILED,
    FAULT_PREV_ERROR_PROTECTION_NOT_SUPPORTED,
    FAULT_PREV_ERROR_SYSTEM_UNSTABLE
} fault_prevention_error_t;

typedef enum {
    RISK_LOW = 0,
    RISK_MEDIUM,
    RISK_HIGH,
    RISK_CRITICAL
} risk_level_t;

typedef struct {
    bool handle_divide_by_zero;
    bool handle_null_pointer_access;
    bool auto_allocate_pages;
    bool emulate_invalid_instructions;
    bool attempt_gpf_recovery;
    bool enable_double_fault_recovery;
    bool enable_machine_check_recovery;
    bool enable_stack_protection;
    bool reset_fpu_state;
    bool clear_debug_registers;
    bool emergency_reboot;
    uint32_t max_faults_per_second;
} fault_prevention_config_t;

typedef struct {
    uint64_t total_faults;
    uint64_t double_faults;
    uint64_t general_protection_faults;
    uint64_t page_faults;
    uint64_t invalid_opcode_faults;
    uint64_t divide_error_faults;
    uint64_t machine_check_exceptions;
    uint64_t stack_overflows_prevented;
    uint64_t recovery_attempts;
    uint64_t system_critical_errors;
} fault_statistics_t;

typedef struct {
    bool system_stable;
    bool double_fault_occurred;
    bool triple_fault_imminent;
    bool excessive_fault_rate;
    bool stack_integrity_ok;
    uint32_t nested_fault_depth;
    uint64_t fault_rate;
    risk_level_t risk_level;
} system_health_t;

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t error_code;
    uint16_t cs, ds, es, fs, gs, ss;
} interrupt_context_t;

fault_prevention_error_t fault_prevention_init(const fault_prevention_config_t *config);

fault_prevention_error_t fault_prevention_enable_protection(void);

fault_prevention_error_t fault_prevention_emergency_recovery(void);

fault_prevention_error_t fault_prevention_get_statistics(fault_statistics_t *stats);

fault_prevention_error_t fault_prevention_check_system_health(system_health_t *health);

bool fault_prevention_is_initialized(void);

bool fault_prevention_is_system_stable(void);

void fault_prevention_reset_counters(void);

void system_emergency_shutdown(const char *reason);

static inline const char* fault_prevention_error_to_string(fault_prevention_error_t error) {
    switch (error) {
        case FAULT_PREV_SUCCESS:
            return "Success";
        case FAULT_PREV_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case FAULT_PREV_ERROR_NOT_INITIALIZED:
            return "Fault prevention not initialized";
        case FAULT_PREV_ERROR_STACK_ALLOCATION_FAILED:
            return "Failed to allocate fault handler stacks";
        case FAULT_PREV_ERROR_HANDLER_REGISTRATION_FAILED:
            return "Failed to register fault handlers";
        case FAULT_PREV_ERROR_PROTECTION_NOT_SUPPORTED:
            return "Hardware protection features not supported";
        case FAULT_PREV_ERROR_SYSTEM_UNSTABLE:
            return "System is in unstable state";
        default:
            return "Unknown fault prevention error";
    }
}

static inline const char* risk_level_to_string(risk_level_t level) {
    switch (level) {
        case RISK_LOW:
            return "Low";
        case RISK_MEDIUM:
            return "Medium";
        case RISK_HIGH:
            return "High";
        case RISK_CRITICAL:
            return "Critical";
        default:
            return "Unknown";
    }
}

static inline fault_prevention_config_t fault_prevention_default_config(void) {
    return (fault_prevention_config_t){
        .handle_divide_by_zero = true,
        .handle_null_pointer_access = true,
        .auto_allocate_pages = false,
        .emulate_invalid_instructions = false,
        .attempt_gpf_recovery = false,
        .enable_double_fault_recovery = true,
        .enable_machine_check_recovery = false,
        .enable_stack_protection = true,
        .reset_fpu_state = true,
        .clear_debug_registers = true,
        .emergency_reboot = true,
        .max_faults_per_second = 100
    };
}

static inline fault_prevention_config_t fault_prevention_permissive_config(void) {
    return (fault_prevention_config_t){
        .handle_divide_by_zero = true,
        .handle_null_pointer_access = true,
        .auto_allocate_pages = true,
        .emulate_invalid_instructions = true,
        .attempt_gpf_recovery = true,
        .enable_double_fault_recovery = true,
        .enable_machine_check_recovery = true,
        .enable_stack_protection = true,
        .reset_fpu_state = true,
        .clear_debug_registers = true,
        .emergency_reboot = false,
        .max_faults_per_second = 1000
    };
}

static inline fault_prevention_config_t fault_prevention_strict_config(void) {
    return (fault_prevention_config_t){
        .handle_divide_by_zero = false,
        .handle_null_pointer_access = false,
        .auto_allocate_pages = false,
        .emulate_invalid_instructions = false,
        .attempt_gpf_recovery = false,
        .enable_double_fault_recovery = false,
        .enable_machine_check_recovery = false,
        .enable_stack_protection = true,
        .reset_fpu_state = false,
        .clear_debug_registers = false,
        .emergency_reboot = true,
        .max_faults_per_second = 10
    };
}

typedef void (*fault_handler_t)(interrupt_context_t *context);

typedef enum {
    IST_DOUBLE_FAULT = 1,
    IST_NMI = 2,
    IST_MACHINE_CHECK = 3
} ist_stack_type_t;

extern uint64_t rdtsc(void);
extern uint32_t get_current_cpu_id(void);
extern void panic(const char *format, ...);
extern void outb(uint16_t port, uint8_t value);

#define PAGE_SIZE 4096

extern void* mm_allocate_pages(size_t page_count);
extern bool mm_handle_page_fault(uint64_t address, bool write, bool user);

extern void interrupt_register_handler(uint8_t vector, void *handler, void *context);
extern void x86_64_ist_configure_stack(ist_stack_type_t type, void *stack_top);

#endif // FAULT_PREVENTION_H