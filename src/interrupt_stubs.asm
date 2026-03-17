bits 32

section .text

extern interrupt_common_handler
extern g_kernel_data_selector
extern isr128

; -----------------------------------------------------------------------------
; GDB-Friendly Interrupt Entry Point
; 
; This version sets up proper stack frames so GDB can do complete backtraces.
; Key changes:
;   1. Push return address for GDB to find caller
;   2. Set up frame pointer (push ebp / mov ebp, esp)
;   3. For faults, adjust return address (+1) so GDB shows correct source line
;
; Stack layout on entry (CPU-pushed):
;   [esp + 0]  -> interrupt number
;   [esp + 4]  -> error code (real or synthetic)
;   [esp + 8]  -> return EIP
;   [esp + 12] -> return CS
;   [esp + 16] -> EFLAGS
; -----------------------------------------------------------------------------

isr_common_stub:
    ; Save original ESP in ebx (callee-saved register)
    push ebx
    mov ebx, esp
    
    ; Check if this is a fault exception (has CPU-pushed error code)
    ; Faults: 8, 10, 11, 12, 13, 14, 17, 21, 29, 30
    ; For faults, EIP points to faulting instruction
    ; For GDB backtrace to show correct line, we need to add 1 to EIP
    mov eax, [ebx + 0]          ; Get interrupt number
    mov edx, 0                  ; Default: no adjustment
    
    cmp eax, 8
    je .is_fault
    cmp eax, 10
    je .is_fault
    cmp eax, 11
    je .is_fault
    cmp eax, 12
    je .is_fault
    cmp eax, 13
    je .is_fault
    cmp eax, 14
    je .is_fault
    cmp eax, 17
    je .is_fault
    cmp eax, 21
    je .is_fault
    cmp eax, 29
    je .is_fault
    cmp eax, 30
    jne .setup_frame
    
.is_fault:
    mov edx, 1                  ; Add 1 to return address for faults

.setup_frame:
    ; Standard register saves
    pusha
    push ds
    push es
    push fs
    push gs

    ; Set up GDB-friendly frame pointer
    ; Push adjusted return address and old EBP
    ; After pusha: ESP + 32 = original ESP
    ; After segment regs: ESP + 48 = original ESP
    ; We saved original ESP in ebx
    
    ; Get the return address and apply any adjustment
    mov ecx, [ebx + 8]         ; Original return EIP
    add ecx, edx                ; Apply fault adjustment if needed
    
    ; Push for GDB frame
    push ecx                    ; Adjusted return address
    push ebp                    ; Old EBP
    mov ebp, esp                ; New frame pointer
    
    ; Load kernel data segment
    mov ax, [g_kernel_data_selector]
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Get interrupt info
    mov eax, [ebx + 0]          ; Interrupt number
    mov edx, [ebx + 4]          ; Error code
    lea ecx, [ebx + 8]          ; Pointer to interrupt frame

    ; Call handler
    push edx                    ; error_code
    push ecx                    ; frame pointer  
    push eax                    ; interrupt number
    call interrupt_common_handler
    add esp, 12

    ; Restore frame and clean up
    pop ebp                     ; Restore old EBP
    add esp, 4                 ; Remove pushed return address
    
    ; Restore registers
    pop gs
    pop fs
    pop es
    pop ds
    popa
    
    ; Restore original ESP (saved at start)
    pop ebx
    
    ; Clean up interrupt number + error code pushed by stub entry
    add esp, 8
    
    iret

%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push dword 0                 ; Synthetic error code
    push dword %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    push dword %1
    jmp isr_common_stub
%endmacro

; Exceptions that push an error code automatically
; CPU pushes error code for: 8, 10, 11, 12, 13, 14, 17, 21, 29, 30
%macro DEFINE_ISR 1
%if (%1 = 8) | (%1 = 10) | (%1 = 11) | (%1 = 12) | (%1 = 13) | (%1 = 14) | (%1 = 17) | (%1 = 21) | (%1 = 29) | (%1 = 30)
    ISR_ERR %1
%else
    ISR_NOERR %1
%endif
%endmacro

%assign vec 0
%rep 256
    %if vec = 128
        global isr_stub_128
isr_stub_128:
        jmp isr128
    %else
        DEFINE_ISR vec
    %endif
%assign vec vec+1
%endrep

section .rodata
align 4
global interrupt_stub_table
interrupt_stub_table:
%assign vec 0
%rep 256
    %if vec = 128
        dd isr_stub_128
    %else
        dd isr_stub_%+vec
    %endif
%assign vec vec+1
%endrep
