#ifndef IO_PORTS_H
#define IO_PORTS_H

#include <stdint.h>

// Write a byte to the specified port
static inline void outportb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// Read a byte from the specified port
static inline uint8_t inportb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" 
                      : "=a"(ret) 
                      : "Nd"(port));
    return ret;
}

// Write a word to the specified port
static inline void outportw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

// Read a word from the specified port
static inline uint16_t inportw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" 
                      : "=a"(ret) 
                      : "Nd"(port));
    return ret;
}

// Write a dword to the specified port
static inline void outportd(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

// Read a dword from the specified port
static inline uint32_t inportd(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" 
                      : "=a"(ret) 
                      : "Nd"(port));
    return ret;
}

// IO wait function for slow hardware
static inline void io_wait(void) {
    // Port 0x80 is used for POST cards on x86 systems.
    // Writing to it causes a delay needed for slow hardware
    outportb(0x80, 0);
}

#endif // IO_PORTS_H
