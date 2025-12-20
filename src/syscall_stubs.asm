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
    
    ; Create syscall frame structure on stack matching the syscall_frame_t structure
    ; After pusha, the stack contains (from top to bottom after DS push):
    ; - DS (saved data segment)
    ; - EDI (from pusha)
    ; - ESI (from pusha) 
    ; - EBP (from pusha)
    ; - ESP (from pusha - original ESP)
    ; - EBX (from pusha)
    ; - EDX (from pusha)
    ; - ECX (from pusha)
    ; - EAX (from pusha)
    ; - Interrupt number (128)
    ; - Dummy error code (0)
    ; - Return address (EIP)
    ; - Code segment (CS)
    ; - Flags (EFLAGS)
    
    ; ESP now points to the syscall frame (after the DS push)
    ; Adjust ESP to point to EAX (which is the first field in syscall_frame_t)
    add esp, 4             ; Skip the DS we pushed
    push esp               ; Pass pointer to syscall frame (starting at EAX)
    call syscall_handle    ; Call C handler
    add esp, 4             ; Remove frame pointer from stack
    sub esp, 4             ; Restore ESP to point to DS
    
    pop eax                ; Restore data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    popa                   ; Pop all general purpose registers
    add esp, 8             ; Remove error code and interrupt number
    sti                    ; Re-enable interrupts
    iret                   ; Return from interrupt
