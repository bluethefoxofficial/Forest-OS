#include "include/ssp.h"
#include "include/screen.h"
#include "include/memory.h"
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// STACK SMASHING PROTECTION (SSP) IMPLEMENTATION
// =============================================================================
// Comprehensive protection against buffer overflows and return address attacks
// =============================================================================

// Global canary value (used by compiler-generated code)
uint32_t __stack_chk_guard = 0;

// SSP statistics and state
typedef struct ssp_stats {
    uint32_t violations_detected;
    uint32_t checks_performed;
    uint32_t false_positives;
    uint32_t return_address_violations;
} ssp_stats_t;

static ssp_stats_t ssp_stats = {0};
static bool ssp_initialized = false;
static uint32_t ssp_random_seed = SSP_CANARY_RANDOM_SEED;

// Simple PRNG for canary generation
static uint32_t ssp_random_next(void) {
    ssp_random_seed = ssp_random_seed * 1103515245 + 12345;
    return (ssp_random_seed >> 16) & 0x7FFF;
}

// Generate a cryptographically strong canary value
void ssp_generate_random_canary(void) {
    uint32_t entropy = 0;
    uint32_t timestamp = 0;
    
    // Get some entropy from CPU timestamp counter if available
    __asm__ volatile("rdtsc" : "=a"(timestamp) : : "edx");
    
    // Combine multiple entropy sources
    entropy ^= timestamp;
    entropy ^= (uint32_t)&ssp_stats;  // Use address space layout
    entropy ^= ssp_random_next();
    
    // Ensure canary is never null or contains common patterns
    __stack_chk_guard = entropy;
    if (__stack_chk_guard == 0 || 
        __stack_chk_guard == 0xFFFFFFFF ||
        __stack_chk_guard == 0xDEADBEEF ||
        __stack_chk_guard == 0xFEEDFACE) {
        __stack_chk_guard = 0xCAFEBABE ^ entropy;
    }
}

// Initialize SSP system
void ssp_init(void) {
    print("[SSP] Initializing Stack Smashing Protection...\n");
    
    // Generate initial canary
    ssp_generate_random_canary();
    
    // Initialize statistics
    ssp_stats.violations_detected = 0;
    ssp_stats.checks_performed = 0;
    ssp_stats.false_positives = 0;
    ssp_stats.return_address_violations = 0;
    
    ssp_initialized = true;
    
    print("[SSP] Canary value: 0x");
    print_hex(__stack_chk_guard);
    print("\n");
}

// Get current canary value
uint32_t ssp_get_canary(void) {
    return __stack_chk_guard;
}

// Direct VGA write function for emergencies (avoid heap allocation)
static void ssp_emergency_write(const char *msg, int row, int col, uint8_t attr) {
    volatile uint16_t *vga = (volatile uint16_t*)0xB8000;
    int pos = row * 80 + col;
    
    while (*msg && col < 80) {
        vga[pos] = (attr << 8) | *msg;
        msg++;
        pos++;
        col++;
    }
}

// Handle stack smashing detection (compiler-generated calls)
void __stack_chk_fail(void) {
    ssp_stats.violations_detected++;
    
    // Use direct VGA writing to avoid potential recursion
    ssp_emergency_write("*** STACK SMASHING DETECTED ***", 0, 0, 0x4F);
    ssp_emergency_write("System halted for security", 1, 0, 0x4F);
    
    // Disable interrupts and halt
    __asm__ volatile("cli");
    
    // Try to switch to emergency stack if available
    extern void switch_to_emergency_stack(void);
    switch_to_emergency_stack();
    
    // Regenerate canary for future protection
    ssp_generate_random_canary();
    
    // Halt the system
    __asm__ volatile("hlt");
    
    // Should never reach here
    while(1) {
        __asm__ volatile("hlt");
    }
}

// Local stack checking failure (when using local canaries)
void __stack_chk_fail_local(void) {
    ssp_stats.violations_detected++;
    ssp_emergency_write("*** LOCAL STACK CORRUPTION ***", 2, 0, 0x4F);
    __stack_chk_fail(); // Chain to main handler
}

// Manual function entry protection
uint32_t ssp_function_enter(void) {
    ssp_stats.checks_performed++;
    
    if (!ssp_initialized) {
        return 0; // Can't protect if not initialized
    }
    
    // Return current canary for this function
    return __stack_chk_guard;
}

// Manual function exit validation
void ssp_function_exit(uint32_t canary) {
    if (!ssp_initialized) {
        return;
    }
    
    if (canary != __stack_chk_guard) {
        ssp_report_violation("manual_protection", __stack_chk_guard, canary);
        __stack_chk_fail();
    }
}

// Protect return address location
void ssp_protect_return_address(uint32_t *return_addr) {
    if (!ssp_initialized || !return_addr) {
        return;
    }
    
    // Validate that this looks like a reasonable return address
    uint32_t addr = *return_addr;
    
    // Check if address is in reasonable code range (above 0x100000, below 0x80000000)
    if (addr < 0x100000 || addr > 0x80000000) {
        ssp_stats.return_address_violations++;
        ssp_report_violation("return_address", addr, 0);
        __stack_chk_fail();
    }
}

// Validate a return address
bool ssp_validate_return_address(uint32_t return_addr) {
    if (!ssp_initialized) {
        return true; // Can't validate if not initialized
    }
    
    // Basic sanity checks for return address
    if (return_addr < 0x100000 || return_addr > 0x80000000) {
        return false;
    }
    
    // Check alignment (x86 instructions are typically aligned)
    if (return_addr & 0x3) {
        return false;
    }
    
    return true;
}

// Enhanced stack frame validation
bool ssp_validate_stack_frame(uint32_t *frame_pointer) {
    if (!ssp_initialized || !frame_pointer) {
        return false;
    }
    
    // Basic sanity checks
    uint32_t frame_addr = (uint32_t)frame_pointer;
    
    // Check if frame pointer is in reasonable stack range
    if (frame_addr < 0x100000 || frame_addr > 0x800000) {
        return false;
    }
    
    // Check frame pointer alignment
    if (frame_addr & 0x3) {
        return false;
    }
    
    // Validate the saved frame pointer and return address
    uint32_t saved_ebp = frame_pointer[0];
    uint32_t return_addr = frame_pointer[1];
    
    // Saved EBP should either be null (bottom of stack) or valid pointer
    if (saved_ebp != 0) {
        if (saved_ebp < frame_addr || saved_ebp > 0x800000) {
            return false;
        }
    }
    
    // Validate return address
    if (!ssp_validate_return_address(return_addr)) {
        ssp_stats.return_address_violations++;
        return false;
    }
    
    return true;
}

// Report SSP violation with detailed information
void ssp_report_violation(const char *function_name, uint32_t expected, uint32_t found) {
    ssp_stats.violations_detected++;
    
    // Use safe direct writing to avoid recursion
    char msg[80];
    int pos = 0;
    
    // Build message carefully
    const char *prefix = "SSP VIOLATION in ";
    while (*prefix && pos < 70) {
        msg[pos++] = *prefix++;
    }
    
    if (function_name) {
        while (*function_name && pos < 70) {
            msg[pos++] = *function_name++;
        }
    }
    
    msg[pos] = '\0';
    
    ssp_emergency_write(msg, 3, 0, 0x4F);
}

// Get SSP statistics
void ssp_get_stats(uint32_t *violations, uint32_t *checks_performed) {
    if (violations) {
        *violations = ssp_stats.violations_detected;
    }
    
    if (checks_performed) {
        *checks_performed = ssp_stats.checks_performed;
    }
}

// Mark a function as safe (debugging aid)
void ssp_mark_function_safe(const char *function_name) {
    // This is primarily for debugging - could maintain a whitelist
    // For now, just acknowledge the call
    (void)function_name; // Suppress unused parameter warning
}

// Periodic canary refresh for enhanced security
void ssp_refresh_canary(void) {
    if (!ssp_initialized) {
        return;
    }
    
    // Generate new canary value periodically
    ssp_generate_random_canary();
    
    print("[SSP] Canary refreshed: 0x");
    print_hex(__stack_chk_guard);
    print("\n");
}