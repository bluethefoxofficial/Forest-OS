OUTPUT_FORMAT(elf32-i386)
ENTRY(start)
SECTIONS
{
    . = 0x100000;
    kernel_start = .;

    /* Place multiboot headers at the very start of the image */
    .multiboot ALIGN(8) : {
        KEEP(*(.multiboot))
    }

    .text : {
        *(.text)
    }
    
    /* Align data section after text - don't use fixed address */
    . = ALIGN(4096);
    .data : { *(.data) }
    .eh_frame : { *(.eh_frame) }
    .bss  : { 
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
}
