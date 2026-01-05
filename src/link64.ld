/* 64-bit BIOS Linker Script for Forest OS */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(start)

SECTIONS
{
    . = 0x100000;
    kernel_start = .;

    .multiboot_header : {
        *(.multiboot_header)
    }

    .text : ALIGN(4096) {
        *(.text.boot)
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4096) {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(4096) {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    }

    . = ALIGN(4096);
    .stack : {
        _stack_bottom = .;
        . += 16384; /* 16KB stack */
        _stack_top = .;
    }

    kernel_end = .;

    /DISCARD/ : {
        *(.note.*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
