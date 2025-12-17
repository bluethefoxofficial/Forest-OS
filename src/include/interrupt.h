#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "types.h"

/* Interrupt Management System for Forest OS
 * Provides comprehensive interrupt handling with proper initialization order
 */

// Interrupt state management
extern volatile bool interrupts_initialized;

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
void idt_set_gate(uint8 num, uint32 handler, uint8 flags);

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

// IDT structures
typedef struct {
    uint16 offset_low;    // Lower 16 bits of handler address
    uint16 selector;      // Code segment selector
    uint8  reserved;      // Always 0
    uint8  flags;         // Type and attributes
    uint16 offset_high;   // Upper 16 bits of handler address
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16 limit;         // Size of IDT - 1
    uint32 base;          // Address of IDT
} __attribute__((packed)) idtr_t;

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

// Stack frame pushed by CPU and GCC's __attribute__((interrupt))
struct interrupt_frame {
    uint32 eip;
    uint32 cs;
    uint32 eflags;
    uint32 useresp;
    uint32 ss;
};

// Interrupt handler function type
typedef void (*interrupt_handler_t)(struct interrupt_frame* frame, uint32 error_code);

// Handler registration
void interrupt_set_handler(uint8 int_num, interrupt_handler_t handler);
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

// Common interrupt handler called by C stubs
void interrupt_common_handler(int int_no, struct interrupt_frame* frame, unsigned int error_code);

#endif // INTERRUPT_H
