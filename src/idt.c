/*
 * Enhanced Interrupt Descriptor Table (IDT) Implementation for Forest OS
 * Supports both x86-32 and x86-64 architectures with UEFI integration
 */

#include "interrupt.h"
#include "cpu_ops.h"
#include "string.h"
#include "panic.h"
#include "debuglog.h"
#include "mm.h"

/* Global IDT manager instance */
static struct idt_manager g_idt_manager;

/* Assembly stub function declarations based on architecture */
#if ARCH_64BIT
extern void isr_stub_0(void), isr_stub_1(void), isr_stub_2(void), isr_stub_3(void);
extern void isr_stub_4(void), isr_stub_5(void), isr_stub_6(void), isr_stub_7(void);
extern void isr_stub_8(void), isr_stub_9(void), isr_stub_10(void), isr_stub_11(void);
extern void isr_stub_12(void), isr_stub_13(void), isr_stub_14(void), isr_stub_15(void);
extern void isr_stub_16(void), isr_stub_17(void), isr_stub_18(void), isr_stub_19(void);
extern void isr_stub_20(void), isr_stub_21(void), isr_stub_22(void), isr_stub_23(void);
extern void isr_stub_24(void), isr_stub_25(void), isr_stub_26(void), isr_stub_27(void);
extern void isr_stub_28(void), isr_stub_29(void), isr_stub_30(void), isr_stub_31(void);

extern void irq_stub_0(void), irq_stub_1(void), irq_stub_2(void), irq_stub_3(void);
extern void irq_stub_4(void), irq_stub_5(void), irq_stub_6(void), irq_stub_7(void);
extern void irq_stub_8(void), irq_stub_9(void), irq_stub_10(void), irq_stub_11(void);
extern void irq_stub_12(void), irq_stub_13(void), irq_stub_14(void), irq_stub_15(void);

extern void nmi_handler(void);
extern void double_fault_handler(void);
extern void machine_check_handler(void);
extern void syscall_handler(void);
extern void spurious_irq_handler(void);
#else
/* 32-bit: ELF uses non-underscored names */
extern void isr_stub_0(void), isr_stub_1(void), isr_stub_2(void), isr_stub_3(void);
extern void isr_stub_4(void), isr_stub_5(void), isr_stub_6(void), isr_stub_7(void);
extern void isr_stub_8(void), isr_stub_9(void), isr_stub_10(void), isr_stub_11(void);
extern void isr_stub_12(void), isr_stub_13(void), isr_stub_14(void), isr_stub_15(void);
extern void isr_stub_16(void), isr_stub_17(void), isr_stub_18(void), isr_stub_19(void);
extern void isr_stub_20(void), isr_stub_21(void), isr_stub_22(void), isr_stub_23(void);
extern void isr_stub_24(void), isr_stub_25(void), isr_stub_26(void), isr_stub_27(void);
extern void isr_stub_28(void), isr_stub_29(void), isr_stub_30(void), isr_stub_31(void);

/* IRQ stubs - these are just aliases to isr_stub for vector 32+ */
#define irq_stub_0  isr_stub_32
#define irq_stub_1  isr_stub_33
#define irq_stub_2  isr_stub_34
#define irq_stub_3  isr_stub_35
#define irq_stub_4  isr_stub_36
#define irq_stub_5  isr_stub_37
#define irq_stub_6  isr_stub_38
#define irq_stub_7  isr_stub_39
#define irq_stub_8  isr_stub_40
#define irq_stub_9  isr_stub_41
#define irq_stub_10 isr_stub_42
#define irq_stub_11 isr_stub_43
#define irq_stub_12 isr_stub_44
#define irq_stub_13 isr_stub_45
#define irq_stub_14 isr_stub_46
#define irq_stub_15 isr_stub_47

extern void isr_stub_32(void), isr_stub_33(void), isr_stub_34(void), isr_stub_35(void);
extern void isr_stub_36(void), isr_stub_37(void), isr_stub_38(void), isr_stub_39(void);
extern void isr_stub_40(void), isr_stub_41(void), isr_stub_42(void), isr_stub_43(void);
extern void isr_stub_44(void), isr_stub_45(void), isr_stub_46(void), isr_stub_47(void);

/* Prefer the dedicated syscall stub that builds a syscall_frame_t with registers */
extern void isr128(void);
extern void isr_stub_255(void);  /* SPURIOUS_VECTOR */

/* For 32-bit, syscall_handler and spurious_irq_handler are isr stubs */
#define syscall_handler isr128
#define spurious_irq_handler isr_stub_255
#endif

/* ISR function pointer arrays for easy indexing */
static void (*isr_stub_table[32])(void) = {
    isr_stub_0, isr_stub_1, isr_stub_2, isr_stub_3, isr_stub_4, isr_stub_5, isr_stub_6, isr_stub_7,
    isr_stub_8, isr_stub_9, isr_stub_10, isr_stub_11, isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
    isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19, isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
    isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27, isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
};

static void (*irq_stub_table[16])(void) = {
    irq_stub_0, irq_stub_1, irq_stub_2, irq_stub_3, irq_stub_4, irq_stub_5, irq_stub_6, irq_stub_7,
    irq_stub_8, irq_stub_9, irq_stub_10, irq_stub_11, irq_stub_12, irq_stub_13, irq_stub_14, irq_stub_15
};

/* Default gate type definitions */
#if ARCH_64BIT
#define DEFAULT_INTERRUPT_GATE  (IDT_PRESENT | IDT_DPL_0 | GATE_TYPE_INT64)
#define DEFAULT_TRAP_GATE       (IDT_PRESENT | IDT_DPL_0 | GATE_TYPE_TRAP64)
#define DEFAULT_USER_GATE       (IDT_PRESENT | IDT_DPL_3 | GATE_TYPE_INT64)
#else
#define DEFAULT_INTERRUPT_GATE  (IDT_PRESENT | IDT_DPL_0 | GATE_TYPE_INT32)
#define DEFAULT_TRAP_GATE       (IDT_PRESENT | IDT_DPL_0 | GATE_TYPE_TRAP32)
#define DEFAULT_USER_GATE       (IDT_PRESENT | IDT_DPL_3 | GATE_TYPE_INT32)
#endif

/* Forward declarations */
static void idt_set_gate_internal(uint8_t vector, void *handler, uint16_t selector, 
                                 uint8_t flags, uint8_t ist);
static void idt_clear_gate(uint8_t vector);
static void idt_setup_default_handlers(void);

/*
 * Initialize IDT manager structure
 */
static int idt_manager_init(void)
{
    memset(&g_idt_manager, 0, sizeof(g_idt_manager));
    
    spinlock_init(&g_idt_manager.lock, "idt_manager");
    atomic_set(&g_idt_manager.initialized, 0);
    
    /* Clear all handlers */
    memset(g_idt_manager.handlers, 0, sizeof(g_idt_manager.handlers));
    memset(g_idt_manager.handler_data, 0, sizeof(g_idt_manager.handler_data));
    memset(g_idt_manager.handler_stats, 0, sizeof(g_idt_manager.handler_stats));
    
    debuglog_printf("IDT: Manager initialized\n");
    return 0;
}

/*
 * Early IDT initialization - minimal setup for basic exception handling
 */
int idt_init_early(void)
{
    unsigned long flags;
    int ret;
    
    debuglog_printf("IDT: Starting early initialization\n");
    
    /* Initialize manager structure */
    ret = idt_manager_init();
    if (ret != 0) {
        panic("IDT: Failed to initialize manager");
        return ret;
    }
    
    flags = interrupt_save_and_disable();
    
    /* Clear the IDT entries */
    memset(g_idt_manager.entries, 0, sizeof(g_idt_manager.entries));
    
    /* Set up IDTR */
    g_idt_manager.idtr.limit = (sizeof(idt_entry_t) * IDT_ENTRIES_MAX) - 1;
#if ARCH_64BIT
    g_idt_manager.idtr.base = (uint64_t)g_idt_manager.entries;
#else
    g_idt_manager.idtr.base = (uint32_t)g_idt_manager.entries;
#endif
    
    /* Install critical exception handlers first */
    idt_setup_default_handlers();
    
    /* Load the IDT */
    idt_load_new();
    
    atomic_set(&g_idt_manager.initialized, 1);
    
    interrupt_restore(flags);
    
    debuglog_printf("IDT: Early initialization completed\n");
    return 0;
}

/*
 * Full IDT initialization - complete setup with all handlers
 */
int idt_init_full(void)
{
    unsigned long flags;
    int i;
    
    debuglog_printf("IDT: Starting full initialization\n");
    
    if (!atomic_read(&g_idt_manager.initialized)) {
        panic("IDT: Early initialization not completed");
        return -1;
    }
    
    flags = interrupt_save_and_disable();
    
    /* Set up all IRQ handlers */
    for (i = 0; i < 16; i++) {
        idt_set_gate_internal(IRQ_BASE_OFFSET + i, irq_stub_table[i], 
                             get_kernel_cs(), DEFAULT_INTERRUPT_GATE, 0);
    }
    
    /* Set up system call handler */
    idt_set_gate_internal(SYSCALL_VECTOR,
                         syscall_handler,
                         get_kernel_cs(), DEFAULT_USER_GATE, 0);

    /* Set up spurious interrupt handler */
    idt_set_gate_internal(SPURIOUS_VECTOR_PRIMARY,
                         spurious_irq_handler,
                         get_kernel_cs(), DEFAULT_INTERRUPT_GATE, 0);
    
    interrupt_restore(flags);
    
    debuglog_printf("IDT: Full initialization completed\n");
    return 0;
}

/*
 * Set up default exception handlers
 */
static void idt_setup_default_handlers(void)
{
    int i;
    uint16_t kernel_cs = get_kernel_cs();
    
    debuglog_printf("IDT: Setting up default exception handlers\n");
    
    /* Set up all 32 exception handlers */
    for (i = 0; i < 32; i++) {
        idt_set_gate_internal(i, isr_stub_table[i], kernel_cs, 
                             DEFAULT_INTERRUPT_GATE, 0);
    }
    
    /* Special handling for critical exceptions with IST on x86-64 */
#if ARCH_64BIT
    /* Double fault gets its own stack (IST1) */
    idt_set_gate_internal(EXCEPTION_DOUBLE_FAULT, double_fault_handler, 
                         kernel_cs, DEFAULT_INTERRUPT_GATE, 1);
    
    /* NMI gets its own stack (IST2) */
    idt_set_gate_internal(EXCEPTION_NMI, nmi_handler, 
                         kernel_cs, DEFAULT_INTERRUPT_GATE, 2);
    
    /* Machine check gets its own stack (IST3) */
    idt_set_gate_internal(EXCEPTION_MACHINE_CHECK, machine_check_handler, 
                         kernel_cs, DEFAULT_INTERRUPT_GATE, 3);
#else
    /* For 32-bit, use the same exception handlers from ISR stub table */
    idt_set_gate_internal(EXCEPTION_DOUBLE_FAULT, isr_stub_table[EXCEPTION_DOUBLE_FAULT],
                         kernel_cs, DEFAULT_INTERRUPT_GATE, 0);

    idt_set_gate_internal(EXCEPTION_NMI, isr_stub_table[EXCEPTION_NMI],
                         kernel_cs, DEFAULT_INTERRUPT_GATE, 0);

    idt_set_gate_internal(EXCEPTION_MACHINE_CHECK, isr_stub_table[EXCEPTION_MACHINE_CHECK],
                         kernel_cs, DEFAULT_INTERRUPT_GATE, 0);
#endif
    
    /* Some exceptions should be trap gates to allow nested interrupts */
    idt_set_gate_internal(EXCEPTION_DEBUG, isr_stub_table[EXCEPTION_DEBUG], 
                         kernel_cs, DEFAULT_TRAP_GATE, 0);
    idt_set_gate_internal(EXCEPTION_BREAKPOINT, isr_stub_table[EXCEPTION_BREAKPOINT], 
                         kernel_cs, DEFAULT_TRAP_GATE, 0);
}

/*
 * Internal function to set an IDT gate entry
 */
static void idt_set_gate_internal(uint8_t vector, void *handler, uint16_t selector, 
                                 uint8_t flags, uint8_t ist)
{
    idt_entry_t *entry;
    
    if (vector >= IDT_ENTRIES_MAX) {
        debuglog_printf("IDT: Invalid vector %d\n", vector);
        return;
    }
    
    entry = &g_idt_manager.entries[vector];
    
#if ARCH_64BIT
    uint64_t handler_addr = (uint64_t)handler;
    
    entry->offset_low = handler_addr & 0xFFFF;
    entry->selector = selector;
    entry->ist = ist & 0x7;  /* IST is 3 bits */
    entry->flags = flags;
    entry->offset_mid = (handler_addr >> 16) & 0xFFFF;
    entry->offset_high = (handler_addr >> 32) & 0xFFFFFFFF;
    entry->reserved = 0;
#else
    uint32_t handler_addr = (uint32_t)handler;
    
    entry->offset_low = handler_addr & 0xFFFF;
    entry->selector = selector;
    entry->reserved = 0;
    entry->flags = flags;
    entry->offset_high = (handler_addr >> 16) & 0xFFFF;
#endif
    
    debuglog_printf("IDT: Set gate %d -> %p (flags=0x%02x, ist=%d)\n", 
                vector, handler, flags, ist);
}

/*
 * Clear an IDT gate entry
 */
static void idt_clear_gate(uint8_t vector)
{
    if (vector >= IDT_ENTRIES_MAX) {
        return;
    }
    
    memset(&g_idt_manager.entries[vector], 0, sizeof(idt_entry_t));
    g_idt_manager.handlers[vector] = NULL;
    g_idt_manager.handler_data[vector] = NULL;
}

/*
 * Public function to set an enhanced IDT gate
 */
void idt_set_gate_enhanced(uint8_t vector, void *handler, uint16_t selector, 
                          uint8_t flags, uint8_t ist)
{
    unsigned long irq_flags;
    
    irq_flags = interrupt_save_and_disable();
    idt_set_gate_internal(vector, handler, selector, flags, ist);
    interrupt_restore(irq_flags);
}

/*
 * Set a user-accessible gate (DPL=3)
 */
void idt_set_user_gate(uint8_t vector, void *handler)
{
    idt_set_gate_enhanced(vector, handler, get_kernel_cs(), DEFAULT_USER_GATE, 0);
}

/*
 * Set a system gate (DPL=0)
 */
void idt_set_system_gate(uint8_t vector, void *handler)
{
    idt_set_gate_enhanced(vector, handler, get_kernel_cs(), DEFAULT_INTERRUPT_GATE, 0);
}

/*
 * Load the IDT register
 */
void idt_load_new(void)
{
#if ARCH_64BIT
    asm volatile("lidt %0" :: "m" (g_idt_manager.idtr) : "memory");
#else
    asm volatile("lidt %0" :: "m" (g_idt_manager.idtr) : "memory");
#endif
    
    debuglog_printf("IDT: Loaded new IDT at %p (limit=0x%04x)\n", 
                (void*)g_idt_manager.idtr.base, g_idt_manager.idtr.limit);
}

/*
 * Register a high-level interrupt handler for a specific vector
 */
int idt_register_handler(uint8_t vector, interrupt_handler_t handler, void *data)
{
    unsigned long flags;
    
    if (vector >= IDT_ENTRIES_MAX) {
        return -1;
    }
    
    if (!handler) {
        return -1;
    }
    
    flags = interrupt_save_and_disable();
    
    g_idt_manager.handlers[vector] = handler;
    g_idt_manager.handler_data[vector] = data;
    
    interrupt_restore(flags);
    
    debuglog_printf("IDT: Registered handler for vector %d\n", vector);
    return 0;
}

/*
 * Unregister a high-level interrupt handler
 */
void idt_unregister_handler(uint8_t vector)
{
    unsigned long flags;
    
    if (vector >= IDT_ENTRIES_MAX) {
        return;
    }
    
    flags = interrupt_save_and_disable();
    
    g_idt_manager.handlers[vector] = NULL;
    g_idt_manager.handler_data[vector] = NULL;
    
    interrupt_restore(flags);
    
    debuglog_printf("IDT: Unregistered handler for vector %d\n", vector);
}

/*
 * Get the registered handler for a vector
 */
interrupt_handler_t idt_get_handler(uint8_t vector)
{
    if (vector >= IDT_ENTRIES_MAX) {
        return NULL;
    }
    
    return g_idt_manager.handlers[vector];
}

/*
 * Get handler data for a vector
 */
void *idt_get_handler_data(uint8_t vector)
{
    if (vector >= IDT_ENTRIES_MAX) {
        return NULL;
    }
    
    return g_idt_manager.handler_data[vector];
}

/*
 * Update statistics for a vector
 */
void idt_update_stats(uint8_t vector)
{
    if (vector < IDT_ENTRIES_MAX) {
        g_idt_manager.handler_stats[vector]++;
    }
}

/*
 * Get statistics for a vector
 */
uint64_t idt_get_stats(uint8_t vector)
{
    if (vector >= IDT_ENTRIES_MAX) {
        return 0;
    }
    
    return g_idt_manager.handler_stats[vector];
}

/*
 * Dump IDT information for debugging
 */
void idt_dump(void)
{
    int i;
    idt_entry_t *entry;
    
    debuglog_printf("IDT Dump (base=0x%p, limit=0x%04x):\n", 
                (void*)g_idt_manager.idtr.base, g_idt_manager.idtr.limit);
    
    for (i = 0; i < IDT_ENTRIES_MAX; i++) {
        entry = &g_idt_manager.entries[i];
        
        if (entry->flags & IDT_PRESENT) {
#if ARCH_64BIT
            uint64_t handler_addr = entry->offset_low | 
                                   ((uint64_t)entry->offset_mid << 16) |
                                   ((uint64_t)entry->offset_high << 32);
            debuglog_printf("  [%3d] Handler: 0x%016lx, Selector: 0x%04x, "
                       "Flags: 0x%02x, IST: %d, Stats: %lu\n", 
                       i, handler_addr, entry->selector, entry->flags, 
                       entry->ist, g_idt_manager.handler_stats[i]);
#else
            uint32_t handler_addr = entry->offset_low | 
                                   ((uint32_t)entry->offset_high << 16);
            debuglog_printf("  [%3d] Handler: 0x%08x, Selector: 0x%04x, "
                       "Flags: 0x%02x, Stats: %lu\n", 
                       i, handler_addr, entry->selector, entry->flags,
                       g_idt_manager.handler_stats[i]);
#endif
        }
    }
}

/*
 * Get the IDT manager (for internal use)
 */
struct idt_manager *get_idt_manager(void)
{
    return &g_idt_manager;
}

/*
 * Check if IDT is properly initialized
 */
bool idt_is_initialized(void)
{
    return atomic_read(&g_idt_manager.initialized) != 0;
}

/*
 * Validate IDT integrity (for debugging)
 */
int idt_validate(void)
{
    int i, errors = 0;
    idt_entry_t *entry;
    
    debuglog_printf("IDT: Validating integrity...\n");
    
    /* Check critical exception handlers */
    for (i = 0; i < 32; i++) {
        entry = &g_idt_manager.entries[i];
        if (!(entry->flags & IDT_PRESENT)) {
            debuglog_printf("IDT: Error - Exception %d not present\n", i);
            errors++;
        }
    }
    
    /* Check if IDT base address is reasonable */
#if ARCH_64BIT
    if (g_idt_manager.idtr.base < 0xFFFF800000000000UL) {
        debuglog_printf("IDT: Warning - IDT base address looks suspicious\n");
        errors++;
    }
#else
    if (g_idt_manager.idtr.base < 0x00100000) {
        debuglog_printf("IDT: Warning - IDT base address looks suspicious\n");
        errors++;
    }
#endif
    
    if (errors == 0) {
        debuglog_printf("IDT: Validation passed\n");
    } else {
        debuglog_printf("IDT: Validation found %d errors\n", errors);
    }
    
    return errors;
}

/*
 * NOTE: Legacy compatibility functions (idt_set_gate, interrupt_get_handler,
 * interrupt_set_handler, interrupt_clear_handler) are now provided by
 * interrupt.c to avoid duplicate symbol conflicts. The functions in this
 * file (idt_get_handler, idt_register_handler, idt_unregister_handler,
 * idt_set_gate_enhanced) can be used for advanced IDT management.
 */
