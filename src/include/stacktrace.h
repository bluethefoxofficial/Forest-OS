#ifndef STACKTRACE_H
#define STACKTRACE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_STACK_FRAMES 32

typedef struct {
    uintptr_t ip;      // Instruction pointer
    uintptr_t bp;      // Base pointer
    const char *symbol; // Symbol name if available
} stack_frame_t;

typedef struct {
    stack_frame_t frames[MAX_STACK_FRAMES];
    size_t frame_count;
    bool symbols_available;
} stacktrace_t;

// Core stacktrace functions
int stacktrace_capture(stacktrace_t *trace);
void stacktrace_print(const stacktrace_t *trace);
void stacktrace_print_current(void);

// Symbol resolution (placeholder for future implementation)
const char *stacktrace_resolve_symbol(uintptr_t address);
int stacktrace_init_symbols(void);

// Utility functions
void stacktrace_dump_registers(void);
void stacktrace_print_frame(const stack_frame_t *frame, size_t index);

// Exception-specific stacktraces
void stacktrace_print_exception(const char *exception_name, uintptr_t error_code);

#endif // STACKTRACE_H