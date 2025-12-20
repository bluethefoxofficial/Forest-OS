/*
 * Timer interrupt handler for Forest OS
 * Uses the new interrupt system for timer-based task scheduling
 */

#include "include/interrupt.h"
#include "include/task.h"
#include "include/io_ports.h"

static uint32 timer_ticks = 0;

static void timer_handler(struct interrupt_frame* frame, uint32 error_code) {
    (void)frame;
    (void)error_code;
    timer_ticks++;
    
    // Call the task scheduler every timer interrupt
    task_schedule();
    
    pic_send_eoi(0);
}

bool timer_init(uint32 frequency) {
    if (frequency == 0 || !interrupts_initialized) {
        return false;
    }
    
    // Install timer interrupt handler
    interrupt_set_handler(IRQ_TIMER, timer_handler);
    
    // Enable timer IRQ
    pic_unmask_irq(0);
    
    // Calculate the divisor for the PIT
    uint32 divisor = 1193180 / frequency;
    if (divisor == 0) {
        return false;
    }
    
    // Send divisor to PIT
    outportb(0x43, 0x36);  // Command byte
    outportb(0x40, divisor & 0xFF);        // Low byte
    outportb(0x40, (divisor >> 8) & 0xFF); // High byte
    
    return true;
}

uint32 timer_get_ticks(void) {
    return timer_ticks;
}

void timer_shutdown(void) {
    pic_mask_irq(0);
    interrupt_clear_handler(IRQ_TIMER);
}
