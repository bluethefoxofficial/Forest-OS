#include "include/interrupt.h"
#include "include/screen.h"
#include "include/panic.h"
#include "include/system.h"
#include "include/timer.h"
#include "include/util.h"
#include "include/debuglog.h"
#include "include/syscall.h"
#include "include/tty.h"
#include "include/stacktrace.h"
#include "include/libc/stdio.h"
#include <stdint.h>

#if ARCH_64BIT
#define FRAME_IP(f)    ((f)->rip)
#define FRAME_FLAGS(f) ((f)->rflags)
#define FRAME_SP(f)    ((f)->rsp)
#else
#define FRAME_IP(f)    ((f)->eip)
#define FRAME_FLAGS(f) ((f)->eflags)
#define FRAME_SP(f)    ((f)->useresp)
#endif

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
static irq_handler_t interrupt_handlers[IDT_ENTRIES] = {0};
static void *interrupt_dev_ids[IDT_ENTRIES] = {0};

// External assembly interrupt stub table
extern uintptr_t interrupt_stub_table[];

// =============================================================================
// LOW LEVEL HELPERS  
// =============================================================================

static inline bool cpu_interrupt_flag(void) {
    unsigned long flags;
#if ARCH_64BIT
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
#else
    __asm__ __volatile__("pushf; pop %0" : "=r"(flags));
#endif
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

/* Legacy handler array for old-style handlers */
static legacy_interrupt_handler_t legacy_handlers[IDT_ENTRIES] = {0};

void interrupt_set_handler(uint8 int_num, interrupt_handler_t handler) {
    interrupt_handlers[int_num] = handler;
    legacy_handlers[int_num] = NULL;  /* Clear legacy handler if setting new style */
}

/* Register a legacy handler with old signature (struct interrupt_frame*, uint32 error_code) */
void interrupt_set_handler_legacy(uint8 int_num, legacy_interrupt_handler_t handler) {
    interrupt_handlers[int_num] = NULL;  /* Clear new-style handler */
    legacy_handlers[int_num] = handler;
}

void interrupt_clear_handler(uint8 int_num) {
    interrupt_handlers[int_num] = NULL;
    legacy_handlers[int_num] = NULL;
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
    unsigned long dr6;
    __asm__ volatile("mov %%dr6, %0" : "=r"(dr6));
    
    // Prevent infinite loops by limiting exception handling
    exception_count++;
    if (exception_count > 5) {
        print_colored("[CRITICAL] Too many debug exceptions, disabling handler\n", 0x0C, 0x00);
        return false; // Let it panic instead of infinite loop
    }
    
    // Clear all debug status and control registers to stop the cascade
    unsigned long zero_debug = 0;
    __asm__ volatile("mov %0, %%dr6" : : "r"(zero_debug));
    __asm__ volatile("mov %0, %%dr7" : : "r"(zero_debug));
    
    // Clear the Trap Flag (TF) in EFLAGS to stop single stepping
    FRAME_FLAGS(frame) &= ~0x100; // Clear TF bit
    
    // Also clear Resume Flag (RF) and other debug-related flags
    FRAME_FLAGS(frame) &= ~0x10000; // Clear RF bit
    
    print_colored("[DEBUG] Debug exception handled and debug state cleared\n", 0x0A, 0x00);
    
    return true; // Indicate we handled it
}

// Linux-style Invalid Opcode Handler
// Like Linux, we only handle actual exceptions - let CPU execute valid opcodes naturally
static bool handle_invalid_opcode(struct interrupt_frame* frame) {
    uint8* instruction = (uint8*)FRAME_IP(frame);
    uint8 opcode = *instruction;

    // Log to debug only (avoid print functions that could cause more exceptions)
    if (debuglog_is_ready()) {
        debuglog_write("[EXCEPTION] Invalid opcode at EIP: 0x");
        debuglog_write_hex((uint32)FRAME_IP(frame));
        debuglog_write(", opcode sequence: ");

        // Show sequence of up to 16 bytes as potential opcodes
        for (int i = 0; i < 16 && (uintptr_t)(instruction + i) < 0xFFFFFFFFFFFFFFF0; i++) {
            if (i > 0) debuglog_write(" ");
            debuglog_write_hex(instruction[i]);
        }
        debuglog_write("\n");
    }

    // Comprehensive opcode emulation for 64-bit compatibility
    bool emulated = false;
    uint32 skip_bytes = 1;

    // Check for two-byte opcodes first
    if (opcode == 0x0F) {
        if ((uintptr_t)instruction + 1 <= 0xFFFFFFFFFFFFFFF0) {
            uint8 second_byte = instruction[1];
            switch (second_byte) {
                case 0x05: // SYSCALL
                case 0x0B: // UD2
                case 0x1F: // NOP with multi-byte encoding
                case 0x77: // EMMS
                    skip_bytes = 2;
                    emulated = true;
                    break;
                default:
                    // Unknown 0F ?? opcode - skip the 0F prefix
                    skip_bytes = 1;
                    emulated = true;
                    break;
            }
        }
    }
    // REX prefixes
    else if (opcode >= 0x40 && opcode <= 0x4F) {
        skip_bytes = 1;
        emulated = true;
    }
    // Prefixes
    else if (opcode == 0x66 || opcode == 0x67) {
        skip_bytes = 1;
        emulated = true;
    }
    // Immediate operations with 32-bit immediates
    else if (opcode == 0x05 || opcode == 0x0D || opcode == 0x15 || opcode == 0x1D ||
             opcode == 0x25 || opcode == 0x2D || opcode == 0x35 || opcode == 0x3D) {
        skip_bytes = 5; // opcode + 4 bytes immediate
        emulated = true;
    }
    // Immediate operations with 8-bit immediates
    else if (opcode == 0x04 || opcode == 0x0C || opcode == 0x14 || opcode == 0x1C ||
             opcode == 0x24 || opcode == 0x2C || opcode == 0x34 || opcode == 0x3C) {
        skip_bytes = 2; // opcode + 1 byte immediate
        emulated = true;
    }
    // Absolute address operations (invalid in 64-bit)
    else if (opcode >= 0xA0 && opcode <= 0xA3) {
        skip_bytes = (opcode & 0x01) ? 5 : 9; // A0/A2 = 9 bytes, A1/A3 = 5 bytes
        emulated = true;
    }
    // Most other single-byte opcodes can be safely skipped
    else {
        skip_bytes = 1;
        emulated = true;
    }

    if (emulated) {
        // Successfully emulated - skip the instruction
        FRAME_IP(frame) += skip_bytes;

        if (debuglog_is_ready()) {
            debuglog_write("[KERNEL] Emulated invalid opcode, continuing at EIP: 0x");
            debuglog_write_hex((uint32)FRAME_IP(frame));
            debuglog_write("\n");
        }

        return true;
    }

    // Could not emulate - show crash screen
    uintptr_t cr2 = 0;
#if ARCH_64BIT
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
#endif

    tty_show_crash_screen("INVALID OPCODE EXCEPTION",
                         "Invalid/undefined instruction in kernel mode - cannot emulate",
                         FRAME_IP(frame), opcode, cr2);

    // Don't try to recover - halt the system
    while (1) {
        __asm__ __volatile__("hlt");
    }

    return false; // Won't reach here
}

// Default exception handler - Linux-style approach
static void default_exception_handler(int int_no, struct interrupt_frame* frame, unsigned int error_code) {
    // Get CR2 register
    uintptr_t cr2 = 0;
#if ARCH_64BIT
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
#else
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
#endif

    // Get current CR3 for debugging
    uint32 cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));

    // Log to debug first (if available)
    if (debuglog_is_ready()) {
        // Print exception name
        const char* exc_name = "Unknown";
        switch(int_no) {
            case 0: exc_name = "Divide Error"; break;
            case 1: exc_name = "Debug"; break;
            case 2: exc_name = "NMI"; break;
            case 3: exc_name = "Breakpoint"; break;
            case 4: exc_name = "Overflow"; break;
            case 5: exc_name = "Bound Range"; break;
            case 6: exc_name = "Invalid Opcode"; break;
            case 7: exc_name = "Device Not Available"; break;
            case 8: exc_name = "Double Fault"; break;
            case 9: exc_name = "Coproc Segment"; break;
            case 10: exc_name = "Invalid TSS"; break;
            case 11: exc_name = "Segment Not Present"; break;
            case 12: exc_name = "Stack Fault"; break;
            case 13: exc_name = "General Protection"; break;
            case 14: exc_name = "Page Fault"; break;
            case 16: exc_name = "x87 Fault"; break;
            case 17: exc_name = "Alignment Check"; break;
            case 18: exc_name = "Machine Check"; break;
            case 19: exc_name = "SIMD Fault"; break;
        }
        debuglog_write("[EXCEPTION] ");
        debuglog_write(exc_name);
        debuglog_write(" (#"); debuglog_write_dec((uint32)int_no); debuglog_write(")\n");
        debuglog_write("  error_code=0x"); debuglog_write_hex(error_code);
        debuglog_write(" eip=0x"); debuglog_write_hex((uint32)FRAME_IP(frame));
        debuglog_write(" cr2=0x"); debuglog_write_hex((uint32)cr2);
        debuglog_write(" cr3=0x"); debuglog_write_hex(cr3);
        debuglog_write("\n");
        
        // Page fault specific details
        if (int_no == 14) {
            debuglog_write("  PageFault: ");
            if (error_code & 1) debuglog_write("Present ");
            if (error_code & 2) debuglog_write("Write ");
            else debuglog_write("Read ");
            if (error_code & 4) debuglog_write("User ");
            if (error_code & 8) debuglog_write("ReservedWrite ");
            if (error_code & 16) debuglog_write("InstrFetch ");
            debuglog_write("\n");
        }
        
        // GPF specific details  
        if (int_no == 13) {
            debuglog_write("  GPF: selector_index=");
            debuglog_write_hex(error_code & 0xFFF8);
            if (error_code & 1) debuglog_write(" Ext ");
            debuglog_write("\n");
        }

        // Print detailed register dump from exception frame
        debuglog_write("Detailed exception frame dump:\n");
        debuglog_write("  EIP=");
        debuglog_write_hex((uint32)FRAME_IP(frame));
        debuglog_write(" CS=");
        debuglog_write_hex(frame->cs);
        debuglog_write(" EFL=");
        debuglog_write_hex(FRAME_FLAGS(frame));
        debuglog_write(" ESP=");
        debuglog_write_hex(FRAME_SP(frame));
        debuglog_write(" SS=");
        debuglog_write_hex(frame->ss);
        debuglog_write("\n");
        // Note: Register dump requires struct member access - disabled for now
        debuglog_write("\n");

        // Print stack trace for debugging
        debuglog_write("Stack trace:\n");
        stacktrace_print_current();
    }

    // Try to handle specific exceptions
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

    // Show crash screen on framebuffer
    char title[64];
    char message[128];

    if (int_no == EXCEPTION_INVALID_OPCODE) {
        sprintf(title, "INVALID OPCODE EXCEPTION (#%d)", int_no);
        sprintf(message, "Invalid/undefined instruction encountered");
    } else if (int_no == 13) { // General Protection Fault
        sprintf(title, "GENERAL PROTECTION FAULT (#%d)", int_no);
        sprintf(message, "Segment or privilege violation - check GDT/IDT setup");
    } else {
        sprintf(title, "CPU EXCEPTION (#%d)", int_no);
        sprintf(message, "Unhandled processor exception");
    }

    tty_show_crash_screen(title, message, FRAME_IP(frame), error_code, cr2);

    // Infinite loop - don't continue execution
    while (1) {
        __asm__ __volatile__("hlt");
    }
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
    /* First check for new-style handlers */
    interrupt_handler_t handler = interrupt_handlers[int_no];
    if (handler) {
        /* Build a temporary interrupt_context for the new handler signature */
        struct interrupt_context ctx = {0};
        ctx.vector = int_no;
    #if ARCH_64BIT
        ctx.frame.rip = frame->rip;
        ctx.frame.cs = frame->cs;
        ctx.frame.rflags = frame->rflags;
        ctx.frame.rsp = frame->rsp;
        ctx.frame.ss = frame->ss;
    #else
        ctx.frame.eip = frame->eip;
        ctx.frame.cs = frame->cs;
        ctx.frame.eflags = frame->eflags;
        ctx.frame.useresp = frame->useresp;
        ctx.frame.ss = frame->ss;
    #endif
        ctx.error_code = error_code;
        /* Note: useresp/ss are only valid on privilege changes */
        ctx.timestamp = 0;  /* TODO: Use TSC if available */

        handler(int_no, interrupt_dev_ids[int_no], &ctx);
        return;
    }

    /* Check for legacy handlers */
    legacy_interrupt_handler_t legacy_handler = legacy_handlers[int_no];
    if (legacy_handler) {
        legacy_handler(frame, error_code);
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
// 64-BIT DISPATCHER (USED BY NEW ASM STUBS)
// =============================================================================

void interrupt_dispatch_handler(struct interrupt_context *ctx) {
    if (!ctx) {
        return;
    }

#if ARCH_64BIT
    /* Fast path for syscalls coming from the 0x80 interrupt gate */
    if (ctx->vector == SYSCALL_VECTOR) {
        syscall_frame_t frame = {0};
        frame.rdi = ctx->regs.rdi;
        frame.rsi = ctx->regs.rsi;
        frame.rbp = ctx->regs.rbp;
        frame.rbx = ctx->regs.rbx;
        frame.rdx = ctx->regs.rdx;
        frame.rcx = ctx->regs.rcx;
        frame.rax = ctx->regs.rax;

        syscall_handle(&frame);

        /* Propagate return value back to userland */
        ctx->regs.rax = frame.rax;
        return;
    }
#endif

    /* Fallback to the legacy/common handler path */
    interrupt_common_handler((int)ctx->vector, &ctx->frame, (unsigned int)ctx->error_code);
}

// =============================================================================
// IDT MANAGEMENT
// =============================================================================

void idt_set_gate(uint8 num, uintptr_t handler, uint8 flags) {
#if ARCH_64BIT
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = g_kernel_code_selector;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (uint32_t)((uint64_t)handler >> 32);
    idt[num].reserved = 0;
#else
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = g_kernel_code_selector;
    idt[num].reserved = 0;
    idt[num].flags = flags;
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
#endif
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
    unsigned long zero = 0;
    __asm__ volatile("mov %0, %%dr0" : : "r"(zero) : "memory");
    __asm__ volatile("mov %0, %%dr1" : : "r"(zero) : "memory");
    __asm__ volatile("mov %0, %%dr2" : : "r"(zero) : "memory");
    __asm__ volatile("mov %0, %%dr3" : : "r"(zero) : "memory");
    __asm__ volatile("mov %0, %%dr6" : : "r"(zero) : "memory");
    __asm__ volatile("mov %0, %%dr7" : : "r"(zero) : "memory");
    
    // Initialize IDT descriptor
    idtr.limit = sizeof(idt) - 1;
    idtr.base =
#if ARCH_64BIT
        (uint64_t)&idt;
#else
        (uint32)&idt;
#endif
    
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

// =============================================================================
// GLOBAL INTERRUPT MANAGER INSTANCE
// =============================================================================

/* Global interrupt manager - required by the advanced interrupt subsystem */
struct interrupt_manager interrupt_mgr = {
    .total_interrupts = ATOMIC64_INIT(0),
    .spurious_interrupts = ATOMIC64_INIT(0),
    .unhandled_interrupts = ATOMIC64_INIT(0),
    .vector_lock = SPINLOCK_INIT("vector_lock"),
    .early_init_done = ATOMIC_INIT(0),
    .full_init_done = ATOMIC_INIT(0),
    .controllers_ready = ATOMIC_INIT(0)
};

/* Global priority manager - required by interrupt priority subsystem */
struct priority_manager priority_mgr = {
    .global_priority_lock = SPINLOCK_INIT("priority_lock"),
    .initialized = false
};

/* Global interrupt state variables */
volatile bool interrupt_controllers_ready = false;
volatile bool timer_subsystem_ready = false;

/* Simple IRQ management for basic drivers */
int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
                const char *name, void *dev_id)
{
    if (irq >= IDT_ENTRIES || interrupt_handlers[irq] != NULL) {
        return -1; // Already registered or invalid IRQ
    }
    interrupt_handlers[irq] = handler;
    interrupt_dev_ids[irq] = dev_id;
    return 0;
}

int request_irq_advanced(unsigned int irq, irq_handler_t handler,
                        unsigned long flags, const char *name, void *dev_id)
{
    return request_irq(irq, handler, flags, name, dev_id);
}

void free_irq_advanced(unsigned int irq, void *dev_id)
{
    if (irq < IDT_ENTRIES) {
        interrupt_handlers[irq] = NULL;
        interrupt_dev_ids[irq] = NULL;
    }
}
