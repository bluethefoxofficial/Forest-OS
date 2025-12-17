#include "include/system.h"
#include "include/string.h"
uint8 inportb (uint16 _port)
{
    	uint8 rv;
    	__asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
    	return rv;
}

void outportb (uint16 _port, uint8 _data)
{
	__asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

void *memset(void *s, int c, size_t n) {
    unsigned char* p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    char *dp = dest;
    const char *sp = src;
    while (n--) *dp++ = *sp++;
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
