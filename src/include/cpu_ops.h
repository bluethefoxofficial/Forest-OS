#ifndef CPU_OPS_H
#define CPU_OPS_H

#include "types.h"
#include <stdint.h>

// Low-level helpers implemented in assembly to keep C sources free of inline asm
uint32 cpu_read_eflags(void);
void cpu_disable_interrupts(void);
void cpu_enable_interrupts(void);
void cpu_load_idt(const void* descriptor);
uint16 cpu_read_cs(void);
uint16 cpu_read_ds(void);
uint32 cpu_read_cr2(void);

// MSR and TSC access (implemented in cpu_utils.c)
uint64_t read_msr(uint32_t msr);
void write_msr(uint32_t msr, uint64_t value);
uint64_t read_tsc(void);

// Get kernel code segment selector (implemented in cpu_utils.c)
uint32_t get_kernel_cs(void);

// Timer delay (implemented in cpu_utils.c)
void timer_delay_ms(uint32_t milliseconds);

// Memory mapping (implemented in cpu_utils.c)
void* mm_map_physical_page(uint64_t physical_addr, uint32_t flags);

#endif // CPU_OPS_H
