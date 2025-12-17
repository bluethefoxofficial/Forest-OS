OUTPUT_FORMAT(elf32-i386)
ENTRY(start)
SECTIONS
{
    . = 0x100000;
    kernel_start = .;
    .multiboot_header : {
        *(.multiboot_header)
    }
    .text : {
        *(.text)
    }
    .data : { *(.data) }
    .bss  : { *(.bss)  }
    . = ALIGN(4096);
    .stack : {
        _stack_bottom = .;
        . += 16384; /* 16KB stack */
        _stack_top = .;
    }
    kernel_end = .;
}
