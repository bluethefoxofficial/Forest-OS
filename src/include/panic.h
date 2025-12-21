#ifndef PANIC_H
#define PANIC_H

#include "types.h"
#include "screen.h"
#include "ps2_mouse.h"


typedef enum {
    PANIC_SCREEN_STACK = 0,
    PANIC_SCREEN_MEMORY,
    PANIC_SCREEN_SYSTEM,
    PANIC_SCREEN_INTERRUPTS,
    PANIC_SCREEN_REGISTERS,
    PANIC_SCREEN_FLAGS,
    PANIC_SCREEN_VMM,
    PANIC_SCREEN_MAX
} panic_screen_id_t;

// Trigger a fatal kernel panic with a friendly screen.
void kernel_panic_annotated(const char* message, const char* file, uint32 line, const char* func);
void kernel_panic_with_stack(const char* message, const uint32* stack_entries, uint32 entry_count);
void kernel_panic_set_scroll(panic_screen_id_t screen, int32 offset);
void panic_preload_fault_info(uint32 fault_addr, uint32 error_code,
                              uint32 fault_eip, uint32 fault_cs, uint32 fault_eflags);

#define kernel_panic(msg) kernel_panic_annotated(msg, __FILE__, __LINE__, __func__)
#define PANIC(msg) kernel_panic_annotated(msg, __FILE__, __LINE__, __func__)

#endif
