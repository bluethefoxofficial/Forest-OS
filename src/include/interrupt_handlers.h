#ifndef INTERRUPT_HANDLERS_H
#define INTERRUPT_HANDLERS_H

#include "interrupt.h"

extern void (*interrupt_stub_table[IDT_ENTRIES])(void);

void interrupt_install_all_stubs(void);

#endif // INTERRUPT_HANDLERS_H
