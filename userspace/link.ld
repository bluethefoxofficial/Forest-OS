OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FILEHDR PHDRS FLAGS(0x5); /* RX */
    data PT_LOAD FLAGS(0x6);               /* RW */
}

SECTIONS
{
    /* Keep binaries aligned inside user space and leave room for ELF headers. */
    . = 0x40001000;

    .text ALIGN(0x1000) : {
        *(.text*)
        *(.rodata*)
        *(.eh_frame*)
        *(.gcc_except_table*)
    } :text

    .data ALIGN(0x1000) : {
        *(.data*)
        *(.ctors*)
        *(.dtors*)
        *(.init_array*)
        *(.fini_array*)
    } :data

    .bss ALIGN(0x1000) : {
        *(COMMON)
        *(.bss*)
    } :data
}
