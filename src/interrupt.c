#include "include/interrupt.h"
#include "include/screen.h"
#include "include/panic.h"
#include "include/system.h"
#include "include/timer.h"
#include "include/util.h"
#include "include/debuglog.h"
#include <stdint.h>

// =============================================================================
// GLOBAL STATE
// =============================================================================

typedef enum interrupt_state {
    INTERRUPT_STATE_UNINITIALIZED = 0,
    INTERRUPT_STATE_EARLY = 1,
    INTERRUPT_STATE_FULL = 2
} interrupt_state_t;

static volatile interrupt_state_t g_interrupt_state = INTERRUPT_STATE_UNINITIALIZED;
volatile bool interrupts_initialized = false;

// IDT structures
static idt_entry_t idt[IDT_ENTRIES];
static idtr_t idtr;

// Global selectors
uint16 g_kernel_code_selector = 0x08;
uint16 g_kernel_data_selector = 0x10;

// Handler table
static interrupt_handler_t interrupt_handlers[IDT_ENTRIES] = {0};

// External assembly interrupt stub table
extern uint32 interrupt_stub_table[];

// =============================================================================
// LOW LEVEL HELPERS  
// =============================================================================

static inline bool cpu_interrupt_flag(void) {
    uint32 flags;
    __asm__ __volatile__("pushf; pop %0" : "=r"(flags));
    return (flags & 0x200) != 0;
}

// io_wait is already declared in system.h

// =============================================================================
// SAFE INTERRUPT CONTROL FUNCTIONS
// =============================================================================

bool irq_save_and_disable_safe(void) {
    if (g_interrupt_state == INTERRUPT_STATE_UNINITIALIZED) {
        return false;
    }
    
    bool was_enabled = cpu_interrupt_flag();
    __asm__ __volatile__("cli");
    return was_enabled;
}

void irq_restore_safe(bool interrupts_enabled) {
    if (g_interrupt_state == INTERRUPT_STATE_UNINITIALIZED) {
        return;
    }
    
    if (interrupts_enabled) {
        __asm__ __volatile__("sti");
    }
}

void irq_enable_safe(void) {
    if (g_interrupt_state >= INTERRUPT_STATE_EARLY) {
        __asm__ __volatile__("sti");
    }
}

void irq_disable_safe(void) {
    if (g_interrupt_state >= INTERRUPT_STATE_EARLY) {
        __asm__ __volatile__("cli");
    }
}

bool irq_are_enabled(void) {
    return cpu_interrupt_flag();
}

// =============================================================================
// PIC MANAGEMENT
// =============================================================================

void pic_init(void) {
    // ICW1: Start initialization sequence
    outportb(PIC1_COMMAND, 0x11);
    io_wait();
    outportb(PIC2_COMMAND, 0x11);
    io_wait();

    // ICW2: Set vector offsets
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

// Forward declaration
static bool handle_invalid_opcode(struct interrupt_frame* frame);
static bool handle_debug_exception(struct interrupt_frame* frame);

// Debug Exception Handler (Vector 1)
// Handle debug exceptions caused by debug registers, single stepping, etc.
static bool handle_debug_exception(struct interrupt_frame* frame) {
    static int exception_count = 0;
    uint32_t dr6;
    __asm__ volatile("mov %%dr6, %0" : "=r"(dr6));
    
    // Prevent infinite loops by limiting exception handling
    exception_count++;
    if (exception_count > 5) {
        print_colored("[CRITICAL] Too many debug exceptions, disabling handler\n", 0x0C, 0x00);
        return false; // Let it panic instead of infinite loop
    }
    
    // Clear all debug status and control registers to stop the cascade
    __asm__ volatile("mov %0, %%dr6" : : "r"(0));
    __asm__ volatile("mov %0, %%dr7" : : "r"(0));
    
    // Clear the Trap Flag (TF) in EFLAGS to stop single stepping
    frame->eflags &= ~0x100; // Clear TF bit
    
    // Also clear Resume Flag (RF) and other debug-related flags
    frame->eflags &= ~0x10000; // Clear RF bit
    
    print_colored("[DEBUG] Debug exception handled and debug state cleared\n", 0x0A, 0x00);
    
    return true; // Indicate we handled it
}

// Linux-style Invalid Opcode Handler
// Like Linux, we only handle actual exceptions - let CPU execute valid opcodes naturally
static bool handle_invalid_opcode(struct interrupt_frame* frame) {
    uint8* instruction = (uint8*)frame->eip;
    uint8 opcode = *instruction;
    
    print_colored("[EXCEPTION] Invalid/Undefined Instruction (#UD) at EIP: 0x", 0x0C, 0x00);
    print_hex(frame->eip);
    print(", opcode: 0x");
    print_hex(opcode);
    print("\n");
    
    // In Linux, this would:
    // 1. Send SIGILL to userspace processes  
    // 2. Emulate only specific legacy instructions if needed
    // 3. Panic if in kernel mode with unhandled instruction
    
    // For now, just try to skip the instruction and continue
    // In a proper OS, we'd send SIGILL to the process
    uint32 skip_length = 1; // Default: skip 1 byte
    
    // Handle known multi-byte patterns
    if (opcode == 0x0F) {
        skip_length = 2; // Two-byte opcode
        print_colored("[KERNEL] Two-byte opcode detected\n", 0x0E, 0x00);
    }
    
    frame->eip += skip_length;
    print_colored("[KERNEL] Skipped invalid instruction, continuing at EIP: 0x", 0x0A, 0x00);
    print_hex(frame->eip);
    print("\n");
    
    return true; // Always attempt recovery for now
}

// Default exception handler - Linux-style approach
static void default_exception_handler(int int_no, struct interrupt_frame* frame, unsigned int error_code) {
    if (debuglog_is_ready()) {
        uint32 cr2 = 0;
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
        debuglog_write("[EXCEPTION] vector=");
        debuglog_write_dec((uint32)int_no);
        debuglog_write(" err=");
        debuglog_write_hex(error_code);
        debuglog_write(" eip=");
        debuglog_write_hex(frame->eip);
        debuglog_write(" cr2=");
        debuglog_write_hex(cr2);
        debuglog_write("\n");
    }

    switch (int_no) {
        case 1: // EXCEPTION_DEBUG
            if (handle_debug_exception(frame)) {
                return; // Successfully handled
            }
            break;
        case EXCEPTION_INVALID_OPCODE:
            if (handle_invalid_opcode(frame)) {
                return; // Successfully handled
            }
            break;
        default:
            break;
    }
    
    // Unhandled exception - panic with details  
    print_colored("[PANIC] Unhandled exception: ", 0x0C, 0x00);
    print_dec(int_no);
    print(" at EIP: ");
    print_hex(frame->eip);
    print(", error: ");
    print_hex(error_code);
    print("\n");
    kernel_panic("Unhandled CPU exception");
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
    } else {
        print_colored("[WARNING] Unhandled interrupt: ", 0x0E, 0x00);
        print_dec(int_no);
        print("\n");
    }
}

// =============================================================================
// IDT MANAGEMENT
// =============================================================================

void idt_set_gate(uint8 num, uint32 handler, uint8 flags) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = g_kernel_code_selector;
    idt[num].reserved = 0;
    idt[num].flags = flags;
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
}

// =============================================================================
// INTERRUPT INITIALIZATION
// =============================================================================

void interrupt_early_init(void) {
    if (g_interrupt_state != INTERRUPT_STATE_UNINITIALIZED) {
        return; // Already initialized
    }
    
    print_colored("[INIT] Setting up interrupt descriptor table...\n", 0x0A, 0x00);
    
    // Clear debug registers to prevent spurious debug exceptions
    uint32 zero = 0;
    __asm__ volatile("mov %0, %%dr0" : : "r"(zero) : "memory");
    __asm__ volatile("mov %0, %%dr1" : : "r"(zero) : "memory");
    __asm__ volatile("mov %0, %%dr2" : : "r"(zero) : "memory");
    __asm__ volatile("mov %0, %%dr3" : : "r"(zero) : "memory");
    __asm__ volatile("mov %0, %%dr6" : : "r"(zero) : "memory");
    __asm__ volatile("mov %0, %%dr7" : : "r"(zero) : "memory");
    
    // Initialize IDT descriptor
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32)&idt;
    
    // Clear IDT
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0);
    }
    
    // Use the interrupt stub table from interrupt_stubs.asm
    
    // Set up exception handlers (0-31) 
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, interrupt_stub_table[i], IDT_GATE_INTERRUPT32);
    }

    // Load IDT
    __asm__ __volatile__("lidt %0" :: "m"(idtr));
    
    g_interrupt_state = INTERRUPT_STATE_EARLY;
    print_colored("[INIT] Basic interrupt handling enabled\n", 0x0A, 0x00);
}

void interrupt_full_init(void) {
    if (g_interrupt_state != INTERRUPT_STATE_EARLY) {
        interrupt_early_init();
    }
    
    print_colored("[INIT] Setting up PIC and IRQ handlers...\n", 0x0A, 0x00);
    
    // Initialize PIC
    pic_init();
    
    // Set up IRQ handlers (32-47) using the same stub table
    for (int i = 0; i < 16; i++) {
        idt_set_gate(IRQ_TIMER + i, interrupt_stub_table[IRQ_TIMER + i], IDT_GATE_INTERRUPT32);
    }

    // Set up system call handler (0x80)
    idt_set_gate(0x80, interrupt_stub_table[0x80], IDT_GATE_USER_INT);

    g_interrupt_state = INTERRUPT_STATE_FULL;
    interrupts_initialized = true;
    
    print_colored("[INIT] Full interrupt system initialized\n", 0x0A, 0x00);
}
