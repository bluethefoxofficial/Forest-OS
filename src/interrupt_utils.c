#include "interrupt.h"
#include <stdint.h>

// Simple I/O port functions - needed by many modules
void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %1, %0" : : "dN"(port), "a"(value));
}

uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "dN"(port));
    return result;
}

void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %1, %0" : : "dN"(port), "a"(value));
}

uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile ("inw %1, %0" : "=a"(result) : "dN"(port));
    return result;
}

void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %1, %0" : : "dN"(port), "a"(value));
}

uint32_t inl(uint16_t port) {
    uint32_t result;
    __asm__ volatile ("inl %1, %0" : "=a"(result) : "dN"(port));
    return result;
}

// I/O wait using port 0x80 (POST debug port)
void io_wait(void) {
    outb(0x80, 0);
}

// Send NMI to all processors - stub implementation
void local_apic_send_nmi_all(void) {
    // This would send NMI to all processors
    // Simplified implementation - requires APIC
}

// I/O device management stubs (interrupt_driven_io.c is excluded)
typedef int io_error_t;
typedef struct io_device_descriptor io_device_descriptor_t;

io_error_t io_register_device(const io_device_descriptor_t *descriptor, void *handle) {
    (void)descriptor;
    (void)handle;
    return 0;  // Success
}

void io_process_completions(void) {
    // Stub - no interrupt-driven I/O completions to process
}
