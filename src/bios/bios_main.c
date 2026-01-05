/*
 * Forest OS BIOS Boot Entry Point
 * This file provides BIOS-specific boot initialization
 */

#ifdef BIOS_BOOT

#include <stdint.h>
#include <stdbool.h>

// Forward declaration for kernel main
extern int kernel_main(void);

/*
 * BIOS Boot Entry Point
 * This is called after the bootloader has loaded the kernel
 */
void bios_main(void)
{
    // BIOS-specific initialization can go here
    // For example: setting up A20 line, getting memory map from GRUB, etc.
    
    // Call the main kernel entry point
    kernel_main();
    
    // Should never return
    while (1) {
        asm volatile("hlt");
    }
}

/*
 * Alternative entry point for compatibility with existing code
 */
void _start(void) __attribute__((alias("bios_main")));

#endif // BIOS_BOOT