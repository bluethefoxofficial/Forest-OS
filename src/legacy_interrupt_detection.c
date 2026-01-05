#include "legacy_interrupt_detection.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool pic_present;
    bool apic_available;
    interrupt_mode_t current_mode;
    bool initialized;
} legacy_detect_context_t;

static legacy_detect_context_t legacy_ctx = {0};

legacy_interrupt_error_t legacy_interrupt_detect(void) {
    // Check for APIC presence
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    
    legacy_ctx.apic_available = (edx & (1 << 9)) != 0;
    legacy_ctx.pic_present = true; // Assume PIC is always present
    
    if (legacy_ctx.apic_available) {
        legacy_ctx.current_mode = INTERRUPT_MODE_APIC;
    } else {
        legacy_ctx.current_mode = INTERRUPT_MODE_PIC;
    }
    
    legacy_ctx.initialized = true;
    return LEGACY_INTERRUPT_SUCCESS;
}

interrupt_mode_t legacy_interrupt_get_mode(void) {
    return legacy_ctx.current_mode;
}

bool legacy_interrupt_pic_available(void) {
    return legacy_ctx.pic_present;
}
