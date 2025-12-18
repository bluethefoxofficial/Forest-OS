/*
 * Forest OS interrupt handling
 *
 * Provides a predictable initialization sequence and safe helper
 * routines for enabling/disabling interrupts at runtime.
 */

#include "include/cpu_ops.h"
#include "include/interrupt.h"
#include "include/interrupt_handlers.h"
#include "include/io_ports.h"
#include "include/panic.h"
#include "include/screen.h"
#include "include/util.h"

// =============================================================================
// GLOBAL STATE
// =============================================================================

typedef enum interrupt_state {
    INTERRUPT_STATE_OFF = 0,
    INTERRUPT_STATE_EARLY,
    INTERRUPT_STATE_READY
} interrupt_state_t;

volatile bool interrupts_initialized = false;
static volatile interrupt_state_t interrupt_state = INTERRUPT_STATE_OFF;

static idt_entry_t idt[IDT_ENTRIES];
static idtr_t idtr;
static interrupt_handler_t interrupt_handlers[IDT_ENTRIES] = {0};

uint16 g_kernel_code_selector = 0;
uint16 g_kernel_data_selector = 0;

const char* exception_names[32] = {
    "Division Error",                "Debug Exception",
    "Non-Maskable Interrupt",        "Breakpoint",
    "Overflow",                      "Bound Range Exceeded",
    "Invalid Opcode",                "Device Not Available",
    "Double Fault",                  "Coprocessor Segment Overrun",
    "Invalid TSS",                   "Segment Not Present",
    "Stack Fault",                   "General Protection Fault",
    "Page Fault",                    "Reserved",
    "x87 FPU Error",                 "Alignment Check",
    "Machine Check",                 "SIMD Floating-Point Exception",
    "Virtualization Exception",      "Control Protection Exception",
    "Reserved",                      "Reserved",
    "Reserved",                      "Reserved",
    "Reserved",                      "Reserved",
    "Reserved",                      "Reserved",
    "Reserved",                      "Reserved"
};

// =============================================================================
// LOW LEVEL HELPERS
// =============================================================================

static inline bool cpu_interrupt_flag(void) {
    uint32 flags = cpu_read_eflags();
    return (flags & (1u << 9)) != 0;
}

static inline bool interrupt_system_ready(void) {
    return interrupt_state == INTERRUPT_STATE_READY;
}

static inline uint16 kernel_code_selector(void) {
    if (g_kernel_code_selector == 0) {
        g_kernel_code_selector = cpu_read_cs();
    }
    return g_kernel_code_selector;
}

void idt_set_gate(uint8 num, uint32 handler, uint8 flags) {
    idt[num].offset_low  = handler & 0xFFFF;
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
    idt[num].selector    = kernel_code_selector();
    idt[num].reserved    = 0;
    idt[num].flags       = flags;
}

static void idt_clear(void) {
    for (int i = 0; i < IDT_ENTRIES; ++i) {
        idt_set_gate(i, 0, 0);
        interrupt_handlers[i] = NULL;
    }
}

static void idt_load(void) {
    idtr.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idtr.base  = (uint32)&idt;
    cpu_load_idt(&idtr);
}

// =============================================================================
// SAFE INTERRUPT CONTROL FUNCTIONS
// =============================================================================

bool irq_save_and_disable_safe(void) {
    bool were_enabled = cpu_interrupt_flag();
    cpu_disable_interrupts();
    return were_enabled;
}

void irq_restore_safe(bool interrupts_enabled) {
    if (interrupts_enabled) {
        irq_enable_safe();
    } else {
        irq_disable_safe();
    }
}

void irq_enable_safe(void) {
    if (!interrupt_system_ready()) {
        kernel_panic("Interrupt system not ready when enabling IRQs");
    }
    cpu_enable_interrupts();
}

void irq_disable_safe(void) {
    cpu_disable_interrupts();
}

bool irq_are_enabled(void) {
    return cpu_interrupt_flag();
}

// =============================================================================
// PIC MANAGEMENT
// =============================================================================

void pic_init(void) {
    outportb(PIC1_COMMAND, 0x11);
    io_wait();
    outportb(PIC2_COMMAND, 0x11);
    io_wait();

    outportb(PIC1_DATA, 0x20); // Master PIC vector offset
    io_wait();
    outportb(PIC2_DATA, 0x28); // Slave PIC vector offset
    io_wait();

    outportb(PIC1_DATA, 0x04);
    io_wait();
    outportb(PIC2_DATA, 0x02);
    io_wait();

    outportb(PIC1_DATA, 0x01);
    io_wait();
    outportb(PIC2_DATA, 0x01);
    io_wait();

    // Mask all IRQs
    outportb(PIC1_DATA, 0xFF);
    outportb(PIC2_DATA, 0xFF);
}

void pic_mask_irq(uint8 irq) {
    uint16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) {
        irq -= 8;
    }
    uint8 value = inportb(port) | (1 << irq);
    outportb(port, value);
}

void pic_unmask_irq(uint8 irq) {
    uint16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) {
        irq -= 8;
    }
    uint8 value = inportb(port) & ~(1 << irq);
    outportb(port, value);
}

void pic_send_eoi(uint8 irq) {
    if (irq >= 8) {
        outportb(PIC2_COMMAND, PIC_EOI);
    }
    outportb(PIC1_COMMAND, PIC_EOI);
}

uint16 pic_get_irr(void) {
    outportb(PIC1_COMMAND, 0x0A);
    outportb(PIC2_COMMAND, 0x0A);
    return (inportb(PIC2_COMMAND) << 8) | inportb(PIC1_COMMAND);
}

uint16 pic_get_isr(void) {
    outportb(PIC1_COMMAND, 0x0B);
    outportb(PIC2_COMMAND, 0x0B);
    return (inportb(PIC2_COMMAND) << 8) | inportb(PIC1_COMMAND);
}

// =============================================================================
// HANDLER REGISTRATION
// =============================================================================

void interrupt_set_handler(uint8 int_num, interrupt_handler_t handler) {
    interrupt_handlers[int_num] = handler;
}

void interrupt_clear_handler(uint8 int_num) {
    interrupt_handlers[int_num] = NULL;
}

interrupt_handler_t interrupt_get_handler(uint8 int_num) {
    return interrupt_handlers[int_num];
}

// =============================================================================
// DEFAULT HANDLERS
// =============================================================================

static void default_exception_handler(int int_no, struct interrupt_frame* frame, unsigned int error_code) {
    // Check if this is a user-mode exception (based on CS register)
    bool is_user_mode = (frame->cs & 0x3) == 0x3;  // Ring 3 (user mode)
    
    print_colored("[EXCEPTION] ", 0x0C, 0x00);
    print(exception_names[int_no]);
    print(" at EIP: ");
    print(int_to_string((uint32)frame->eip));
    
    if (error_code != 0) {
        print(" (Error Code: ");
        print(int_to_string(error_code));
        print(")");
    }
    
    print(" - ");
    print(is_user_mode ? "User Mode" : "Kernel Mode");
    print("\n");
    
    // For critical kernel-level exceptions, panic immediately
    if (!is_user_mode && (int_no == 8 || int_no == 10 || int_no == 11 || int_no == 12)) {
        // Double Fault, Invalid TSS, Segment Not Present, Stack Fault
        kernel_panic_annotated(
            exception_names[int_no],
            __FILE__,
            __LINE__,
            __FUNCTION__
        );
    }
    
    // For user-mode exceptions or recoverable kernel exceptions
    if (is_user_mode) {
        print_colored("[EXCEPTION] Terminating user process due to exception\n", 0x0E, 0x00);
        // TODO: Implement proper process termination
        // For now, we'll panic but with a different message
        print_colored("[KERNEL] Process termination not yet implemented, halting system\n", 0x0C, 0x00);
    } else {
        print_colored("[EXCEPTION] Recoverable kernel exception, attempting to continue\n", 0x0E, 0x00);
        // For certain recoverable exceptions, try to continue
        if (int_no == 6) { // Invalid Opcode
            print_colored("[EXCEPTION] Invalid Opcode - skipping instruction\n", 0x0E, 0x00);
            frame->eip += 1; // Skip the bad instruction (naive approach)
            return;
        }
    }
    
    // If we reach here, it's a serious issue
    kernel_panic_annotated(
        exception_names[int_no],
        __FILE__,
        __LINE__,
        __FUNCTION__
    );
}

static void default_irq_handler(int int_no, struct interrupt_frame* frame, unsigned int error_code) {
    (void)frame;
    (void)error_code;
    if (int_no >= IRQ_TIMER && int_no <= IRQ_SECONDARY_HD) {
        pic_send_eoi(int_no - IRQ_TIMER);
    }
}

// =============================================================================
// COMMON INTERRUPT HANDLER
// =============================================================================

void interrupt_common_handler(int int_no, struct interrupt_frame* frame, unsigned int error_code) {
    interrupt_handler_t handler = interrupt_handlers[int_no];

    if (handler) {
        handler(frame, error_code);
        return;
    }

    if (int_no < 32) {
        default_exception_handler(int_no, frame, error_code);
    } else if (int_no >= IRQ_TIMER && int_no <= IRQ_SECONDARY_HD) {
        default_irq_handler(int_no, frame, error_code);
    }
}

// =============================================================================
// INTERRUPT INITIALIZATION
// =============================================================================

void interrupt_early_init(void) {
    irq_disable_safe();
    idt_clear();
    g_kernel_code_selector = cpu_read_cs();
    g_kernel_data_selector = cpu_read_ds();
    if (g_kernel_code_selector == 0 || g_kernel_data_selector == 0) {
        kernel_panic("Interrupt selectors not initialized");
    }
    interrupt_state = INTERRUPT_STATE_EARLY;
}

void interrupt_full_init(void) {
    if (interrupt_state == INTERRUPT_STATE_OFF) {
        interrupt_early_init();
    }

    interrupt_install_all_stubs();
    idt_load();
    pic_init();

    interrupt_state = INTERRUPT_STATE_READY;
    interrupts_initialized = true;
}
