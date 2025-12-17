bits 32

section .text

extern interrupt_common_handler
extern g_kernel_data_selector

; -----------------------------------------------------------------------------
; Common interrupt entry point. Expects stack layout:
;   [esp + 0]  -> interrupt number
;   [esp + 4]  -> error code (real or synthetic)
;   [esp + 8]  -> return EIP
;   [esp + 12] -> return CS
;   [esp + 16] -> EFLAGS
; -----------------------------------------------------------------------------
isr_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, [g_kernel_data_selector]
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov eax, [esp + 48]      ; Interrupt vector number
    mov ebx, [esp + 52]      ; Error code
    mov edx, esp
    add edx, 56              ; Pointer to hardware-pushed frame

    push ebx                 ; error_code (last argument)
    push edx                 ; struct interrupt_frame*
    push eax                 ; interrupt number
    call interrupt_common_handler
    add esp, 12

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8               ; Drop interrupt number + error code
    iretd

%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push dword 0             ; Synthetic error code
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
; Helper to determine whether vector pushes error code
%macro DEFINE_ISR 1
%if (%1 = 8) | (%1 = 10) | (%1 = 11) | (%1 = 12) | (%1 = 13) | (%1 = 14) | (%1 = 17) | (%1 = 21) | (%1 = 29) | (%1 = 30)
    ISR_ERR %1
%else
    ISR_NOERR %1
%endif
%endmacro

%assign vec 0
%rep 256
    DEFINE_ISR vec
%assign vec vec+1
%endrep

section .rodata
align 4
global interrupt_stub_table
interrupt_stub_table:
%assign vec 0
%rep 256
    dd isr_stub_%+vec
%assign vec vec+1
%endrep
