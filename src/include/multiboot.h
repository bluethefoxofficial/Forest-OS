#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "types.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36d76289

#define MULTIBOOT_FLAG_MEM       0x001
#define MULTIBOOT_FLAG_MMAP      0x040
#define MULTIBOOT_FLAG_FRAMEBUFFER 0x1000

#define MULTIBOOT_MEMORY_AVAILABLE      1
#define MULTIBOOT_MEMORY_RESERVED       2
#define MULTIBOOT_MEMORY_ACPI_RECLAIM   3
#define MULTIBOOT_MEMORY_ACPI_NVS       4
#define MULTIBOOT_MEMORY_BADRAM         5

typedef struct multiboot_info {
    uint32 flags;
    uint32 mem_lower;
    uint32 mem_upper;
    uint32 boot_device;
    uint32 cmdline;
    uint32 mods_count;
    uint32 mods_addr;
    uint32 syms[4];
    uint32 mmap_length;
    uint32 mmap_addr;
    uint32 drives_length;
    uint32 drives_addr;
    uint32 config_table;
    uint32 boot_loader_name;
    uint32 apm_table;
    uint32 vbe_control_info;
    uint32 vbe_mode_info;
    uint16 vbe_mode;
    uint16 vbe_interface_seg;
    uint16 vbe_interface_off;
    uint16 vbe_interface_len;
    uint64 framebuffer_addr;
    uint32 framebuffer_pitch;
    uint32 framebuffer_width;
    uint32 framebuffer_height;
    uint8 framebuffer_bpp;
    uint8 framebuffer_type;
    uint16 framebuffer_color_info;
} __attribute__((packed)) multiboot_info_t;

typedef struct multiboot_mmap_entry {
    uint32 size;
    uint32 addr_low;
    uint32 addr_high;
    uint32 len_low;
    uint32 len_high;
    uint32 type;
} __attribute__((packed)) multiboot_mmap_entry_t;

typedef struct multiboot_module {
    uint32 mod_start;
    uint32 mod_end;
    uint32 string;
    uint32 reserved;
} __attribute__((packed)) multiboot_module_t;

// Multiboot 2 structures (minimal subset)
// Tag types per Multiboot2 specification
#define MULTIBOOT2_TAG_END           0
#define MULTIBOOT2_TAG_CMDLINE       1
#define MULTIBOOT2_TAG_BOOT_LOADER   2
#define MULTIBOOT2_TAG_MODULE        3
#define MULTIBOOT2_TAG_BASIC_MEMINFO 4
#define MULTIBOOT2_TAG_BOOTDEV       5   // BIOS boot device
#define MULTIBOOT2_TAG_MMAP          6
#define MULTIBOOT2_TAG_VBE           7   // VBE info (legacy)
#define MULTIBOOT2_TAG_FRAMEBUFFER   8   // Framebuffer info (CRITICAL: was incorrectly 5!)

typedef struct multiboot2_info {
    uint32 total_size;
    uint32 reserved;
} __attribute__((packed)) multiboot2_info_t;

typedef struct multiboot2_tag {
    uint32 type;
    uint32 size;
} __attribute__((packed)) multiboot2_tag_t;

typedef struct multiboot2_tag_module {
    multiboot2_tag_t tag;
    uint32 mod_start;
    uint32 mod_end;
    char cmdline[];
} __attribute__((packed)) multiboot2_tag_module_t;

typedef struct multiboot2_tag_basic_mem {
    multiboot2_tag_t tag;
    uint32 mem_lower;
    uint32 mem_upper;
} __attribute__((packed)) multiboot2_tag_basic_mem_t;

typedef struct multiboot2_tag_mmap {
    multiboot2_tag_t tag;
    uint32 entry_size;
    uint32 entry_version;
} __attribute__((packed)) multiboot2_tag_mmap_t;

typedef struct multiboot2_mmap_entry {
    uint64 base_addr;
    uint64 length;
    uint32 type;
    uint32 reserved;
} __attribute__((packed)) multiboot2_mmap_entry_t;

typedef struct multiboot2_tag_framebuffer {
    multiboot2_tag_t tag;
    uint64 framebuffer_addr;
    uint32 framebuffer_pitch;
    uint32 framebuffer_width;
    uint32 framebuffer_height;
    uint8 framebuffer_bpp;
    uint8 framebuffer_type;
    uint16 reserved;
} __attribute__((packed)) multiboot2_tag_framebuffer_t;

typedef struct multiboot2_tag_string {
    multiboot2_tag_t tag;
    char string[];
} __attribute__((packed)) multiboot2_tag_string_t;

#endif
