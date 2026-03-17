/* 64-bit BIOS Linker Script for Forest OS */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(start)

SECTIONS
{
    . = 0x100000;
    kernel_start = .;

    /* Ensure multiboot headers stay in the first 8 KiB */
    .multiboot ALIGN(8) : {
        KEEP(*(.multiboot))
    }

    .text : {
        *(.text.boot)
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    . = LOADADDR(.rodata) + SIZEOF(.rodata);

    .data : AT(.) {
        *(.data)
        *(.data.*)
    }

    .eh_frame : { *(.eh_frame) }

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
        *(.eh_frame_hdr)
    }
}
