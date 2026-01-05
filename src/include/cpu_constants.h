#ifndef CPU_CONSTANTS_H
#define CPU_CONSTANTS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* CR0 Register Bits */
#define CR0_PE      (1 << 0)   /* Protection Enable */
#define CR0_MP      (1 << 1)   /* Monitor Coprocessor */
#define CR0_EM      (1 << 2)   /* Emulation */
#define CR0_TS      (1 << 3)   /* Task Switched */
#define CR0_ET      (1 << 4)   /* Extension Type */
#define CR0_NE      (1 << 5)   /* Numeric Error */
#define CR0_WP      (1 << 16)  /* Write Protect */
#define CR0_AM      (1 << 18)  /* Alignment Mask */
#define CR0_NW      (1 << 29)  /* Not Write-through */
#define CR0_CD      (1 << 30)  /* Cache Disable */
#define CR0_PG      (1 << 31)  /* Paging */

/* CPU Feature Flags */
#define CPU_FEATURE_FPU         (1 << 0)
#define CPU_FEATURE_VME         (1 << 1)
#define CPU_FEATURE_DE          (1 << 2)
#define CPU_FEATURE_PSE         (1 << 3)
#define CPU_FEATURE_TSC         (1 << 4)
#define CPU_FEATURE_MSR         (1 << 5)
#define CPU_FEATURE_PAE         (1 << 6)
#define CPU_FEATURE_MCE         (1 << 7)
#define CPU_FEATURE_APIC        (1 << 9)
#define CPU_FEATURE_SEP         (1 << 11)

/* Memory allocation flags */
#define GFP_KERNEL      0x01    /* Kernel memory allocation */
#define GFP_ATOMIC      0x02    /* Atomic allocation */
#define GFP_USER        0x04    /* User memory allocation */

/* Kernel base address for 32-bit */
#ifndef KERNEL_BASE
#define KERNEL_BASE     0xC0000000
#endif

/* CPU Register Access Functions */
#if ARCH_64BIT
static inline uint64_t read_cr0(void) {
    uint64_t val;
    __asm__ volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

static inline void write_cr0(uint64_t val) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(val) : "memory");
}

static inline uint64_t read_dr6(void) {
    uint64_t val;
    __asm__ volatile("mov %%dr6, %0" : "=r"(val));
    return val;
}

static inline void write_dr6(uint64_t val) {
    __asm__ volatile("mov %0, %%dr6" : : "r"(val) : "memory");
}
#else
static inline uint32_t read_cr0(void) {
    uint32_t val;
    __asm__ volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

static inline void write_cr0(uint32_t val) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(val) : "memory");
}

static inline uint32_t read_dr6(void) {
    uint32_t val;
    __asm__ volatile("mov %%dr6, %0" : "=r"(val));
    return val;
}

static inline void write_dr6(uint32_t val) {
    __asm__ volatile("mov %0, %%dr6" : : "r"(val) : "memory");
}
#endif

/* Get kernel code segment selector - if not already declared elsewhere */
#ifndef GET_KERNEL_CS_DECLARED
#define GET_KERNEL_CS_DECLARED
/* Note: get_kernel_cs() is implemented in cpu_utils.c and declared in cpu_ops.h */
/* Use the cpu_ops.h declaration for consistency */
#endif

/* Port I/O functions - if not already declared elsewhere */
#ifndef INB_DECLARED
#define INB_DECLARED
/* Note: inb/outb are declared in interrupt.h and system.h */
/* Avoid duplicate inline definitions here */
#endif

#endif /* CPU_CONSTANTS_H */
