#include <stdint.h>
#include "cpu_ops.h"

uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

uint64_t read_tsc(void) {
    uint32_t low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

uint32_t get_kernel_cs(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return (uint32_t)cs;
}

// Timer delay function placeholder
void timer_delay_ms(uint32_t milliseconds) {
    // Simple delay using CPU cycles - not accurate but functional
    volatile uint64_t target = read_tsc() + (milliseconds * 1000000ULL);
    while (read_tsc() < target) {
        __asm__ volatile ("pause");
    }
}

// Memory mapping placeholder function
void* mm_map_physical_page(uint64_t physical_addr, uint32_t flags) {
    // This is a placeholder - in a real OS this would set up virtual memory mapping
    // For now, just return the physical address as virtual (identity mapping)
    return (void*)(uintptr_t)physical_addr;
}