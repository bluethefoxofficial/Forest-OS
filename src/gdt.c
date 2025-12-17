#include "include/gdt.h"
#include "include/util.h"

#define GDT_ENTRY_COUNT 6

typedef struct {
    uint16 limit_low;
    uint16 base_low;
    uint8  base_mid;
    uint8  access;
    uint8  granularity;
    uint8  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16 limit;
    uint32 base;
} __attribute__((packed)) gdt_descriptor_t;

typedef struct {
    uint32 prev_tss;
    uint32 esp0;
    uint32 ss0;
    uint32 esp1;
    uint32 ss1;
    uint32 esp2;
    uint32 ss2;
    uint32 cr3;
    uint32 eip;
    uint32 eflags;
    uint32 eax;
    uint32 ecx;
    uint32 edx;
    uint32 ebx;
    uint32 esp;
    uint32 ebp;
    uint32 esi;
    uint32 edi;
    uint16 es;
    uint16 reserved0;
    uint16 cs;
    uint16 reserved1;
    uint16 ss;
    uint16 reserved2;
    uint16 ds;
    uint16 reserved3;
    uint16 fs;
    uint16 reserved4;
    uint16 gs;
    uint16 reserved5;
    uint16 ldt;
    uint16 reserved6;
    uint16 trap;
    uint16 iomap_base;
} __attribute__((packed)) tss_entry_t;

static gdt_entry_t gdt[GDT_ENTRY_COUNT];
static gdt_descriptor_t gdt_desc;
static tss_entry_t tss_entry;

static void gdt_set_entry(int index, uint32 base, uint32 limit, uint8 access, uint8 granularity) {
    gdt[index].base_low    = base & 0xFFFF;
    gdt[index].base_mid    = (base >> 16) & 0xFF;
    gdt[index].base_high   = (base >> 24) & 0xFF;
    gdt[index].limit_low   = limit & 0xFFFF;
    gdt[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    gdt[index].access      = access;
}

static void gdt_set_tss(int index, uint32 base, uint32 limit) {
    gdt_set_entry(index, base, limit, 0x89, 0x00);
}

static void gdt_flush_and_load_tss(void) {
    uint16 data_selector = GDT_KERNEL_DATA_SELECTOR;
    uint32 code_selector = GDT_KERNEL_CODE_SELECTOR;
    uint16 tss_selector  = GDT_TSS_SELECTOR;

    __asm__ __volatile__ (
        "lgdt (%0)\n"
        "mov %1, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "pushl %2\n"
        "lea 1f, %%eax\n"
        "pushl %%eax\n"
        "lret\n"
        "1:\n"
        "ltr %3\n"
        :
        : "r"(&gdt_desc),
          "r"(data_selector),
          "r"(code_selector),
          "r"(tss_selector)
        : "eax"
    );
}

void gdt_set_kernel_stack(uint32 stack_top) {
    tss_entry.esp0 = stack_top;
}

void gdt_init(uint32 initial_stack_top) {
    memory_set((uint8*)&tss_entry, 0, sizeof(tss_entry));

    gdt_set_entry(0, 0, 0, 0, 0);                    // Null descriptor
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xCF);        // Kernel code
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xCF);        // Kernel data
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xCF);        // User code
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xCF);        // User data

    tss_entry.ss0 = GDT_KERNEL_DATA_SELECTOR;
    gdt_set_kernel_stack(initial_stack_top);
    tss_entry.cs = GDT_USER_CODE_SELECTOR;
    tss_entry.ss = tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs = GDT_USER_DATA_SELECTOR;
    tss_entry.iomap_base = sizeof(tss_entry);

    gdt_set_tss(5, (uint32)&tss_entry, sizeof(tss_entry) - 1);

    gdt_desc.limit = sizeof(gdt) - 1;
    gdt_desc.base  = (uint32)&gdt;

    gdt_flush_and_load_tss();
}
