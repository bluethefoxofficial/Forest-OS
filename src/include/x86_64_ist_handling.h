#ifndef X86_64_IST_HANDLING_H
#define X86_64_IST_HANDLING_H

#include <stdint.h>
#include <stdbool.h>

/*
 * x86-64 Interrupt Stack Table (IST) handling
 * Provides dedicated stacks for critical interrupts
 */

// IST stack indices (1-7, 0 means no IST)
#define IST_STACK_NONE          0
#define IST_STACK_DF            1  // Double Fault
#define IST_STACK_NMI           2  // Non-Maskable Interrupt
#define IST_STACK_MC            3  // Machine Check
#define IST_STACK_DEBUG         4  // Debug exceptions
#define IST_STACK_RESERVED1     5
#define IST_STACK_RESERVED2     6
#define IST_STACK_RESERVED3     7

// IST stack size (16KB per stack)
#define IST_STACK_SIZE          0x4000

// Maximum number of CPUs supported
#define MAX_CPUS                256

typedef struct {
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
} __attribute__((packed)) ist_table_t;

typedef struct {
    void *stacks[8];  // IST stacks 0-7 (0 unused)
    uintptr_t stack_tops[8];
    bool initialized;
} cpu_ist_context_t;

// IST initialization and management
int ist_init_global(void);
int ist_init_cpu(uint32_t cpu_id);
void ist_cleanup_cpu(uint32_t cpu_id);

// Stack allocation and setup
int ist_allocate_stacks(uint32_t cpu_id);
void ist_free_stacks(uint32_t cpu_id);
int ist_setup_tss_stacks(uint32_t cpu_id);

// IST stack access
uintptr_t ist_get_stack_top(uint32_t cpu_id, uint8_t ist_index);
void *ist_get_stack_base(uint32_t cpu_id, uint8_t ist_index);
size_t ist_get_stack_size(void);

// Exception handlers that use IST
void ist_double_fault_handler(void);
void ist_nmi_handler(void);
void ist_machine_check_handler(void);
void ist_debug_handler(void);

// IST configuration
int ist_configure_idt_entry(uint8_t vector, uint8_t ist_index);
int ist_set_handler_stack(uint8_t vector, uint8_t ist_index);

// Debugging and status
bool ist_is_initialized(uint32_t cpu_id);
void ist_dump_stacks(uint32_t cpu_id);
uint32_t ist_get_current_cpu(void);

// Stack overflow detection
bool ist_check_stack_overflow(uint32_t cpu_id, uint8_t ist_index);
void ist_install_guard_pages(uint32_t cpu_id);

// Per-CPU TSS management
int ist_update_tss(uint32_t cpu_id);
void ist_load_tss(uint32_t cpu_id);

#ifdef CONFIG_SMP
// SMP-specific IST functions
int ist_init_all_cpus(void);
void ist_cleanup_all_cpus(void);
#endif

#endif // X86_64_IST_HANDLING_H