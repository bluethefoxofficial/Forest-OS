/*
 * Forest OS UEFI Boot Entry Point
 * This file provides UEFI-specific boot initialization
 */

#ifdef UEFI_BOOT

#include <stdint.h>
#include <stdbool.h>

// UEFI Basic Types
typedef uint64_t UINTN;
typedef uint64_t EFI_STATUS;
typedef void* EFI_HANDLE;
typedef struct _EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;

// EFI Status Codes
#define EFI_SUCCESS 0

// Forward declaration for kernel main
extern int kernel_main(void);

/*
 * UEFI Application Entry Point
 * This is called by the UEFI firmware
 */
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    // Store UEFI handles for later use
    // (These would be used by UEFI-specific drivers)
    
    // Initialize basic UEFI services if needed
    // This is where you'd set up console output, memory services, etc.
    
    // Call the main kernel entry point
    kernel_main();
    
    // Should never return, but if it does, return success
    return EFI_SUCCESS;
}

/*
 * Alternative entry point name for compatibility
 */
EFI_STATUS _start(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
    __attribute__((alias("efi_main")));

#endif // UEFI_BOOT