; Forest OS Syscall Stub
; Assembly wrapper for system call interrupt

%ifdef __x86_64__
BITS 64
section .text
extern syscall_handler
global isr128
isr128:
    ; Forward to the main 64-bit interrupt stub so we share the same context logic
    jmp syscall_handler

%else
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
    ; After pusha, the stack contains (from low address/ESP to high address):
    ; - DS (saved data segment) <- ESP after push eax
    ; - EDI (from pusha - pushed last)
    ; - ESI (from pusha)
    ; - EBP (from pusha)
    ; - ESP (from pusha - original ESP, not used)
    ; - EBX (from pusha)
    ; - EDX (from pusha)
    ; - ECX (from pusha)
    ; - EAX (from pusha - pushed first)
    ; - Interrupt number (128)
    ; - Dummy error code (0)
    ; - Return address (EIP)
    ; - Code segment (CS)
    ; - Flags (EFLAGS)

    ; Calculate pointer to EDI (syscall_frame_t starts at ESP+4, skipping DS)
    ; Using LEA avoids modifying ESP which would corrupt the saved DS
    lea eax, [esp + 4]     ; EAX = pointer to EDI (syscall_frame_t)
    push eax               ; Pass pointer to syscall frame
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
%endif
