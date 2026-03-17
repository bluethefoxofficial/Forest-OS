/* =============================================================================
 * UEFI x86-64 Linker Script for Forest OS
 * =============================================================================
 * This linker script creates a UEFI PE32+ compatible binary layout
 * =============================================================================
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH("i386:x86-64")
ENTRY(efi_main)

SECTIONS
{
    . = 0x100000;
    kernel_start = .;

    /* Keep multiboot headers inside the first 8 KiB for GRUB */
    .multiboot ALIGN(8) : {
        KEEP(*(.multiboot))
    }

    .text : {
        _text_start = .;
        *(.text.startup)
        *(.text)
        *(.text.*)
        . = ALIGN(4096);
        _text_end = .;
    }
    
    .rodata : {
        _rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4096);
        _rodata_end = .;
    }
    
    .data : {
        _data_start = .;
        *(.data)
        *(.data.*)
        . = ALIGN(4096);
        _data_end = .;
    }
    
    .sdata : {
        *(.sdata)
        *(.sdata.*)
    }
    
    .bss : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4096);
        _bss_end = .;
    }
    
    .dynamic : {
        *(.dynamic)
    }
    
    .dynsym : {
        *(.dynsym)
    }
    
    .dynstr : {
        *(.dynstr)
    }
    
    .rel : {
        *(.rel.*)
    }
    
    .rela : {
        *(.rela.*)
    }
    
    .reloc : {
        *(.reloc)
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
