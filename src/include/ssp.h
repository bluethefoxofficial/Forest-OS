#ifndef __SSP_H__
#define __SSP_H__

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// STACK SMASHING PROTECTION (SSP) HEADER
// =============================================================================
// Provides compiler-level protection against buffer overflows and ROP attacks
// =============================================================================

// SSP configuration
#define SSP_CANARY_RANDOM_SEED    0xFACEFEED
#define SSP_MAX_VIOLATION_COUNT   10

// SSP Initialization and management
void ssp_init(void);
void ssp_generate_random_canary(void);
uint32_t ssp_get_canary(void);

// Canary checking functions (called by compiler-generated code)
void __stack_chk_fail(void) __attribute__((noreturn));
void __stack_chk_fail_local(void) __attribute__((noreturn));

// Manual canary operations for critical functions
uint32_t ssp_function_enter(void);
void ssp_function_exit(uint32_t canary);

// Return address protection
void ssp_protect_return_address(uint32_t *return_addr);
bool ssp_validate_return_address(uint32_t return_addr);

// Statistics and debugging
void ssp_get_stats(uint32_t *violations, uint32_t *checks_performed);
void ssp_report_violation(const char *function_name, uint32_t expected, uint32_t found);

// Enhanced stack frame validation
bool ssp_validate_stack_frame(uint32_t *frame_pointer);
void ssp_mark_function_safe(const char *function_name);

// Compiler support macros
extern uint32_t __stack_chk_guard;

// Function attribute macros for SSP
#define SSP_PROTECTED __attribute__((stack_protect))
#define SSP_STRONG __attribute__((stack_protect_strong))
#define SSP_ALL __attribute__((stack_protect_all))

// Manual canary protection macros
#define SSP_FUNCTION_ENTER() \
    uint32_t __ssp_canary = ssp_function_enter()

#define SSP_FUNCTION_EXIT() \
    ssp_function_exit(__ssp_canary)

// Return address protection macro
#define SSP_PROTECT_RETURN() do { \
    uint32_t *__ret_addr; \
    __asm__ volatile("lea 4(%%ebp), %0" : "=r"(__ret_addr)); \
    ssp_protect_return_address(__ret_addr); \
} while(0)

// Enhanced stack validation macro
#define SSP_VALIDATE_FRAME() do { \
    uint32_t *__frame_ptr; \
    __asm__ volatile("mov %%ebp, %0" : "=r"(__frame_ptr)); \
    if (!ssp_validate_stack_frame(__frame_ptr)) { \
        __stack_chk_fail(); \
    } \
} while(0)

#endif /* __SSP_H__ */