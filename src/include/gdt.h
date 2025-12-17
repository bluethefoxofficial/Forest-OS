#ifndef GDT_H
#define GDT_H

#include "types.h"

// Segment selectors
#define GDT_KERNEL_CODE_SELECTOR 0x08
#define GDT_KERNEL_DATA_SELECTOR 0x10
#define GDT_USER_CODE_SELECTOR   0x1B
#define GDT_USER_DATA_SELECTOR   0x23
#define GDT_TSS_SELECTOR         0x28

// Initialize GDT + TSS with an initial kernel stack top.
void gdt_init(uint32 initial_stack_top);

// Update the kernel stack used when returning from user mode.
void gdt_set_kernel_stack(uint32 stack_top);

#endif
