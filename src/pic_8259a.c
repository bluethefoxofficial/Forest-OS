/*
 * Advanced 8259A PIC (Programmable Interrupt Controller) Driver
 * Supports cascaded configuration, spurious interrupts, and advanced features
 * Integrates with Forest OS advanced interrupt management system
 */

#include "interrupt.h"
#include "cpu_ops.h"
#include "debug.h"
#include "debuglog.h"
#include "panic.h"
#include "atomic.h"
#include "timer.h"

/* PIC commands */
#define ICW1_ICW4           0x01  /* ICW4 (not) needed */
#define ICW1_SINGLE         0x02  /* Single (cascade) mode */
#define ICW1_INTERVAL4      0x04  /* Call address interval 4 (8) */
#define ICW1_LEVEL          0x08  /* Level triggered (edge) mode */
#define ICW1_INIT           0x10  /* Initialization - required! */

#define ICW4_8086           0x01  /* 8086/88 (MCS-80/85) mode */
#define ICW4_AUTO           0x02  /* Auto (normal) EOI */
#define ICW4_BUF_SLAVE      0x08  /* Buffered mode/slave */
#define ICW4_BUF_MASTER     0x0C  /* Buffered mode/master */
#define ICW4_SFNM           0x10  /* Special fully nested (not) */

/* PIC vector bases */
#define PIC1_VECTOR_BASE    0x20  /* Master PIC base vector */
#define PIC2_VECTOR_BASE    0x28  /* Slave PIC base vector */

/* Spurious interrupt vectors */
#define PIC1_SPURIOUS_IRQ   7
#define PIC2_SPURIOUS_IRQ   15

/* PIC state management */
struct pic_8259a_state {
    bool initialized;
    bool auto_eoi_mode;
    uint16_t irq_mask;
    atomic64_t spurious_count[2];  /* Master, Slave */
    atomic64_t interrupt_count[16]; /* Per-IRQ counters */
    uint64_t last_spurious_time[2];
    spinlock_t lock;
};

static struct pic_8259a_state pic_state = {
    .initialized = false,
    .auto_eoi_mode = false,
    .irq_mask = 0xFFFF,  /* All IRQs masked initially */
    .lock = SPINLOCK_UNLOCKED
};

/* Function prototypes */
static void pic_8259a_remap(uint8_t offset1, uint8_t offset2);
static bool pic_8259a_is_spurious(uint8_t irq);
static void pic_8259a_mask_all(void);
static void pic_8259a_unmask_cascade(void);
static irq_return_t pic_spurious_handler(int vector, struct interrupt_context *ctx);

/*
 * Initialize 8259A PIC in advanced mode
 */
int pic_8259a_init_advanced(void)
{
    unsigned long flags;
    
    debug_print("PIC-8259A: Initializing advanced 8259A PIC driver\n");
    
    spin_lock_irqsave(&pic_state.lock, flags);
    
    if (pic_state.initialized) {
        spin_unlock_irqrestore(&pic_state.lock, flags);
        debug_print("PIC-8259A: Already initialized\n");
        return 0;
    }
    
    /* Clear statistics */
    for (int i = 0; i < 16; i++) {
        atomic64_set(&pic_state.interrupt_count[i], 0);
    }
    atomic64_set(&pic_state.spurious_count[0], 0);
    atomic64_set(&pic_state.spurious_count[1], 0);
    pic_state.last_spurious_time[0] = 0;
    pic_state.last_spurious_time[1] = 0;
    
    /* Disable interrupts during initialization */
    irq_disable_safe();
    
    /* Save original PIC masks */
    inb(PIC1_DATA);
    inb(PIC2_DATA);
    
    /* Remap PIC to avoid conflicts with CPU exceptions */
    pic_8259a_remap(PIC1_VECTOR_BASE, PIC2_VECTOR_BASE);
    
    /* Mask all IRQs except cascade line */
    pic_8259a_mask_all();
    pic_8259a_unmask_cascade();
    
    /* Configure for manual EOI mode (safer for debugging) */
    pic_state.auto_eoi_mode = false;
    
    /* Register spurious interrupt handlers */
    idt_register_handler(PIC1_VECTOR_BASE + PIC1_SPURIOUS_IRQ, 
                        pic_spurious_handler, 
                        "PIC1 Spurious IRQ");
    idt_register_handler(PIC2_VECTOR_BASE + PIC2_SPURIOUS_IRQ, 
                        pic_spurious_handler, 
                        "PIC2 Spurious IRQ");
    
    pic_state.initialized = true;
    
    spin_unlock_irqrestore(&pic_state.lock, flags);
    
    debug_print("PIC-8259A: Advanced initialization complete\n");
    debug_print("PIC-8259A: Master base=0x%02x, Slave base=0x%02x\n", 
                PIC1_VECTOR_BASE, PIC2_VECTOR_BASE);
    
    return 0;
}

bool pic_is_available(void)
{
    return pic_state.initialized;
}

/*
 * Remap PIC interrupt vectors
 */
static void pic_8259a_remap(uint8_t offset1, uint8_t offset2)
{
    /* Save masks */
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);
    
    /* Start initialization sequence in cascade mode */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    
    /* Set vector offsets */
    outb(PIC1_DATA, offset1);  /* Master PIC vector offset */
    io_wait();
    outb(PIC2_DATA, offset2);  /* Slave PIC vector offset */
    io_wait();
    
    /* Configure cascade */
    outb(PIC1_DATA, 4);        /* Tell Master PIC that there is slave at IRQ2 (0000 0100) */
    io_wait();
    outb(PIC2_DATA, 2);        /* Tell Slave PIC its cascade identity (0000 0010) */
    io_wait();
    
    /* Set 8086/88 mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();
    
    /* Restore saved masks */
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

/*
 * Mask all IRQs
 */
static void pic_8259a_mask_all(void)
{
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    pic_state.irq_mask = 0xFFFF;
}

/*
 * Unmask cascade line (IRQ 2)
 */
static void pic_8259a_unmask_cascade(void)
{
    uint8_t master_mask = inb(PIC1_DATA);
    master_mask &= ~(1 << 2);  /* Unmask IRQ2 (cascade) */
    outb(PIC1_DATA, master_mask);
    pic_state.irq_mask &= ~(1 << 2);
}

/*
 * Mask specific IRQ
 */
void pic_8259a_mask_irq(uint8_t irq)
{
    unsigned long flags;
    uint16_t port;
    uint8_t value;
    
    if (irq >= 16 || !pic_state.initialized) {
        return;
    }
    
    spin_lock_irqsave(&pic_state.lock, flags);
    
    port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) {
        irq -= 8;
    }
    
    value = inb(port) | (1 << irq);
    outb(port, value);
    
    /* Update state */
    pic_state.irq_mask |= (1 << (irq + ((port == PIC2_DATA) ? 8 : 0)));
    
    spin_unlock_irqrestore(&pic_state.lock, flags);
    
    debug_print("PIC-8259A: Masked IRQ %d\n", 
               irq + ((port == PIC2_DATA) ? 8 : 0));
}

/*
 * Unmask specific IRQ
 */
void pic_8259a_unmask_irq(uint8_t irq)
{
    unsigned long flags;
    uint16_t port;
    uint8_t value;
    uint8_t original_irq = irq;
    
    if (irq >= 16 || !pic_state.initialized) {
        return;
    }
    
    spin_lock_irqsave(&pic_state.lock, flags);
    
    port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) {
        irq -= 8;
        /* Also ensure cascade line is unmasked */
        uint8_t master_mask = inb(PIC1_DATA);
        if (master_mask & (1 << 2)) {
            master_mask &= ~(1 << 2);
            outb(PIC1_DATA, master_mask);
            pic_state.irq_mask &= ~(1 << 2);
        }
    }
    
    value = inb(port) & ~(1 << irq);
    outb(port, value);
    
    /* Update state */
    pic_state.irq_mask &= ~(1 << original_irq);
    
    spin_unlock_irqrestore(&pic_state.lock, flags);
    
    debug_print("PIC-8259A: Unmasked IRQ %d\n", original_irq);
}

/*
 * Send End of Interrupt
 */
void pic_8259a_send_eoi(uint8_t irq)
{
    unsigned long flags;
    
    if (irq >= 16 || !pic_state.initialized) {
        return;
    }
    
    spin_lock_irqsave(&pic_state.lock, flags);
    
    /* Update interrupt statistics */
    atomic64_inc(&pic_state.interrupt_count[irq]);
    
    /* Send EOI to appropriate PIC(s) */
    if (irq >= 8) {
        /* Send EOI to slave PIC */
        outb(PIC2_COMMAND, PIC_EOI);
    }
    /* Always send EOI to master PIC for any IRQ */
    outb(PIC1_COMMAND, PIC_EOI);
    
    spin_unlock_irqrestore(&pic_state.lock, flags);
}

/*
 * Get IRR (Interrupt Request Register)
 */
uint16_t pic_8259a_get_irr(void)
{
    if (!pic_state.initialized) {
        return 0;
    }
    
    outb(PIC1_COMMAND, 0x0A);
    outb(PIC2_COMMAND, 0x0A);
    return (inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND);
}

/*
 * Get ISR (In-Service Register)
 */
uint16_t pic_8259a_get_isr(void)
{
    if (!pic_state.initialized) {
        return 0;
    }
    
    outb(PIC1_COMMAND, 0x0B);
    outb(PIC2_COMMAND, 0x0B);
    return (inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND);
}

/*
 * Check if interrupt is spurious
 */
static bool pic_8259a_is_spurious(uint8_t irq)
{
    uint16_t isr;
    
    if (irq == PIC1_SPURIOUS_IRQ) {
        /* Check master PIC ISR */
        outb(PIC1_COMMAND, 0x0B);
        isr = inb(PIC1_COMMAND);
        return !(isr & (1 << PIC1_SPURIOUS_IRQ));
    } else if (irq == PIC2_SPURIOUS_IRQ) {
        /* Check slave PIC ISR */
        outb(PIC2_COMMAND, 0x0B);
        isr = inb(PIC2_COMMAND);
        if (!(isr & (1 << (PIC2_SPURIOUS_IRQ - 8)))) {
            /* Also send EOI to master for cascade line */
            outb(PIC1_COMMAND, PIC_EOI);
            return true;
        }
    }
    return false;
}

/*
 * Handle spurious interrupts
 */
static irq_return_t pic_spurious_handler(int vector, struct interrupt_context *ctx)
{
    uint8_t irq = vector - PIC1_VECTOR_BASE;
    int pic_num = (irq >= 8) ? 1 : 0;
    
    if (pic_8259a_is_spurious(irq)) {
        /* Update spurious interrupt statistics */
        atomic64_inc(&pic_state.spurious_count[pic_num]);
        pic_state.last_spurious_time[pic_num] = ctx->timestamp;
        
        debug_print("PIC-8259A: Spurious interrupt on IRQ %d (PIC %d)\n", 
                   irq, pic_num + 1);
        
        /* Do not send EOI for genuine spurious interrupts */
        return IRQ_HANDLED;
    }
    
    /* Not actually spurious - handle normally */
    pic_8259a_send_eoi(irq);
    debug_print("PIC-8259A: False spurious on IRQ %d\n", irq);
    return IRQ_HANDLED;
}

/*
 * Disable PIC (for APIC systems)
 */
void pic_8259a_disable(void)
{
    unsigned long flags;
    
    debug_print("PIC-8259A: Disabling 8259A PIC for APIC mode\n");
    
    spin_lock_irqsave(&pic_state.lock, flags);
    
    /* Mask all interrupts */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    
    pic_state.irq_mask = 0xFFFF;
    pic_state.initialized = false;
    
    spin_unlock_irqrestore(&pic_state.lock, flags);
    
    debug_print("PIC-8259A: Disabled\n");
}

/*
 * Get PIC statistics
 */
void pic_8259a_get_stats(struct pic_stats *stats)
{
    if (!stats || !pic_state.initialized) {
        return;
    }
    
    stats->type = INTCTL_8259A_PIC;
    stats->initialized = pic_state.initialized;
    stats->auto_eoi = pic_state.auto_eoi_mode;
    stats->irq_mask = pic_state.irq_mask;
    
    for (int i = 0; i < 16; i++) {
        stats->irq_counts[i] = atomic64_read(&pic_state.interrupt_count[i]);
    }
    
    stats->spurious_count = atomic64_read(&pic_state.spurious_count[0]) +
                           atomic64_read(&pic_state.spurious_count[1]);
    stats->last_spurious_time = (pic_state.last_spurious_time[0] > pic_state.last_spurious_time[1]) ?
                               pic_state.last_spurious_time[0] : pic_state.last_spurious_time[1];
}

/*
 * Dump PIC debug information
 */
void pic_8259a_debug_dump(void)
{
    uint16_t irr, isr;
    struct pic_stats stats;
    
    if (!pic_state.initialized) {
        debug_print("PIC-8259A: Not initialized\n");
        return;
    }
    
    irr = pic_8259a_get_irr();
    isr = pic_8259a_get_isr();
    pic_8259a_get_stats(&stats);
    
    debug_print("\n=== PIC 8259A DEBUG INFORMATION ===\n");
    debug_print("State: %s, Auto-EOI: %s\n", 
               stats.initialized ? "Initialized" : "Not initialized",
               stats.auto_eoi ? "Enabled" : "Disabled");
    debug_print("IRQ Mask: 0x%04x\n", stats.irq_mask);
    debug_print("IRR (Interrupt Request): 0x%04x\n", irr);
    debug_print("ISR (In Service): 0x%04x\n", isr);
    debug_print("Spurious Interrupts: %lu (last at %lu)\n", 
               stats.spurious_count, stats.last_spurious_time);
    
    debug_print("IRQ Statistics:\n");
    for (int i = 0; i < 16; i++) {
        if (stats.irq_counts[i] > 0) {
            debug_print("  IRQ%2d: %8lu interrupts\n", i, stats.irq_counts[i]);
        }
    }
    debug_print("=== END PIC DEBUG ===\n\n");
}

/*
 * Check if IRQ is enabled
 */
bool pic_8259a_is_irq_enabled(uint8_t irq)
{
    if (irq >= 16 || !pic_state.initialized) {
        return false;
    }
    
    return !(pic_state.irq_mask & (1 << irq));
}

/*
 * Get current IRQ mask
 */
uint16_t pic_8259a_get_mask(void)
{
    return pic_state.irq_mask;
}

/*
 * PIC chip operations structure for interrupt controller abstraction
 */
struct interrupt_chip_ops pic_8259a_chip_ops = {
    .name = "8259A-PIC",
    .init = pic_8259a_init_advanced,
    .mask = pic_8259a_mask_irq,
    .unmask = pic_8259a_unmask_irq,
    .eoi = pic_8259a_send_eoi,
    .disable = pic_8259a_disable,
    .get_stats = (void (*)(void *))pic_8259a_get_stats
};
