#include "include/system.h"
#include "include/string.h"
#include "include/memory_safe.h"

static size_t guarded_copy_span(const void* dest, const void* src, size_t length) {
    size_t dest_span = memory_is_user_pointer(dest) ? memory_probe_user_buffer(dest, length)
                                                    : memory_probe_buffer(dest, length);
    size_t src_span = memory_is_user_pointer(src) ? memory_probe_user_buffer(src, length)
                                                  : memory_probe_buffer(src, length);
    size_t span = (dest_span < src_span) ? dest_span : src_span;
    return (span < length) ? span : length;
}

static inline bool io_port_in_range(uint16 port) {
    return port < 0x10000;
}
uint8 inportb (uint16 _port)
{
        uint8 rv = 0;
        if (!io_port_in_range(_port)) {
            return rv;
        }
        __asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
        return rv;
}

void outportb (uint16 _port, uint8 _data)
{
        if (!io_port_in_range(_port)) {
            return;
        }
        __asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

void *memset(void *s, int c, size_t n) {
    unsigned char* p = s;
    size_t span = memory_is_user_pointer(s) ? memory_probe_user_buffer(s, n)
                                            : memory_probe_buffer(s, n);
    for (size_t i = 0; i < span; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    char *dp = dest;
    const char *sp = src;
    size_t span = guarded_copy_span(dest, src, n);
    for (size_t i = 0; i < span; i++) {
        dp[i] = sp[i];
    }
    return dest;
}
uint32 cpu_get_cr0(void) {
    uint32 value;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(value));
    return value;
}

void cpu_set_cr0(uint32 value) {
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(value));
}

void cpu_set_cr3(uint32 value) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(value));
}
