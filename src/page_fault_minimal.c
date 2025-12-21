#include "include/memory.h"
#include "include/screen.h"
#include "include/panic.h"
#include "include/stack_protection.h"
#include "include/secure_vmm.h"
#include "include/memory_region_manager.h"
#include "include/page_fault_recovery.h"

// =============================================================================
// MINIMAL PAGE FAULT HANDLER - RECURSION SAFE
// =============================================================================
// This handler is designed to prevent recursive page faults that can occur
// when the original handler tries to allocate memory during fault processing.
// =============================================================================

// Recursion protection
static volatile int page_fault_in_progress = 0;
static volatile int recursion_depth = 0;

// Simple VGA direct writing to avoid heap allocation
static void write_char_direct(char c, int x, int y, uint8_t color) {
    volatile uint16_t *video = (volatile uint16_t*)0xB8000;
    video[y * 80 + x] = (color << 8) | c;
}

static void write_string_direct(const char *str, int x, int y, uint8_t color) {
    for (int i = 0; str[i] != '\0' && x + i < 80; i++) {
        write_char_direct(str[i], x + i, y, color);
    }
}

static void write_hex_direct(uint32_t value, int x, int y, uint8_t color) {
    const char hex_chars[] = "0123456789ABCDEF";
    char buffer[11] = "0x";
    
    for (int i = 7; i >= 0; i--) {
        buffer[2 + (7 - i)] = hex_chars[(value >> (i * 4)) & 0xF];
    }
    buffer[10] = '\0';
    
    write_string_direct(buffer, x, y, color);
}

static void panic_format_hex(uint32_t value, char* out) {
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 8; i++) {
        out[2 + i] = digits[(value >> (28 - i * 4)) & 0xF];
    }
    out[10] = '\0';
}

// Minimal page fault handler that avoids recursion
void page_fault_handler_minimal(uint32 fault_addr, uint32 error_code, struct interrupt_frame* frame) {
    // Use minimal stack protection for IRQ handlers
    STACK_PROTECT_IRQ();
    
    // Prevent recursion - critical section
    if (__sync_fetch_and_add(&page_fault_in_progress, 1) > 0) {
        // We're already in a page fault - this is recursion!
        recursion_depth++;
        
        // Write directly to video memory to avoid any heap allocation
        write_string_direct("RECURSIVE PAGE FAULT DETECTED!", 0, 20, 0x4F); // White on red
        write_hex_direct(fault_addr, 32, 20, 0x4F);
        write_string_direct("DEPTH:", 0, 21, 0x4F);
        write_hex_direct(recursion_depth, 7, 21, 0x4F);
        
        if (recursion_depth > 5) {
            // Too many levels - force halt
            write_string_direct("STACK OVERFLOW - HALTING SYSTEM", 0, 22, 0x4F);
            __asm__ volatile("cli; hlt");
        }
        
        __sync_fetch_and_sub(&page_fault_in_progress, 1);
        return; // Exit without panicking to break the recursion
    }

    if (secure_vmm_handle_page_fault(fault_addr, error_code) == 0) {
        __sync_fetch_and_sub(&page_fault_in_progress, 1);
        return;
    }
    
    // Attempt intelligent recovery before panicking
    if (page_fault_attempt_recovery(fault_addr, error_code)) {
        write_string_direct("PAGE FAULT RECOVERED:", 0, 14, 0x0A); // Green on black
        write_hex_direct(fault_addr, 22, 14, 0x0A);
        __sync_fetch_and_sub(&page_fault_in_progress, 1);
        return; // Recovery successful, continue execution
    }
    
    memory_debug_report_fault(fault_addr, error_code);
    
    // Get intelligent region analysis
    char region_analysis[128];
    memory_analyze_page_fault(fault_addr, error_code, region_analysis, sizeof(region_analysis));
    
    // Write fault information directly to screen
    write_string_direct("PAGE FAULT:", 0, 15, 0x0C); // Red on black
    write_hex_direct(fault_addr, 12, 15, 0x0C);
    
    write_string_direct("ERROR CODE:", 0, 16, 0x0C);
    write_hex_direct(error_code, 12, 16, 0x0C);
    
    // Determine fault type
    if (!(error_code & 0x01)) {
        write_string_direct("Page not present", 25, 16, 0x0C);
    } else {
        write_string_direct("Protection violation", 25, 16, 0x0C);
    }
    
    // Check if this is a stack overflow
    uint32_t esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(esp));
    
    if (esp < 0x00100000 || esp > 0x00800000) {
        write_string_direct("STACK OVERFLOW DETECTED", 0, 17, 0x4F);
        write_hex_direct(esp, 25, 17, 0x4F);
    }
    
    // Display intelligent region analysis
    write_string_direct(region_analysis, 0, 18, 0x0E); // Yellow on black
    
    // Check for specific fault patterns
    if (fault_addr < 0x1000) {
        write_string_direct("NULL POINTER DEREFERENCE", 0, 19, 0x4F);
    } else if ((fault_addr & 0xFF000000) == 0xAA000000) {
        write_string_direct("HEAP CORRUPTION - UNINITIALIZED MEMORY", 0, 19, 0x4F);
    } else if ((fault_addr & 0xFF000000) == 0xDE000000) {
        write_string_direct("USE-AFTER-FREE DETECTED", 0, 19, 0x4F);
    }
    
    // Reset recursion protection
    __sync_fetch_and_sub(&page_fault_in_progress, 1);
    
    // Create panic message without using strcpy/strcat to avoid recursion
    char panic_msg[96];
    char addr_buf[11];
    char err_buf[11];
    panic_format_hex(fault_addr, addr_buf);
    panic_format_hex(error_code, err_buf);
    
    // Manual string construction to avoid any memory validation dependencies
    char* p = panic_msg;
    const char* prefix = "Page fault @";
    for (int i = 0; prefix[i] != '\0'; i++) *p++ = prefix[i];
    for (int i = 0; addr_buf[i] != '\0'; i++) *p++ = addr_buf[i];
    const char* middle = " err=";
    for (int i = 0; middle[i] != '\0'; i++) *p++ = middle[i];
    for (int i = 0; err_buf[i] != '\0'; i++) *p++ = err_buf[i];
    *p = '\0';
    panic_preload_fault_info(fault_addr, error_code, frame->eip, frame->cs, frame->eflags);
    kernel_panic(panic_msg);
}

// External interface that matches the expected signature
void page_fault_handler(struct interrupt_frame* frame, uint32 error_code) {
    uint32 fault_addr;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(fault_addr));
    page_fault_handler_minimal(fault_addr, error_code, frame);
}
