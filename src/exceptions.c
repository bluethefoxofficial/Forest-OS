/*
 * Comprehensive CPU Exception Handlers for Forest OS
 * Supports x86-32, x86-64, and UEFI environments
 * Integrates with advanced memory management system
 */

#include "interrupt.h"
#include "signal.h"
#include "cpu_constants.h"
#include "cpu_ops.h"
#include "mm.h"
#include "debuglog.h"
#include "panic.h"
#include "string.h"
#include "task.h"
#include "stacktrace.h"
#include "screen.h"

#if ARCH_64BIT
#define FRAME_IP(frame)    ((frame).rip)
#define FRAME_SP(frame)    ((frame).rsp)
#define FRAME_FLAGS(frame) ((frame).rflags)
#else
#define FRAME_IP(frame)    ((frame).eip)
#define FRAME_SP(frame)    ((frame).useresp)
#define FRAME_FLAGS(frame) ((frame).eflags)
#endif

/* Exception information structure */
struct exception_info {
    const char *name;
    const char *description;
    bool has_error_code;
    bool recoverable;
    bool user_mode_allowed;
};

/* Exception information table */
static const struct exception_info exception_table[32] = {
    [0]  = {"Division Error", "Division by zero or result too large", false, false, true},
    [1]  = {"Debug", "Debug exception from breakpoints or single-step", false, true, true},
    [2]  = {"Non-Maskable Interrupt", "Hardware NMI signal", false, true, false},
    [3]  = {"Breakpoint", "INT3 instruction executed", false, true, true},
    [4]  = {"Overflow", "INTO instruction with OF=1", false, true, true},
    [5]  = {"Bound Range Exceeded", "BOUND instruction limit exceeded", false, false, true},
    [6]  = {"Invalid Opcode", "Undefined or invalid instruction", false, false, true},
    [7]  = {"Device Not Available", "FPU/MMX/SSE instruction without device", false, true, true},
    [8]  = {"Double Fault", "Exception during exception handling", true, false, false},
    [9]  = {"Coprocessor Segment Overrun", "Legacy FPU segment violation", false, false, true},
    [10] = {"Invalid TSS", "Task State Segment error", true, false, false},
    [11] = {"Segment Not Present", "Segment not present in memory", true, false, true},
    [12] = {"Stack-Segment Fault", "Stack segment limit exceeded", true, false, true},
    [13] = {"General Protection", "General protection violation", true, false, true},
    [14] = {"Page Fault", "Virtual memory access violation", true, true, true},
    [15] = {"Reserved", "Reserved by Intel", false, false, false},
    [16] = {"x87 FPU Error", "Floating point unit error", false, true, true},
    [17] = {"Alignment Check", "Unaligned memory access", true, true, true},
    [18] = {"Machine Check", "Hardware machine check error", false, false, false},
    [19] = {"SIMD Exception", "SSE/AVX floating point exception", false, true, true},
    [20] = {"Virtualization", "Virtualization technology exception", false, true, false},
    [21] = {"Control Protection", "CET control flow violation", true, false, true},
    [22] = {"Reserved", "Reserved by Intel", false, false, false},
    [23] = {"Reserved", "Reserved by Intel", false, false, false},
    [24] = {"Reserved", "Reserved by Intel", false, false, false},
    [25] = {"Reserved", "Reserved by Intel", false, false, false},
    [26] = {"Reserved", "Reserved by Intel", false, false, false},
    [27] = {"Reserved", "Reserved by Intel", false, false, false},
    [28] = {"Hypervisor Injection", "Hypervisor injected exception", false, true, false},
    [29] = {"VMM Communication", "VMM communication exception", true, true, false},
    [30] = {"Security", "Security violation exception", true, false, true},
    [31] = {"Reserved", "Reserved by Intel", false, false, false}
};

/* Exception statistics */
static struct {
    atomic64_t count;
    uint64_t last_timestamp;
    uint32_t last_pid;
} exception_stats[32];

/* Forward declarations */
static irq_return_t handle_division_error(struct interrupt_context *ctx);
static irq_return_t handle_debug_exception(struct interrupt_context *ctx);
static irq_return_t handle_nmi(struct interrupt_context *ctx);
static irq_return_t handle_breakpoint(struct interrupt_context *ctx);
static irq_return_t handle_overflow(struct interrupt_context *ctx);
static irq_return_t handle_bound_range_exceeded(struct interrupt_context *ctx);
static irq_return_t handle_invalid_opcode(struct interrupt_context *ctx);
static irq_return_t handle_device_not_available(struct interrupt_context *ctx);
static irq_return_t handle_double_fault_internal(struct interrupt_context *ctx);
static irq_return_t handle_invalid_tss(struct interrupt_context *ctx);
static irq_return_t handle_segment_not_present(struct interrupt_context *ctx);
static irq_return_t handle_stack_fault(struct interrupt_context *ctx);
static irq_return_t handle_general_protection(struct interrupt_context *ctx);
static irq_return_t handle_page_fault_internal(struct interrupt_context *ctx);
static irq_return_t handle_fpu_error(struct interrupt_context *ctx);
static irq_return_t handle_alignment_check(struct interrupt_context *ctx);
static irq_return_t handle_machine_check(struct interrupt_context *ctx);
static irq_return_t handle_simd_exception(struct interrupt_context *ctx);
static irq_return_t handle_virtualization_exception(struct interrupt_context *ctx);
static irq_return_t handle_control_protection(struct interrupt_context *ctx);
static irq_return_t handle_security_exception(struct interrupt_context *ctx);

static void dump_exception_context(struct interrupt_context *ctx);
static void update_exception_stats(int exception, struct interrupt_context *ctx);
static bool is_user_mode_exception(struct interrupt_context *ctx);
static void handle_fatal_exception(int exception, struct interrupt_context *ctx);

/* Public wrapper functions for interrupt.h declarations */
void handle_double_fault(struct interrupt_context *ctx) {
    handle_double_fault_internal(ctx);
}

void handle_page_fault(struct interrupt_context *ctx) {
    handle_page_fault_internal(ctx);
}

/* Exception handler function table */
static irq_return_t (*exception_handlers[32])(struct interrupt_context *ctx) = {
    [0]  = handle_division_error,
    [1]  = handle_debug_exception,
    [2]  = handle_nmi,
    [3]  = handle_breakpoint,
    [4]  = handle_overflow,
    [5]  = handle_bound_range_exceeded,
    [6]  = handle_invalid_opcode,
    [7]  = handle_device_not_available,
    [8]  = handle_double_fault_internal,
    [9]  = NULL, /* Coprocessor segment overrun is legacy */
    [10] = handle_invalid_tss,
    [11] = handle_segment_not_present,
    [12] = handle_stack_fault,
    [13] = handle_general_protection,
    [14] = handle_page_fault_internal,
    [15] = NULL, /* Reserved */
    [16] = handle_fpu_error,
    [17] = handle_alignment_check,
    [18] = handle_machine_check,
    [19] = handle_simd_exception,
    [20] = handle_virtualization_exception,
    [21] = handle_control_protection,
    [22] = NULL, /* Reserved */
    [23] = NULL, /* Reserved */
    [24] = NULL, /* Reserved */
    [25] = NULL, /* Reserved */
    [26] = NULL, /* Reserved */
    [27] = NULL, /* Reserved */
    [28] = NULL, /* Hypervisor injection */
    [29] = NULL, /* VMM communication */
    [30] = handle_security_exception,
    [31] = NULL  /* Reserved */
};

/*
 * Initialize exception handling subsystem
 */
int exception_init(void)
{
    int i;
    
    debuglog_printf("Exception: Initializing exception handling subsystem\n");
    
    /* Clear statistics */
    for (i = 0; i < 32; i++) {
        atomic64_set(&exception_stats[i].count, 0);
        exception_stats[i].last_timestamp = 0;
        exception_stats[i].last_pid = 0;
    }
    
    /* Register exception handlers with IDT */
    for (i = 0; i < 32; i++) {
        if (exception_handlers[i]) {
            idt_register_handler(i, (interrupt_handler_t)exception_handlers[i], NULL);
        }
    }
    
    debuglog_printf("Exception: Subsystem initialized\n");
    return 0;
}

/*
 * Main exception dispatcher called from assembly stubs
 */
void exception_dispatcher(struct interrupt_context *ctx)
{
    int exception = (int)ctx->vector;
    irq_return_t result;
    
    if (exception >= 32) {
        debuglog_printf("Exception: Invalid exception number %d\n", exception);
        return;
    }
    
    /* Update statistics */
    update_exception_stats(exception, ctx);
    
    /* Call specific handler */
    if (exception_handlers[exception]) {
        result = exception_handlers[exception](ctx);
        
        if (result == IRQ_HANDLED) {
            return;
        }
    }
    
    /* Unhandled or fatal exception */
    debuglog_printf("Exception: Unhandled exception %d (%s)\n", 
                exception, exception_table[exception].name);
    dump_exception_context(ctx);
    
    if (!exception_table[exception].recoverable || is_user_mode_exception(ctx)) {
        handle_fatal_exception(exception, ctx);
    }
}

/*
 * Division Error (#DE) - Exception 0
 */
static irq_return_t handle_division_error(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Division error at %p\n", (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    if (is_user_mode_exception(ctx)) {
        /* Terminate user process */
        debuglog_printf("Exception: Terminating user process due to division error\n");
        // task_terminate_current(SIGFPE); // TODO: implement task management
        return IRQ_HANDLED;
    }
    
    /* Fatal in kernel mode */
    dump_exception_context(ctx);
    panic("Division error in kernel mode");
    return IRQ_NONE;
}

/*
 * Debug Exception (#DB) - Exception 1
 */
static irq_return_t handle_debug_exception(struct interrupt_context *ctx)
{
    uint64_t dr6 = read_dr6();
    
    debuglog_printf("Exception: Debug exception (DR6=0x%lx) at %p\n", 
                dr6, (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    /* Handle single-step */
    if (dr6 & (1 << 14)) {
        debuglog_printf("Debug: Single-step debug trap\n");
        /* Clear single-step flag */
        FRAME_FLAGS(ctx->frame) &= ~(1UL << 8);
    }
    
    /* Handle breakpoints */
    if (dr6 & 0x0F) {
        debuglog_printf("Debug: Hardware breakpoint hit (mask=0x%lx)\n", dr6 & 0x0F);
    }
    
    /* Clear DR6 */
    write_dr6(0);
    
    return IRQ_HANDLED;
}

/*
 * Non-Maskable Interrupt - Exception 2
 */
static irq_return_t handle_nmi(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: NMI received at %p\n", (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    /* Check for memory parity errors */
    uint8_t status_b = inb(0x61);
    if (status_b & 0x80) {
        debuglog_printf("NMI: Memory parity error detected\n");
        /* Could attempt memory error recovery here */
    }
    
    if (status_b & 0x40) {
        debuglog_printf("NMI: Channel check error detected\n");
    }
    
    /* Handle watchdog timer NMI */
    /* This would integrate with the watchdog subsystem */
    
    return IRQ_HANDLED;
}

/*
 * Breakpoint (#BP) - Exception 3
 */
static irq_return_t handle_breakpoint(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Breakpoint at %p\n", (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    /* Adjust instruction pointer to point to INT3 instruction */
    FRAME_IP(ctx->frame)--;
    
    if (is_user_mode_exception(ctx)) {
        /* Send SIGTRAP to user process */
        // task_send_signal(task_get_current(), SIGTRAP); // TODO
        return IRQ_HANDLED;
    }
    
    /* Kernel debugger integration point */
    debuglog_printf("Kernel: Breakpoint in kernel mode\n");
    dump_exception_context(ctx);
    
    return IRQ_HANDLED;
}

/*
 * Overflow (#OF) - Exception 4
 */
static irq_return_t handle_overflow(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Overflow at %p\n", (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGFPE); // TODO
        return IRQ_HANDLED;
    }
    
    dump_exception_context(ctx);
    panic("Overflow exception in kernel mode");
    return IRQ_NONE;
}

/*
 * Bound Range Exceeded (#BR) - Exception 5
 */
static irq_return_t handle_bound_range_exceeded(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Bound range exceeded at %p\n", (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGSEGV); // TODO
        return IRQ_HANDLED;
    }
    
    dump_exception_context(ctx);
    panic("Bound range exceeded in kernel mode");
    return IRQ_NONE;
}

/*
 * Invalid Opcode (#UD) - Exception 6
 */
static irq_return_t handle_invalid_opcode(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Invalid opcode at %p\n", (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    /* Could attempt to emulate instruction here */
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGILL); // TODO
        return IRQ_HANDLED;
    }
    
    dump_exception_context(ctx);
    panic("Invalid opcode in kernel mode");
    return IRQ_NONE;
}

/*
 * Device Not Available (#NM) - Exception 7
 */
static irq_return_t handle_device_not_available(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Device not available at %p\n", (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    /* Enable FPU if it was disabled */
    uint64_t cr0 = read_cr0();
    if (cr0 & CR0_TS) {
        write_cr0(cr0 & ~CR0_TS);
        debuglog_printf("FPU: Enabled FPU after task switch\n");
        return IRQ_HANDLED;
    }
    
    if (cr0 & CR0_EM) {
        debuglog_printf("FPU: FPU emulation required\n");
        /* Could implement software emulation */
    }
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGFPE); // TODO
        return IRQ_HANDLED;
    }
    
    dump_exception_context(ctx);
    panic("Device not available in kernel mode");
    return IRQ_NONE;
}

/*
 * Double Fault (#DF) - Exception 8
 * This is critical - usually indicates stack corruption or ISR problems
 */
static irq_return_t handle_double_fault_internal(struct interrupt_context *ctx)
{
    /* Double fault is always fatal */
    debuglog_printf("CRITICAL: Double fault detected!\n");
    debuglog_printf("Error code: 0x%lx\n", ctx->frame.error_code);
    
    dump_exception_context(ctx);
    
    /* Attempt to provide useful information */
    debuglog_printf("Double fault analysis:\n");
    debuglog_printf("  - Possible stack overflow\n");
    debuglog_printf("  - Corrupted IDT or exception handlers\n");
    debuglog_printf("  - Nested exceptions\n");
    
    panic("Double fault - system halted");
    return IRQ_NONE;
}

/*
 * Invalid TSS (#TS) - Exception 10
 */
static irq_return_t handle_invalid_tss(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Invalid TSS (error=0x%lx) at %p\n", 
                ctx->frame.error_code, (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    dump_exception_context(ctx);
    panic("Invalid TSS");
    return IRQ_NONE;
}

/*
 * Segment Not Present (#NP) - Exception 11
 */
static irq_return_t handle_segment_not_present(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Segment not present (error=0x%lx) at %p\n", 
                ctx->frame.error_code, (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGSEGV); // TODO
        return IRQ_HANDLED;
    }
    
    dump_exception_context(ctx);
    panic("Segment not present in kernel mode");
    return IRQ_NONE;
}

/*
 * Stack-Segment Fault (#SS) - Exception 12
 */
static irq_return_t handle_stack_fault(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Stack fault (error=0x%lx) at %p\n", 
                ctx->frame.error_code, (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGSEGV); // TODO
        return IRQ_HANDLED;
    }
    
    dump_exception_context(ctx);
    panic("Stack fault in kernel mode");
    return IRQ_NONE;
}

/*
 * General Protection Fault (#GP) - Exception 13
 */
static irq_return_t handle_general_protection(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: General protection fault (error=0x%lx) at %p\n", 
                ctx->frame.error_code, (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    /* Decode error code */
    if (ctx->frame.error_code != 0) {
        bool external = ctx->frame.error_code & 1;
        int table = (ctx->frame.error_code >> 1) & 3;
        int index = (ctx->frame.error_code >> 3) & 0x1FFF;
        
        debuglog_printf("GP fault: %s, table %d, index %d\n", 
                   external ? "external" : "internal", table, index);
    }
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGSEGV); // TODO
        return IRQ_HANDLED;
    }
    
    dump_exception_context(ctx);
    panic("General protection fault in kernel mode");
    return IRQ_NONE;
}

/*
 * Page Fault (#PF) - Exception 14
 * Integrates with the memory management system
 */
static irq_return_t handle_page_fault_internal(struct interrupt_context *ctx)
{
    uint64_t fault_address = cpu_read_cr2();
    uint64_t error_code = ctx->frame.error_code;
    
    bool present = error_code & 1;
    bool write = error_code & 2;
    bool user = error_code & 4;
    bool reserved = error_code & 8;
    bool instruction = error_code & 16;
    
    debuglog_printf("Page fault: addr=0x%lx, %s %s %s at %p\n",
                fault_address,
                present ? "protection" : "not-present",
                write ? "write" : "read", 
                user ? "user" : "kernel",
                (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    /* Try to handle with memory management system */
    int mm_result = mm_handle_page_fault(&ctx->frame, (unsigned long)fault_address, (unsigned long)error_code);
    
    if (mm_result == 0) {
        /* Successfully handled by MM system */
        return IRQ_HANDLED;
    }
    
    if (user && is_user_mode_exception(ctx)) {
        /* User mode page fault that couldn't be handled */
        debuglog_printf("Page fault: Terminating user process\n");
        print_colored("[TASK] User page fault, terminating task.\n", 0x0C, 0x00);
        print("  addr=0x"); print_hex((uint32)fault_address);
        print(" ip=0x"); print_hex((uint32)FRAME_IP(ctx->frame));
        print(" err=0x"); print_hex((uint32)error_code);
        print("\n");
        if (current_task) {
            current_task->state = TASK_STATE_TERMINATED;
            current_task->pending_signals |= SIGKILL;
        }
        task_schedule();
        return IRQ_HANDLED;
    }
    
    /* Kernel page fault - this is serious */
    debuglog_printf("CRITICAL: Kernel page fault at 0x%lx\n", fault_address);
    dump_exception_context(ctx);
    
    /* Try to provide useful debugging information */
    if (fault_address < 0x1000) {
        debuglog_printf("Page fault: NULL pointer dereference\n");
    } else if (fault_address >= KERNEL_BASE) {
        debuglog_printf("Page fault: Kernel address space violation\n");
    }
    
    panic("Unhandled kernel page fault");
    return IRQ_NONE;
}

/*
 * x87 FPU Error (#MF) - Exception 16
 */
static irq_return_t handle_fpu_error(struct interrupt_context *ctx)
{
    uint16_t fpu_status;
    
    /* Get FPU status word */
    asm volatile("fnstsw %0" : "=m" (fpu_status));
    
    debuglog_printf("Exception: FPU error (status=0x%04x) at %p\n", 
                fpu_status, (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    /* Clear FPU exception */
    asm volatile("fnclex");
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGFPE); // TODO
        return IRQ_HANDLED;
    }
    
    return IRQ_HANDLED;
}

/*
 * Alignment Check (#AC) - Exception 17
 */
static irq_return_t handle_alignment_check(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Alignment check at %p\n", (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGBUS); // TODO
        return IRQ_HANDLED;
    }
    
    dump_exception_context(ctx);
    panic("Alignment check in kernel mode");
    return IRQ_NONE;
}

/*
 * Machine Check (#MC) - Exception 18
 */
static irq_return_t handle_machine_check(struct interrupt_context *ctx)
{
    debuglog_printf("CRITICAL: Machine check exception at %p\n", (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    /* Read machine check registers if available */
    if (0) { // TODO: cpu_has_feature(CPU_FEATURE_MCE)
        /* Could read MCG_STATUS, MCi_STATUS, etc. */
        debuglog_printf("Machine check: Reading MCE registers...\n");
    }
    
    dump_exception_context(ctx);
    panic("Machine check exception - hardware error");
    return IRQ_NONE;
}

/*
 * SIMD Exception (#XM/#XF) - Exception 19
 */
static irq_return_t handle_simd_exception(struct interrupt_context *ctx)
{
    uint32_t mxcsr;
    
    /* Get MXCSR register */
    asm volatile("stmxcsr %0" : "=m" (mxcsr));
    
    debuglog_printf("Exception: SIMD exception (MXCSR=0x%08x) at %p\n", 
                mxcsr, (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGFPE); // TODO
        return IRQ_HANDLED;
    }
    
    return IRQ_HANDLED;
}

/*
 * Virtualization Exception (#VE) - Exception 20
 */
static irq_return_t handle_virtualization_exception(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Virtualization exception at %p\n", (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    /* This would integrate with virtualization subsystem */
    dump_exception_context(ctx);
    panic("Unhandled virtualization exception");
    return IRQ_NONE;
}

/*
 * Control Protection Exception (#CP) - Exception 21
 */
static irq_return_t handle_control_protection(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Control protection fault (error=0x%lx) at %p\n", 
                ctx->frame.error_code, (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    /* This would integrate with Intel CET support */
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGSEGV); // TODO
        return IRQ_HANDLED;
    }
    
    dump_exception_context(ctx);
    panic("Control protection fault in kernel mode");
    return IRQ_NONE;
}

/*
 * Security Exception (#SX) - Exception 30
 */
static irq_return_t handle_security_exception(struct interrupt_context *ctx)
{
    debuglog_printf("Exception: Security exception (error=0x%lx) at %p\n", 
                ctx->frame.error_code, (void*)(uintptr_t)FRAME_IP(ctx->frame));
    
    if (is_user_mode_exception(ctx)) {
        // task_send_signal(task_get_current(), SIGSEGV); // TODO
        return IRQ_HANDLED;
    }
    
    dump_exception_context(ctx);
    panic("Security exception in kernel mode");
    return IRQ_NONE;
}

/*
 * Update exception statistics
 */
static void update_exception_stats(int exception, struct interrupt_context *ctx)
{
    if (exception < 32) {
        atomic64_inc(&exception_stats[exception].count);
        exception_stats[exception].last_timestamp = ctx->timestamp;
        // exception_stats[exception].last_pid = task_get_current_pid(); // TODO
    }
}

/*
 * Check if exception occurred in user mode
 */
static bool is_user_mode_exception(struct interrupt_context *ctx)
{
    return (ctx->frame.cs & 3) == 3;
}

/*
 * Dump detailed exception context for debugging
 */
static void dump_exception_context(struct interrupt_context *ctx)
{
    int exception = (int)ctx->vector;
    
    debuglog_printf("\n=== EXCEPTION CONTEXT DUMP ===\n");
    debuglog_printf("Exception: %d (%s)\n", exception, exception_table[exception].name);
    debuglog_printf("Description: %s\n", exception_table[exception].description);
    
    if (exception_table[exception].has_error_code) {
        debuglog_printf("Error Code: 0x%lx\n", ctx->frame.error_code);
    }
    
#if ARCH_64BIT
    debuglog_printf("RIP: 0x%016lx  RSP: 0x%016lx  RBP: 0x%016lx\n", 
                FRAME_IP(ctx->frame), FRAME_SP(ctx->frame), ctx->regs.rbp);
    debuglog_printf("RAX: 0x%016lx  RBX: 0x%016lx  RCX: 0x%016lx\n",
                ctx->regs.rax, ctx->regs.rbx, ctx->regs.rcx);
    debuglog_printf("RDX: 0x%016lx  RSI: 0x%016lx  RDI: 0x%016lx\n",
                ctx->regs.rdx, ctx->regs.rsi, ctx->regs.rdi);
    debuglog_printf("R8:  0x%016lx  R9:  0x%016lx  R10: 0x%016lx\n",
                ctx->regs.r8, ctx->regs.r9, ctx->regs.r10);
    debuglog_printf("R11: 0x%016lx  R12: 0x%016lx  R13: 0x%016lx\n",
                ctx->regs.r11, ctx->regs.r12, ctx->regs.r13);
    debuglog_printf("R14: 0x%016lx  R15: 0x%016lx\n",
                ctx->regs.r14, ctx->regs.r15);
#else
    debuglog_printf("EIP: 0x%08x  ESP: 0x%08x  EBP: 0x%08x\n", 
                FRAME_IP(ctx->frame), FRAME_SP(ctx->frame), ctx->regs.ebp);
    debuglog_printf("EAX: 0x%08x  EBX: 0x%08x  ECX: 0x%08x  EDX: 0x%08x\n",
                ctx->regs.eax, ctx->regs.ebx, ctx->regs.ecx, ctx->regs.edx);
    debuglog_printf("ESI: 0x%08x  EDI: 0x%08x\n",
                ctx->regs.esi, ctx->regs.edi);
#endif

    debuglog_printf("CS: 0x%04x  SS: 0x%04x  DS: 0x%04x  ES: 0x%04x\n",
                (uint16_t)ctx->frame.cs, (uint16_t)ctx->frame.ss,
                ctx->regs.ds, ctx->regs.es);
    debuglog_printf("RFLAGS: 0x%lx\n", (unsigned long)FRAME_FLAGS(ctx->frame));
    debuglog_printf("Timestamp: %lu\n", ctx->timestamp);
    
    /* Additional context for page faults */
    if (exception == 14) {
        debuglog_printf("CR2 (fault address): 0x%lx\n", cpu_read_cr2());
    }
    
    /* Print stack trace if available */
    debuglog_printf("\nStack trace:\n");
    // print_stack_trace_from_context(ctx); // TODO
    
    debuglog_printf("=== END CONTEXT DUMP ===\n\n");
}

/*
 * Handle fatal exception
 */
static void handle_fatal_exception(int exception, struct interrupt_context *ctx)
{
    debuglog_printf("FATAL: Exception %d (%s) is fatal\n", 
                exception, exception_table[exception].name);
    
    if (is_user_mode_exception(ctx)) {
        /* Terminate the process */
        debuglog_printf("Terminating process due to fatal exception\n");
        task_terminate_current(SIGKILL);
    } else {
        /* System panic */
        panic("Fatal exception in kernel mode");
    }
}

/*
 * Get exception statistics
 */
void exception_get_stats(int exception, uint64_t *count, uint64_t *last_time, uint32_t *last_pid)
{
    if (exception < 32) {
        *count = atomic64_read(&exception_stats[exception].count);
        *last_time = exception_stats[exception].last_timestamp;
        *last_pid = exception_stats[exception].last_pid;
    } else {
        *count = 0;
        *last_time = 0;
        *last_pid = 0;
    }
}

/*
 * Dump all exception statistics
 */
void exception_dump_stats(void)
{
    int i;
    uint64_t count, last_time;
    uint32_t last_pid;
    
    debuglog_printf("\n=== EXCEPTION STATISTICS ===\n");
    
    for (i = 0; i < 32; i++) {
        exception_get_stats(i, &count, &last_time, &last_pid);
        
        if (count > 0) {
            debuglog_printf("[%2d] %-20s: %8lu events (last: PID %u at %lu)\n", 
                       i, exception_table[i].name, count, last_pid, last_time);
        }
    }
    
    debuglog_printf("=== END STATISTICS ===\n\n");
}
