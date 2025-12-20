#include "include/memory.h"
#include "include/screen.h"
#include "include/panic.h"

// Direct VGA memory writing function for emergency situations
static void write_string_direct(const char *str, int x, int y, uint8_t color) {
    uint16_t *vga_memory = (uint16_t*)0xB8000;
    int pos = y * 80 + x;
    
    while (*str && pos < 80 * 25) {
        vga_memory[pos] = ((uint16_t)color << 8) | *str;
        str++;
        pos++;
    }
}

// =============================================================================
// STACK OVERFLOW PROTECTION
// =============================================================================
// Implements stack canaries and guard pages to detect stack overflow
// =============================================================================

#define STACK_CANARY_VALUE      0xDEADBEEF
#define STACK_GUARD_SIZE        4096        // One page guard
#define STACK_WARNING_THRESHOLD 1024       // Warn when less than 1KB left

// Stack information structure
typedef struct stack_info {
    uint32_t base;           // Stack base (top)
    uint32_t limit;          // Stack limit (bottom)  
    uint32_t current;        // Current ESP
    uint32_t canary_addr;    // Address of stack canary
    uint32_t guard_page;     // Guard page address
    bool initialized;
} stack_info_t;

static stack_info_t kernel_stack = {0};

// Initialize stack protection for the kernel stack
void stack_protection_init(void) {
    uint32_t esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(esp));
    
    // Estimate stack boundaries (kernel typically uses 8KB stack)
    kernel_stack.current = esp;
    kernel_stack.base = (esp + 4095) & ~4095;      // Round up to page
    kernel_stack.limit = kernel_stack.base - (8 * 1024); // 8KB stack
    kernel_stack.guard_page = kernel_stack.limit - STACK_GUARD_SIZE;
    kernel_stack.canary_addr = kernel_stack.limit + 16; // Just above limit
    
    // Place stack canary
    *((uint32_t*)kernel_stack.canary_addr) = STACK_CANARY_VALUE;
    
    kernel_stack.initialized = true;
    
    print("[STACK] Protection initialized - Base: 0x");
    print_hex(kernel_stack.base);
    print(", Limit: 0x");
    print_hex(kernel_stack.limit);
    print("\n");
}

// Check if stack is in good condition
bool stack_check_integrity(void) {
    if (!kernel_stack.initialized) {
        return true; // Can't check if not initialized
    }
    
    uint32_t current_esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(current_esp));
    
    kernel_stack.current = current_esp;
    
    // Check if ESP is within valid range
    if (current_esp < kernel_stack.limit || current_esp > kernel_stack.base) {
        return false;
    }
    
    // Check stack canary
    if (*((uint32_t*)kernel_stack.canary_addr) != STACK_CANARY_VALUE) {
        return false;
    }
    
    return true;
}

// Check for stack overflow with warning
int stack_check_overflow(void) {
    if (!kernel_stack.initialized) {
        return 0; // No overflow if not initialized
    }
    
    uint32_t current_esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(current_esp));
    
    // Calculate remaining stack space
    if (current_esp < kernel_stack.limit) {
        return -1; // Stack overflow!
    }
    
    uint32_t remaining = current_esp - kernel_stack.limit;
    
    if (remaining < STACK_WARNING_THRESHOLD) {
        return 1; // Stack warning
    }
    
    return 0; // Stack OK
}

// Enhanced function prologue with stack checking (use as macro)
#define STACK_CHECK_ENTER() do { \
    int overflow = stack_check_overflow(); \
    if (overflow < 0) { \
        write_string_direct("STACK OVERFLOW DETECTED", 0, 23, 0x4F); \
        kernel_panic("Stack overflow detected"); \
    } else if (overflow > 0) { \
        static int warning_count = 0; \
        if (++warning_count < 5) { \
            print("[STACK] WARNING: Low stack space\n"); \
        } \
    } \
} while(0)

// Get stack usage statistics
void stack_get_stats(uint32_t *total_size, uint32_t *used_size, uint32_t *remaining) {
    if (!kernel_stack.initialized) {
        if (total_size) *total_size = 0;
        if (used_size) *used_size = 0;
        if (remaining) *remaining = 0;
        return;
    }
    
    uint32_t current_esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(current_esp));
    
    uint32_t total = kernel_stack.base - kernel_stack.limit;
    uint32_t used = kernel_stack.base - current_esp;
    uint32_t remain = current_esp - kernel_stack.limit;
    
    if (total_size) *total_size = total;
    if (used_size) *used_size = used;
    if (remaining) *remaining = remain;
}

// Emergency stack switching for critical situations
static uint8_t emergency_stack[2048] __attribute__((aligned(16)));
static uint32_t *emergency_stack_top = (uint32_t*)(emergency_stack + sizeof(emergency_stack) - 4);

void switch_to_emergency_stack(void) {
    __asm__ volatile(
        "mov %0, %%esp\n\t"
        "mov %%esp, %%ebp"
        :
        : "r"(emergency_stack_top)
        : "memory"
    );
}

// Stack protection for specific functions (use in critical functions)
static inline void __attribute__((always_inline)) stack_protect_function(void) {
    STACK_CHECK_ENTER();
}

// Export the macro for use in headers
#ifndef __STACK_PROTECTION_H__
#define STACK_PROTECT() stack_protect_function()
#endif

// Quick stack bounds check (for use in page fault handler)
bool stack_quick_check(uint32_t esp) {
    // Quick bounds check without complex operations
    return (esp >= 0x00100000 && esp <= 0x00800000);
}