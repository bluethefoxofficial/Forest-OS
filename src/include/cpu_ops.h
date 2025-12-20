#ifndef CPU_OPS_H
#define CPU_OPS_H

#include "types.h"

// Low-level helpers implemented in assembly to keep C sources free of inline asm
uint32 cpu_read_eflags(void);
void cpu_disable_interrupts(void);
void cpu_enable_interrupts(void);
void cpu_load_idt(const void* descriptor);
uint16 cpu_read_cs(void);
uint16 cpu_read_ds(void);
uint32 cpu_read_cr2(void);

#endif // CPU_OPS_H
