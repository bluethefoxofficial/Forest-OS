#ifndef INTERRUPT_COMPAT_H
#define INTERRUPT_COMPAT_H

#include "interrupt.h"

/*
 * Compatibility layer for legacy interrupt functions
 * This allows old code to work with the new interrupt system
 */

// Legacy IDT function compatibility
static inline void set_idt_gate(int n, uint32 handler) {
    extern void idt_set_gate(uint8 num, uint32 handler, uint8 flags);
    idt_set_gate((uint8)n, handler, IDT_GATE_INTERRUPT32);
}

static inline void set_idt_gate_flags(int n, uint32 handler, uint8 flags) {
    extern void idt_set_gate(uint8 num, uint32 handler, uint8 flags);
    idt_set_gate((uint8)n, handler, flags);
}

// Function prototype for external access to idt_set_gate
void idt_set_gate(uint8 num, uint32 handler, uint8 flags);

#endif // INTERRUPT_COMPAT_H