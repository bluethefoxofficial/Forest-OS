#include "include/interrupt.h"
#include "include/interrupt_handlers.h"
#include "include/panic.h"

extern void (*interrupt_stub_table[IDT_ENTRIES])(void);

static void verify_vector_present(uint16 vector) {
    if (!interrupt_stub_table[vector]) {
        kernel_panic("Missing interrupt stub");
    }
}

void interrupt_install_all_stubs(void) {
    for (uint16 vector = 0; vector < IDT_ENTRIES; ++vector) {
        verify_vector_present(vector);
        idt_set_gate(vector,
                     (uint32)interrupt_stub_table[vector],
                     IDT_GATE_INTERRUPT32);
    }
}
