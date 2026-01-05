#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdint.h>
#include "types.h"
#include "cpu_ops.h"
#include "atomic.h"
#include "smp.h"
#include "spinlock.h"

/* Low-level IO port helpers */
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t value);
void io_wait(void);

/* Bit manipulation macros */
#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define BITS_TO_LONGS(bits) (((bits) + BITS_PER_LONG - 1) / BITS_PER_LONG)

/* CPU definitions */
#ifndef NR_CPUS
#define NR_CPUS 32  /* Maximum number of CPUs supported */
#endif

/* Forward declarations for structures */
struct interrupt_context;
struct interrupt_frame;
struct cpumask;
struct msi_msg;
struct vm_fault;

/* Advanced Interrupt Management System for Forest OS
 * Provides comprehensive interrupt handling with proper initialization order
 * Supports both x86-32, x86-64, and UEFI environments
 */

/* Architecture Detection */
#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH_64BIT 1
    #define ARCH_32BIT 0
#else
    #define ARCH_64BIT 0
    #define ARCH_32BIT 1
#endif

/* UEFI Support Detection */
#ifdef EFI_BOOT
    #define UEFI_SUPPORT 1
#else
    #define UEFI_SUPPORT 0
#endif

/* Extended Interrupt Vector Constants */
#define IDT_ENTRIES_MAX             256
#define EXCEPTION_VECTORS_MAX       32
#define IRQ_BASE_OFFSET             32
#define SYSCALL_VECTOR              0x80
#define SPURIOUS_VECTOR_PRIMARY     0xFF
#define APIC_SPURIOUS_VECTOR        0xFF
#define NMI_VECTOR                  2

/* Advanced CPU Exception Vectors */
#define EXCEPTION_VIRTUALIZATION    20
#define EXCEPTION_CONTROL_PROTECTION 21
#define EXCEPTION_HYPERVISOR_INJ    28
#define EXCEPTION_VMM_COMM          29
#define EXCEPTION_SECURITY          30

/* Interrupt Controller Types */
typedef enum {
    INTCTL_NONE = 0,
    INTCTL_8259A_PIC,
    INTCTL_LOCAL_APIC,
    INTCTL_IOAPIC,
    INTCTL_MSI,
    INTCTL_HPET,
    INTCTL_RTC,
    INTCTL_UEFI_TIMER,
    INTCTL_PIT
} interrupt_controller_type_t;

/* Interrupt Handler Return Values */
#ifndef IRQ_RETURN_T_DEFINED
#define IRQ_RETURN_T_DEFINED
typedef enum {
    IRQ_NONE = 0,           /* No interrupt was handled */
    IRQ_HANDLED,            /* Interrupt was handled successfully */
    IRQ_WAKE_THREAD,        /* Interrupt handled and should wake thread */
    IRQ_SHARED_CONTINUE     /* Continue processing shared IRQ */
} irq_return_t;
#endif

/* Function type declarations (after enum is defined) */
typedef irq_return_t (*interrupt_handler_t)(int vector, struct interrupt_context *ctx);

/* Interrupt Request Flags */
#define IRQF_SHARED                 (1U << 0)
#define IRQF_DISABLED               (1U << 1)
#define IRQF_SAMPLE_RANDOM          (1U << 2)
#define IRQF_TRIGGER_NONE           (0U << 3)
#define IRQF_TRIGGER_RISING         (1U << 3)
#define IRQF_TRIGGER_FALLING        (2U << 3)
#define IRQF_TRIGGER_HIGH           (4U << 3)
#define IRQF_TRIGGER_LOW            (8U << 3)
#define IRQF_TRIGGER_MASK           (0x0FU << 3)
#define IRQF_EARLY_RESUME           (1U << 7)
#define IRQF_NO_SUSPEND             (1U << 8)
#define IRQF_FORCE_RESUME           (1U << 9)
#define IRQF_NO_THREAD              (1U << 10)
#define IRQF_ONESHOT                (1U << 11)

/* Interrupt State Management */
extern volatile bool interrupts_initialized;
extern volatile bool interrupt_controllers_ready;
extern volatile bool timer_subsystem_ready;

// Safe interrupt control functions
bool irq_save_and_disable_safe(void);
void irq_restore_safe(bool interrupts_enabled);
void irq_enable_safe(void);
void irq_disable_safe(void);
bool irq_are_enabled(void);

// Interrupt initialization
void interrupt_early_init(void);  // Minimal setup for basic operation
void interrupt_full_init(void);   // Full interrupt system setup

// Interrupt descriptor table (IDT) management
#define IDT_ENTRIES 256

extern uint16 g_kernel_code_selector;
extern uint16 g_kernel_data_selector;
void idt_set_gate(uint8 num, uintptr_t handler, uint8 flags);
int idt_register_handler(uint8_t vector, interrupt_handler_t handler, void *data);

// IDT gate flags
#define IDT_FLAG_PRESENT     0x80
#define IDT_FLAG_DPL_0       0x00  // Descriptor Privilege Level 0 (kernel)
#define IDT_FLAG_DPL_3       0x60  // Descriptor Privilege Level 3 (user)
#define IDT_FLAG_INTERRUPT   0x0E  // Interrupt gate
#define IDT_FLAG_TRAP        0x0F  // Trap gate

// Common gate types
#define IDT_GATE_INTERRUPT32 (IDT_FLAG_PRESENT | IDT_FLAG_DPL_0 | IDT_FLAG_INTERRUPT)
#define IDT_GATE_TRAP32      (IDT_FLAG_PRESENT | IDT_FLAG_DPL_0 | IDT_FLAG_TRAP)
#define IDT_GATE_USER_INT    (IDT_FLAG_PRESENT | IDT_FLAG_DPL_3 | IDT_FLAG_INTERRUPT)

/* Enhanced IDT Structures for Multi-Architecture Support */

/* IDT Gate Types (Enhanced) */
#define GATE_TYPE_TASK              0x5
#define GATE_TYPE_INT16             0x6
#define GATE_TYPE_TRAP16            0x7
#define GATE_TYPE_INT32             0xE
#define GATE_TYPE_TRAP32            0xF
#define GATE_TYPE_INT64             0xE  /* Same as 32-bit for compatibility */
#define GATE_TYPE_TRAP64            0xF  /* Same as 32-bit for compatibility */

/* IDT Descriptor Flags (Enhanced) */
#define IDT_PRESENT                 (1 << 7)
#define IDT_DPL_0                   (0 << 5)
#define IDT_DPL_1                   (1 << 5)
#define IDT_DPL_2                   (2 << 5)
#define IDT_DPL_3                   (3 << 5)
#define IDT_STORAGE_SEGMENT         (1 << 4)

#if ARCH_64BIT
/* x86-64 IDT Entry Structure */
typedef struct {
    uint16_t offset_low;      /* Offset bits 0-15 */
    uint16_t selector;        /* Code segment selector */
    uint8_t  ist;             /* Interrupt Stack Table offset (0-7) */
    uint8_t  flags;           /* Type and attributes */
    uint16_t offset_mid;      /* Offset bits 16-31 */
    uint32_t offset_high;     /* Offset bits 32-63 */
    uint32_t reserved;        /* Reserved, must be 0 */
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;           /* Size of IDT - 1 */
    uint64_t base;            /* Address of IDT */
} __attribute__((packed)) idtr_t;

#else
/* x86-32 IDT Entry Structure */
typedef struct {
    uint16_t offset_low;      /* Lower 16 bits of handler address */
    uint16_t selector;        /* Code segment selector */
    uint8_t  reserved;        /* Always 0 */
    uint8_t  flags;           /* Type and attributes */
    uint16_t offset_high;     /* Upper 16 bits of handler address */
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;           /* Size of IDT - 1 */
    uint32_t base;            /* Address of IDT */
} __attribute__((packed)) idtr_t;
#endif

/* IDT Management Structure */
struct idt_manager {
    idt_entry_t entries[IDT_ENTRIES_MAX];
    idtr_t idtr;
    spinlock_t lock;
    atomic_t initialized;
    interrupt_handler_t handlers[IDT_ENTRIES_MAX];
    void *handler_data[IDT_ENTRIES_MAX];
    uint64_t handler_stats[IDT_ENTRIES_MAX];
};

// Exception and interrupt numbers
#define EXCEPTION_DIVIDE_ERROR           0
#define EXCEPTION_DEBUG                  1
#define EXCEPTION_NMI                    2
#define EXCEPTION_BREAKPOINT             3
#define EXCEPTION_OVERFLOW               4
#define EXCEPTION_BOUND_RANGE_EXCEEDED   5
#define EXCEPTION_INVALID_OPCODE         6
#define EXCEPTION_DEVICE_NOT_AVAILABLE   7
#define EXCEPTION_DOUBLE_FAULT           8
#define EXCEPTION_INVALID_TSS           10
#define EXCEPTION_SEGMENT_NOT_PRESENT   11
#define EXCEPTION_STACK_FAULT           12
#define EXCEPTION_GENERAL_PROTECTION    13
#define EXCEPTION_PAGE_FAULT            14
#define EXCEPTION_X87_FPU_ERROR         16
#define EXCEPTION_ALIGNMENT_CHECK       17
#define EXCEPTION_MACHINE_CHECK         18
#define EXCEPTION_SIMD_FP_ERROR         19

// IRQ mappings (after PIC remap)
#define IRQ_TIMER         32
#define IRQ_KEYBOARD      33
#define IRQ_CASCADE       34
#define IRQ_COM2          35
#define IRQ_COM1          36
#define IRQ_LPT2          37
#define IRQ_FLOPPY        38
#define IRQ_LPT1          39
#define IRQ_RTC           40
#define IRQ_FREE1         41
#define IRQ_FREE2         42
#define IRQ_FREE3         43
#define IRQ_MOUSE         44
#define IRQ_FPU           45
#define IRQ_PRIMARY_HD    46
#define IRQ_SECONDARY_HD  47

/* Advanced CPU Register State for Interrupt Context */
#if ARCH_64BIT
/* x86-64 register state structure */
struct cpu_registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t ds, es, fs, gs;  /* Segment registers */
} __attribute__((packed));

struct interrupt_frame {
    uint64_t error_code;      /* Error code (if applicable) */
    uint64_t rip;             /* Instruction pointer */
    uint64_t cs;              /* Code segment */
    uint64_t rflags;          /* Processor flags */
    uint64_t rsp;             /* Stack pointer */
    uint64_t ss;              /* Stack segment */
} __attribute__((packed));

/* Combined context for interrupt handlers */
struct interrupt_context {
    struct cpu_registers regs;
    struct interrupt_frame frame;
    uint64_t vector;
    uint64_t timestamp;
    void *private_data;
} __attribute__((packed));

#else
/* x86-32 register state structure */
struct cpu_registers {
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t ds, es, fs, gs;  /* Segment registers */
} __attribute__((packed));

/* Stack frame pushed by CPU for 32-bit */
struct interrupt_frame {
    uint32_t error_code;      /* Error code (if applicable) */
    uint32_t eip;             /* Instruction pointer */
    uint32_t cs;              /* Code segment */
    uint32_t eflags;          /* Processor flags */
    uint32_t useresp;         /* User stack pointer */
    uint32_t ss;              /* Stack segment */
} __attribute__((packed));

/* Combined context for interrupt handlers */
struct interrupt_context {
    struct cpu_registers regs;
    struct interrupt_frame frame;
    uint32_t vector;
    uint64_t timestamp;
    void *private_data;
} __attribute__((packed));
#endif

/* Enhanced Interrupt Handler Function Types */
typedef irq_return_t (*interrupt_handler_t)(int vector, struct interrupt_context *ctx);
typedef irq_return_t (*exception_handler_t)(int exception, struct interrupt_context *ctx);
typedef irq_return_t (*irq_handler_t)(int irq, void *dev_id, struct interrupt_context *ctx);

/* Legacy compatibility handler type */
typedef void (*legacy_interrupt_handler_t)(struct interrupt_frame* frame, uint32_t error_code);

// Handler registration
void interrupt_set_handler(uint8 int_num, interrupt_handler_t handler);
void interrupt_set_handler_legacy(uint8 int_num, legacy_interrupt_handler_t handler);
void interrupt_clear_handler(uint8 int_num);
interrupt_handler_t interrupt_get_handler(uint8 int_num);

// PIC management
#define PIC1_COMMAND     0x20
#define PIC1_DATA        0x21
#define PIC2_COMMAND     0xA0
#define PIC2_DATA        0xA1
#define PIC_EOI          0x20

void pic_init(void);
void pic_mask_irq(uint8 irq);
void pic_unmask_irq(uint8 irq);
void pic_send_eoi(uint8 irq);
uint16 pic_get_irr(void);
uint16 pic_get_isr(void);

/* Advanced Interrupt Management Structures */

/* Interrupt Action Structure for Shared IRQs */
struct irq_action {
    irq_handler_t handler;
    unsigned long flags;
    const char *name;
    void *dev_id;
    struct irq_action *next;
    atomic_t count;
    uint64_t last_time;
    uint64_t total_time;
};

/* Interrupt Descriptor for IRQ Management */
struct irq_desc {
    struct irq_action *action;
    spinlock_t lock;
    atomic_t depth;          /* Nesting level for enable/disable */
    atomic_t count;          /* Total interrupt count */
    atomic_t unhandled;      /* Unhandled interrupt count */
    unsigned int status;     /* IRQ status flags */
    const char *name;        /* IRQ name for debugging */
    void *chip_data;         /* Controller-specific data */
    
    /* Statistics */
    struct {
        uint64_t count;
        uint64_t spurious;
        uint64_t unhandled;
        uint64_t latency_max;
        uint64_t latency_min;
        uint64_t latency_avg;
        uint64_t last_timestamp;
    } stats;
};

/* Interrupt Controller Chip Operations */
struct irq_chip {
    const char *name;
    void (*startup)(unsigned int irq);
    void (*shutdown)(unsigned int irq);
    void (*enable)(unsigned int irq);
    void (*disable)(unsigned int irq);
    void (*ack)(unsigned int irq);
    void (*mask)(unsigned int irq);
    void (*mask_ack)(unsigned int irq);
    void (*unmask)(unsigned int irq);
    void (*eoi)(unsigned int irq);
    void (*end)(unsigned int irq);
    int  (*set_type)(unsigned int irq, unsigned int flow_type);
    int  (*set_wake)(unsigned int irq, unsigned int on);
    int  (*set_affinity)(unsigned int irq, const struct cpumask *dest);
    void (*bus_lock)(unsigned int irq);
    void (*bus_sync_unlock)(unsigned int irq);
    
    /* MSI support */
    int  (*irq_compose_msi_msg)(unsigned int irq, struct msi_msg *msg);
    void (*irq_write_msi_msg)(unsigned int irq, struct msi_msg *msg);
};

/* Timer Source Structure */
struct timer_source {
    const char *name;
    interrupt_controller_type_t type;
    uint64_t frequency;
    bool high_precision;
    bool per_cpu;

    /* Extended fields for timer_abstraction.c compatibility */
    uint32_t priority;
    uint32_t precision;
    uint32_t state;
    uint64_t last_read_time;
    uint64_t calibrated_frequency;
    uint32_t accuracy_percentage;
    bool supports_oneshot;
    bool supports_periodic;
    bool requires_calibration;
    atomic64_t interrupt_count;
    uint64_t total_runtime_ns;

    /* Operations - functions receive pointer to their timer_source for context */
    int  (*init)(struct timer_source *self);
    void (*enable)(struct timer_source *self);
    void (*disable)(struct timer_source *self);
    void (*set_frequency)(struct timer_source *self, uint64_t freq);
    uint64_t (*get_frequency)(struct timer_source *self);
    void (*set_oneshot)(struct timer_source *self, uint64_t ns);
    void (*set_periodic)(struct timer_source *self, uint64_t ns);
    uint64_t (*read_counter)(struct timer_source *self);
    void (*calibrate)(struct timer_source *self);
    void (*stop)(struct timer_source *self);
    uint64_t (*read)(struct timer_source *self);
    void (*cleanup)(struct timer_source *self);
};

/* Global Interrupt Management Structure */
struct interrupt_manager {
    struct idt_manager idt;
    struct irq_desc irq_desc[256];
    struct irq_chip *irq_chips[256];
    struct timer_source *timer_sources[8];
    
    /* Statistics and state */
    atomic64_t total_interrupts;
    atomic64_t spurious_interrupts;
    atomic64_t unhandled_interrupts;
    
    /* Vector management */
    spinlock_t vector_lock;
    unsigned long allocated_vectors[BITS_TO_LONGS(256)];
    
    /* Per-CPU interrupt stacks (for x86-64) */
#if ARCH_64BIT
    uint64_t interrupt_stacks[NR_CPUS];
    uint64_t nmi_stacks[NR_CPUS];
    uint64_t double_fault_stacks[NR_CPUS];
#endif
    
    /* Initialization state */
    atomic_t early_init_done;
    atomic_t full_init_done;
    atomic_t controllers_ready;
};

/* UEFI Integration Structures */
#if UEFI_SUPPORT
struct uefi_interrupt_context {
    void *original_timer_handler;
    uint64_t timer_period;
    bool timer_active;
    void *boot_services;
    void *runtime_services;
};

extern struct uefi_interrupt_context uefi_int_ctx;
#endif

/* Enhanced Function Declarations */

/* Core interrupt management */
int interrupt_init_early(void);
int interrupt_init_full(void);
void interrupt_late_init(void);
int interrupt_reinit_after_memory(void);

/* Enhanced IDT management */
int idt_init_early(void);
int idt_init_full(void);
void idt_set_gate_enhanced(uint8_t vector, void *handler, uint16_t selector, 
                          uint8_t flags, uint8_t ist);
void idt_set_user_gate(uint8_t vector, void *handler);
void idt_set_system_gate(uint8_t vector, void *handler);
void idt_load_new(void);

/* Advanced IRQ management */
int request_irq_advanced(unsigned int irq, irq_handler_t handler, 
                        unsigned long flags, const char *name, void *dev_id);
int request_threaded_irq(unsigned int irq, irq_handler_t handler,
                        irq_handler_t thread_fn, unsigned long flags,
                        const char *name, void *dev_id);
void free_irq_advanced(unsigned int irq, void *dev_id);
int setup_irq(unsigned int irq, struct irq_action *action);
void remove_irq(unsigned int irq, struct irq_action *action);

/* IRQ control */
void enable_irq_advanced(unsigned int irq);
void disable_irq_advanced(unsigned int irq);
void disable_irq_nosync_advanced(unsigned int irq);
void synchronize_irq_advanced(unsigned int irq);
bool irq_can_set_affinity(unsigned int irq);
int irq_set_affinity(unsigned int irq, const struct cpumask *dest);

/* Interrupt controller framework */
int register_irq_chip_advanced(unsigned int irq, struct irq_chip *chip);
void unregister_irq_chip_advanced(unsigned int irq);
struct irq_chip *get_irq_chip_advanced(unsigned int irq);
void irq_chip_set_defaults(struct irq_chip *chip);

/* Vector allocation and management */
int allocate_interrupt_vector_range(int count);
int allocate_interrupt_vector_specific(int vector);
void free_interrupt_vector_range(int start, int count);
void free_interrupt_vector_specific(int vector);
bool is_vector_allocated(int vector);
void reserve_system_vectors(void);

/* Exception handling framework */
int register_exception_handler_advanced(int exception, exception_handler_t handler);
void unregister_exception_handler_advanced(int exception);
void setup_exception_handlers(void);
void handle_double_fault(struct interrupt_context *ctx);
void handle_page_fault(struct interrupt_context *ctx);

/* SMP and IPI support */
void smp_interrupt_init(void);
int send_ipi(int cpu, int vector);
int send_ipi_mask(const struct cpumask *mask, int vector);
int send_ipi_all(int vector);
int send_ipi_allbutself(int vector);
void handle_reschedule_ipi(void);
void handle_call_function_ipi(void);

/* Timer and timing subsystem */
int timer_interrupt_init(void);
int register_timer_source(struct timer_source *source);
void unregister_timer_source(struct timer_source *source);
struct timer_source *get_primary_timer_source(void);
int calibrate_timer_sources(void);
uint64_t get_system_time_ns(void);

/* PIC statistics structure */
struct pic_stats {
    interrupt_controller_type_t type;
    bool initialized;
    bool auto_eoi;
    uint16_t irq_mask;
    uint64_t irq_counts[16];
    uint64_t spurious_count;
    uint64_t last_spurious_time;
};

/* Advanced PIC 8259A functions */
void pic_8259a_mask_irq(uint8_t irq);
void pic_8259a_unmask_irq(uint8_t irq);
void pic_8259a_send_eoi(uint8_t irq);
uint16_t pic_8259a_get_irr(void);
uint16_t pic_8259a_get_isr(void);
void pic_8259a_disable(void);
void pic_8259a_get_stats(struct pic_stats *stats);
void pic_8259a_debug_dump(void);
bool pic_8259a_is_irq_enabled(uint8_t irq);
uint16_t pic_8259a_get_mask(void);
bool pic_is_available(void);

/* Interrupt controller chip operations for abstraction layer */
struct interrupt_chip_ops {
    const char *name;
    int (*init)(void);
    void (*mask)(uint8_t irq);
    void (*unmask)(uint8_t irq);
    void (*eoi)(uint8_t irq);
    void (*disable)(void);
    void (*get_stats)(void *stats);
};

extern struct interrupt_chip_ops pic_8259a_chip_ops;

/* APIC statistics structure */
struct apic_stats {
    bool detected;
    bool local_apic_enabled;
    bool x2apic_available;
    bool x2apic_enabled;
    uint32_t apic_id;
    uint32_t version;
    uint32_t max_lvt_entries;
    uint32_t timer_frequency;
    uint64_t timer_interrupts;
    uint64_t spurious_interrupts;
    uint64_t error_interrupts;
};

/* Advanced APIC functions */
void apic_send_eoi(void);
int apic_send_ipi(uint32_t dest_apic_id, uint32_t vector, uint32_t delivery_mode);
void apic_get_stats(struct apic_stats *stats);
bool apic_is_available(void);
uint32_t apic_get_id(void);

/* APIC Timer statistics structure */
struct apic_timer_stats {
    bool initialized;
    bool running;
    bool periodic_mode;
    uint32_t base_frequency;
    uint32_t current_divisor;
    uint64_t timer_interrupts;
    uint64_t missed_deadlines;
    uint64_t system_time_ns;
};

/* APIC Timer functions */
int apic_timer_init(void);
void apic_timer_get_stats(struct apic_timer_stats *stats);
bool apic_timer_is_available(void);
uint64_t apic_timer_get_time_ns(void);

/* I/O APIC device information */
struct ioapic_device_info {
    bool present;
    uint8_t id;
    uint32_t physical_addr;
    uint32_t global_irq_base;
    uint32_t max_redirections;
    uint32_t version;
};

/* I/O APIC statistics structure */
struct ioapic_stats {
    bool initialized;
    int num_ioapics;
    uint32_t total_irqs;
    int num_overrides;
    uint64_t total_interrupts;
    struct ioapic_device_info devices[8];
};

/* I/O APIC functions */
int ioapic_enable_irq(uint8_t irq);
int ioapic_disable_irq(uint8_t irq);
int ioapic_set_affinity(uint8_t irq, uint32_t target_apic_id);
void ioapic_get_stats(struct ioapic_stats *stats);
bool ioapic_is_available(void);
void ioapic_debug_dump(void);

/* HPET timer information */
struct hpet_timer_info {
    bool in_use;
    bool periodic_capable;
    bool supports_64bit;
    uint64_t interrupt_count;
};

/* HPET statistics structure */
struct hpet_stats {
    bool detected;
    bool initialized;
    bool enabled;
    uint64_t frequency;
    uint64_t period_femtoseconds;
    uint32_t num_timers;
    bool supports_64bit;
    bool supports_legacy_replacement;
    uint64_t main_counter_wraps;
    uint64_t current_counter;
    struct hpet_timer_info timers[8];
};

/* HPET functions */
void hpet_get_stats(struct hpet_stats *stats);
bool hpet_is_available(void);
uint64_t hpet_get_time_ns(void);

/* PIT statistics structure */
struct pit_stats {
    bool initialized;
    bool channel0_in_use;
    bool channel2_in_use;
    uint32_t current_frequency;
    uint16_t current_divisor;
    uint64_t timer_interrupts;
    uint64_t system_time_ns;
    uint64_t last_interrupt_time;
};

/* PIT functions */
void pit_delay_ms(uint32_t milliseconds);
void pit_udelay(uint32_t microseconds);
uint32_t pit_get_frequency(void);
uint64_t pit_calibrate_timing_source(uint64_t (*read_counter)(void), uint32_t ms_duration);
void pit_get_stats(struct pit_stats *stats);
bool pit_is_available(void);
uint64_t pit_get_time_ns(void);
int pit_enable_system_timer(void);
void pit_disable_system_timer(void);

/* RTC time structure */
struct rtc_time {
    uint8_t second;     /* 0-59 */
    uint8_t minute;     /* 0-59 */
    uint8_t hour;       /* 0-23 */
    uint8_t day;        /* 1-31 */
    uint8_t month;      /* 1-12 */
    uint8_t year;       /* 0-99 */
    uint8_t century;    /* 19-20 */
    uint8_t weekday;    /* 1-7 */
};

/* RTC statistics structure */
struct rtc_stats {
    bool detected;
    bool initialized;
    bool periodic_enabled;
    bool update_enabled;
    bool alarm_enabled;
    bool binary_mode;
    bool hour_24_mode;
    uint8_t current_rate;
    uint32_t frequency;
    uint64_t periodic_interrupts;
    uint64_t update_interrupts;
    uint64_t alarm_interrupts;
    uint64_t last_update_time;
    struct rtc_time current_time;
};

/* RTC functions */
int rtc_enable_periodic_interrupts(uint8_t rate);
void rtc_disable_periodic_interrupts(void);
int rtc_enable_update_interrupts(void);
void rtc_disable_update_interrupts(void);
void rtc_get_stats(struct rtc_stats *stats);
bool rtc_is_available(void);

/* NMI statistics structure */
struct nmi_stats {
    bool enabled;
    uint32_t handler_state;
    uint64_t total_nmis;
    uint64_t memory_parity_errors;
    uint64_t channel_check_errors;
    uint64_t watchdog_timeouts;
    uint64_t pcie_errors;
    uint64_t thermal_events;
    uint64_t voltage_events;
    uint64_t software_nmis;
    uint64_t unknown_nmis;
    uint64_t recursive_nmis;
    uint64_t last_nmi_time;
    uint64_t last_nmi_rip;
    uint32_t last_nmi_source;
    uint32_t consecutive_nmis;
};

/* NMI functions */
int nmi_init(void);
void nmi_enable(void);
void nmi_disable(void);
void nmi_trigger_software(void);
void nmi_set_panic_on_error(bool panic_on_nmi);
void nmi_set_log_all(bool log_all);
void nmi_get_stats(struct nmi_stats *stats);
bool nmi_is_available(void);
irq_return_t nmi_handler_c(struct interrupt_context *ctx);

/* Timer source information structure */
struct timer_source_info {
    const char *name;
    uint32_t priority;
    uint32_t precision;
    uint64_t frequency;
    uint32_t state;
    uint64_t interrupt_count;
    uint32_t accuracy_percentage;
};

/* Timer abstraction statistics */
struct timer_abstraction_stats {
    bool initialized;
    uint32_t num_sources;
    const char *primary_source_name;
    const char *fallback_source_name;
    uint64_t system_uptime_ns;
    uint64_t timer_switches;
    uint64_t timer_failures;
    bool auto_fallback_enabled;
    struct timer_source_info sources[16];
};

/* Timer abstraction functions */
int timer_interrupt_init(void);
int register_timer_source(struct timer_source *source);
void unregister_timer_source(struct timer_source *source);
struct timer_source *get_primary_timer_source(void);
uint64_t get_system_time_ns(void);
int timer_set_periodic(uint64_t period_ns);
int timer_set_oneshot(uint64_t timeout_ns);
void timer_stop(void);
void timer_interrupt_handler(void);
void timer_get_abstraction_stats(struct timer_abstraction_stats *stats);

/* Platform-specific controller initialization */
int pic_8259a_init_advanced(void);
int local_apic_init(void);
int ioapic_init_advanced(void);
int msi_init_advanced(void);
int hpet_init_advanced(void);
int rtc_interrupt_init(void);
int pit_init_advanced(void);

/* UEFI integration functions */
#if UEFI_SUPPORT
int uefi_interrupt_early_init(void);
int uefi_timer_setup(uint64_t period_us);
void uefi_timer_cleanup(void);
void uefi_exit_boot_services_prep(void);
void uefi_runtime_services_setup(void);
#endif

/* Debugging, profiling and statistics */
void interrupt_debug_init(void);
void interrupt_stats_init_advanced(void);
void interrupt_stats_update_latency(int irq, uint64_t start_time, uint64_t end_time);
void interrupt_stats_get_advanced(int irq, struct irq_desc *desc);
void dump_interrupt_stats_advanced(void);
void trace_interrupt_entry_advanced(int vector, struct interrupt_context *ctx);
void trace_interrupt_exit_advanced(int vector, irq_return_t ret, uint64_t duration);

/* Interrupt context and state queries */
bool in_interrupt_context(void);
bool in_irq_context(void);
bool in_nmi_context(void);
bool in_exception_context(void);
int get_interrupt_nesting_level(void);
struct interrupt_context *get_current_interrupt_context(void);

/* Memory management integration */
void interrupt_mm_init(void);
void *alloc_interrupt_stack(int cpu);
void free_interrupt_stack(void *stack);
void setup_per_cpu_interrupt_stacks(void);

/* Power management integration */
void interrupt_suspend_prepare(void);
void interrupt_suspend_late(void);
void interrupt_resume_early(void);
void interrupt_resume_complete(void);

/* Error handling and recovery */
void handle_spurious_interrupt(int vector);
void handle_unhandled_interrupt(int vector);
void interrupt_error_recovery(int vector, int error_code);
void dump_interrupt_context(struct interrupt_context *ctx);

/* Assembly stub declarations for both architectures */
#if ARCH_64BIT
/* x86-64 ISR stubs */
extern void isr_stub_0(void), isr_stub_1(void), isr_stub_2(void), isr_stub_3(void);
extern void isr_stub_4(void), isr_stub_5(void), isr_stub_6(void), isr_stub_7(void);
extern void isr_stub_8(void), isr_stub_9(void), isr_stub_10(void), isr_stub_11(void);
extern void isr_stub_12(void), isr_stub_13(void), isr_stub_14(void), isr_stub_15(void);
extern void isr_stub_16(void), isr_stub_17(void), isr_stub_18(void), isr_stub_19(void);
extern void isr_stub_20(void), isr_stub_21(void), isr_stub_22(void), isr_stub_23(void);
extern void isr_stub_24(void), isr_stub_25(void), isr_stub_26(void), isr_stub_27(void);
extern void isr_stub_28(void), isr_stub_29(void), isr_stub_30(void), isr_stub_31(void);

/* x86-64 IRQ stubs */
extern void irq_stub_0(void), irq_stub_1(void), irq_stub_2(void), irq_stub_3(void);
extern void irq_stub_4(void), irq_stub_5(void), irq_stub_6(void), irq_stub_7(void);
extern void irq_stub_8(void), irq_stub_9(void), irq_stub_10(void), irq_stub_11(void);
extern void irq_stub_12(void), irq_stub_13(void), irq_stub_14(void), irq_stub_15(void);

/* x86-64 special handlers */
extern void nmi_handler(void); /* Assembly stub */
extern void double_fault_handler(void);
extern void machine_check_handler(void);
#else
/* x86-32 ISR stubs (existing declarations) */
#endif

/* Global interrupt manager instance */
extern struct interrupt_manager interrupt_mgr;

/* Inline utility functions for interrupt control */
static inline void arch_local_irq_disable(void) {
    asm volatile("cli" ::: "memory");
}

static inline void arch_local_irq_enable(void) {
    asm volatile("sti" ::: "memory");
}

static inline unsigned long arch_local_save_flags(void) {
    unsigned long flags;
#if ARCH_64BIT
    asm volatile("pushfq; popq %0" : "=rm" (flags) :: "memory");
#else
    asm volatile("pushfl; popl %0" : "=rm" (flags) :: "memory");
#endif
    return flags;
}

static inline void arch_local_irq_restore(unsigned long flags) {
#if ARCH_64BIT
    asm volatile("pushq %0; popfq" :: "rm" (flags) : "memory", "cc");
#else
    asm volatile("pushl %0; popfl" :: "rm" (flags) : "memory", "cc");
#endif
}

static inline unsigned long arch_local_irq_save(void) {
    unsigned long flags = arch_local_save_flags();
    arch_local_irq_disable();
    return flags;
}

static inline bool arch_irqs_disabled_flags(unsigned long flags) {
    return !(flags & 0x200);  /* Check IF flag */
}

static inline bool arch_irqs_disabled(void) {
    return arch_irqs_disabled_flags(arch_local_save_flags());
}

/* Memory barriers for interrupt synchronization */
#define irq_mb()    mb()
#define irq_rmb()   rmb()
#define irq_wmb()   wmb()

/* Safe interrupt control macros */
#define interrupt_save_and_disable()   arch_local_irq_save()
#define interrupt_restore(flags)       arch_local_irq_restore(flags)
#define interrupt_disable_local()      arch_local_irq_disable()
#define interrupt_enable_local()       arch_local_irq_enable()

// Common interrupt handler called by C stubs
void interrupt_common_handler(int int_no, struct interrupt_frame* frame, unsigned int error_code);

/* Enhanced interrupt dispatcher */
void interrupt_dispatch_handler(struct interrupt_context *ctx);

/* ===============================
 * INTERRUPT PRIORITY AND NESTING SUPPORT
 * =============================== */

/* Interrupt Priority Levels (0 = highest, 255 = lowest) */
#define INTERRUPT_PRIORITY_NMI              0   /* Non-maskable interrupts */
#define INTERRUPT_PRIORITY_MACHINE_CHECK    1   /* Machine check exceptions */
#define INTERRUPT_PRIORITY_DOUBLE_FAULT     2   /* Double fault handlers */
#define INTERRUPT_PRIORITY_CRITICAL         16  /* Critical system interrupts */
#define INTERRUPT_PRIORITY_HIGH             32  /* High priority interrupts */
#define INTERRUPT_PRIORITY_TIMER            48  /* Timer interrupts */
#define INTERRUPT_PRIORITY_NORMAL           64  /* Normal priority interrupts */
#define INTERRUPT_PRIORITY_LOW              128 /* Low priority interrupts */
#define INTERRUPT_PRIORITY_BACKGROUND       192 /* Background/deferred interrupts */
#define INTERRUPT_PRIORITY_LOWEST           255 /* Lowest priority */

/* Maximum nesting depth for interrupt handling */
#define MAX_INTERRUPT_NESTING_DEPTH         16

/* Priority state flags */
#define PRIORITY_FLAG_PREEMPTIBLE          (1U << 0)  /* Can be preempted */
#define PRIORITY_FLAG_CRITICAL             (1U << 1)  /* Cannot be preempted */
#define PRIORITY_FLAG_ATOMIC               (1U << 2)  /* Atomic operation */
#define PRIORITY_FLAG_REALTIME             (1U << 3)  /* Real-time constraint */
#define PRIORITY_FLAG_INHERIT_PRIORITY     (1U << 4)  /* Can inherit priority */

/* Interrupt nesting context structure */
struct interrupt_nesting_context {
    uint8_t current_priority;          /* Current interrupt priority */
    uint8_t original_priority;         /* Original priority before inheritance */
    uint8_t nesting_level;             /* Current nesting depth */
    uint32_t flags;                    /* Priority and nesting flags */
    uint64_t entry_timestamp;          /* When interrupt started */
    uint64_t deadline;                 /* Real-time deadline (0 if none) */
    struct interrupt_context *contexts[MAX_INTERRUPT_NESTING_DEPTH]; /* Context stack */
    void *stack_pointers[MAX_INTERRUPT_NESTING_DEPTH]; /* Stack pointer stack */
    uint8_t priorities[MAX_INTERRUPT_NESTING_DEPTH];   /* Priority stack */
} __attribute__((packed));

/* Per-CPU priority state */
struct cpu_priority_state {
    struct interrupt_nesting_context nesting;
    spinlock_t priority_lock;          /* Protects priority changes */
    atomic_t preemption_count;         /* Preemption disable counter */
    bool priority_inversion_detected;  /* Priority inversion flag */
    uint64_t last_priority_boost;      /* Last priority boost timestamp */
    
    /* Statistics */
    struct {
        uint64_t total_nesting_events;
        uint64_t max_nesting_depth;
        uint64_t priority_inversions;
        uint64_t priority_inheritances;
        uint64_t preemptions;
        uint64_t deadline_misses;
    } stats;
} __attribute__((aligned(64))); /* Cache line aligned */

/* Global priority management structure */
struct priority_manager {
    struct cpu_priority_state cpu_states[NR_CPUS];
    uint8_t vector_priorities[256];    /* Priority for each interrupt vector */
    spinlock_t global_priority_lock;   /* Global priority operations */
    
    /* Priority inheritance tracking */
    struct {
        int active_chains;             /* Number of active inheritance chains */
        int max_chain_length;          /* Maximum inheritance chain length */
        uint64_t total_inheritance_time;
    } inheritance;
    
    /* Global statistics */
    atomic64_t total_nested_interrupts;
    atomic64_t total_preemptions;
    atomic64_t priority_violations;
    
    bool initialized;
};

/* Priority boost structure for avoiding priority inversion */
struct priority_boost {
    uint8_t original_priority;
    uint8_t boosted_priority;
    uint64_t boost_start_time;
    uint64_t boost_duration_ns;
    int vector;
    bool active;
};

/* Function declarations for priority and nesting support */

/* Core priority management */
int interrupt_priority_init(void);
int interrupt_priority_init_cpu(int cpu);
void interrupt_priority_cleanup(void);

/* Priority configuration */
int interrupt_set_priority(int vector, uint8_t priority, uint32_t flags);
uint8_t interrupt_get_priority(int vector);
int interrupt_set_realtime_deadline(int vector, uint64_t deadline_ns);

/* Nesting control */
bool interrupt_can_preempt(int new_vector, int current_vector);
int interrupt_enter_nested(int vector, struct interrupt_context *ctx);
void interrupt_exit_nested(int vector);
int interrupt_get_nesting_level(void);
bool interrupt_is_nested(void);

/* Priority inheritance */
int interrupt_boost_priority(int vector, uint8_t new_priority, uint64_t duration_ns);
void interrupt_inherit_priority(int vector, uint8_t inherited_priority);
void interrupt_restore_priority(int vector);
void interrupt_resolve_priority_inversion(void);

/* Preemption control */
void interrupt_preempt_disable(void);
void interrupt_preempt_enable(void);
bool interrupt_preemptible(void);
int interrupt_preempt_count(void);

/* Real-time support */
int interrupt_set_deadline(int vector, uint64_t deadline_ns);
bool interrupt_check_deadline(int vector);
void interrupt_deadline_missed(int vector);

/* Context switching for nested interrupts */
int interrupt_switch_context(struct interrupt_context *old_ctx, 
                           struct interrupt_context *new_ctx, int vector);
void interrupt_save_nested_context(int level, struct interrupt_context *ctx);
struct interrupt_context *interrupt_restore_nested_context(int level);

/* Stack management for nesting */
void *interrupt_allocate_nested_stack(int cpu, int level);
void interrupt_free_nested_stack(void *stack);
void interrupt_switch_to_nested_stack(void *stack);

/* Priority violation detection */
void interrupt_detect_priority_violation(int vector, uint8_t expected_priority);
void interrupt_log_priority_violation(int vector, const char *reason);

/* Statistics and debugging */
void interrupt_priority_get_stats(int cpu, struct cpu_priority_state *stats);
void interrupt_priority_dump_state(void);
void interrupt_priority_debug_enable(bool enable);
void interrupt_priority_trace_nesting(int vector, bool entering);

/* Priority utility functions */
static inline bool priority_is_higher(uint8_t prio1, uint8_t prio2) {
    return prio1 < prio2;  /* Lower numerical value = higher priority */
}

static inline bool priority_is_critical(uint8_t priority) {
    return priority <= INTERRUPT_PRIORITY_CRITICAL;
}

static inline bool priority_is_realtime(uint32_t flags) {
    return (flags & PRIORITY_FLAG_REALTIME) != 0;
}

static inline bool can_be_preempted(uint32_t flags) {
    return (flags & PRIORITY_FLAG_PREEMPTIBLE) && 
           !(flags & PRIORITY_FLAG_CRITICAL) &&
           !(flags & PRIORITY_FLAG_ATOMIC);
}

/* Macros for safe priority-aware interrupt handling */
#define INTERRUPT_PRIORITY_SAVE_DISABLE() \
    ({ \
        unsigned long __flags = arch_local_irq_save(); \
        interrupt_preempt_disable(); \
        __flags; \
    })

#define INTERRUPT_PRIORITY_RESTORE(__flags) \
    do { \
        interrupt_preempt_enable(); \
        arch_local_irq_restore(__flags); \
    } while (0)

/* Global priority manager instance */
extern struct priority_manager priority_mgr;

#endif // INTERRUPT_H
