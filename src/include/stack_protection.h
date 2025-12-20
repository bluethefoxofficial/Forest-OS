#ifndef __STACK_PROTECTION_H__
#define __STACK_PROTECTION_H__

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// STACK OVERFLOW PROTECTION HEADER
// =============================================================================

// Function declarations
void stack_protection_init(void);
bool stack_check_integrity(void);
int stack_check_overflow(void);
void stack_get_stats(uint32_t *total_size, uint32_t *used_size, uint32_t *remaining);
void switch_to_emergency_stack(void);
bool stack_quick_check(uint32_t esp);

// Macro for protecting critical functions
#define STACK_PROTECT() do { \
    int overflow = stack_check_overflow(); \
    if (overflow < 0) { \
        extern void kernel_panic(const char* msg); \
        kernel_panic("Stack overflow detected"); \
    } \
} while(0)

// Macro for protecting interrupt handlers (minimal version)
#define STACK_PROTECT_IRQ() do { \
    uint32_t esp; \
    __asm__ volatile("mov %%esp, %0" : "=r"(esp)); \
    if (!stack_quick_check(esp)) { \
        __asm__ volatile("cli; hlt"); \
    } \
} while(0)

#endif /* __STACK_PROTECTION_H__ */