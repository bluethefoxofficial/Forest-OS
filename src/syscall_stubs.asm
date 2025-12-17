; Forest OS Syscall Stub
; Assembly wrapper for system call interrupt

section .text
align 4

extern syscall_handle
extern g_kernel_data_selector

global isr128
isr128:
    cli                    ; Disable interrupts
    push byte 0            ; Push dummy error code
    push byte 128          ; Push interrupt number (0x80)
    
    pusha                  ; Push all general purpose registers
    
    mov ax, ds             ; Save data segment
    push eax
    
    mov ax, [g_kernel_data_selector]
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Create syscall frame structure on stack
    ; The stack now contains (from top to bottom):
    ; - Data segment (DS)
    ; - General purpose registers (pusha)
    ; - Interrupt number (128)
    ; - Dummy error code (0)
    ; - Return address (EIP)
    ; - Code segment (CS)
    ; - Flags (EFLAGS)
    
    push esp               ; Pass pointer to register frame
    call syscall_handle    ; Call C handler
    add esp, 4             ; Remove frame pointer from stack
    
    pop eax                ; Restore data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    popa                   ; Pop all general purpose registers
    add esp, 8             ; Remove error code and interrupt number
    sti                    ; Re-enable interrupts
    iret                   ; Return from interrupt
