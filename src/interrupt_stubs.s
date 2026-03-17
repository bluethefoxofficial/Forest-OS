; Enhanced Interrupt Service Routine Stubs for Forest OS
; Supports both x86-32 and x86-64 architectures
; Provides proper context saving and restoration

; Architecture detection based on output format (NASM does not always define
; __x86_64__ for us). This keeps symbol names consistent with the C side.
%ifidn __OUTPUT_FORMAT__,elf64
    %define ARCH_64BIT 1
    %define ARCH_32BIT 0
    bits 64
%else
    %define ARCH_64BIT 0
    %define ARCH_32BIT 1
    bits 32
%endif

; External C functions
extern interrupt_dispatch_handler
extern interrupt_common_handler
extern interrupt_stats_update_latency

; Global symbols for assembly stubs
%if ARCH_64BIT
global isr_stub_0, isr_stub_1, isr_stub_2, isr_stub_3, isr_stub_4, isr_stub_5, isr_stub_6, isr_stub_7
global isr_stub_8, isr_stub_9, isr_stub_10, isr_stub_11, isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15
global isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19, isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23
global isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27, isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31

global irq_stub_0, irq_stub_1, irq_stub_2, irq_stub_3, irq_stub_4, irq_stub_5, irq_stub_6, irq_stub_7
global irq_stub_8, irq_stub_9, irq_stub_10, irq_stub_11, irq_stub_12, irq_stub_13, irq_stub_14, irq_stub_15

global nmi_handler, double_fault_handler, machine_check_handler
global syscall_handler, spurious_irq_handler
%else
; NOTE: For 32-bit ELF, we don't use underscore prefixes.
; The main interrupt stubs are provided by interrupt_stubs.asm which defines
; interrupt_stub_table and isr_stub_N symbols. This file provides additional
; architecture-specific functionality but should not duplicate those symbols.
; The 32-bit section below is kept for reference but uses alt_ prefix to avoid conflicts.

global alt_isr_stub_0, alt_isr_stub_1, alt_isr_stub_2, alt_isr_stub_3
global alt_nmi_handler, alt_double_fault_handler
%endif

; Interrupt context structure offsets for assembly
%if ARCH_64BIT
    ; x86-64 register offsets in interrupt_context
    OFFSET_R15          equ 0
    OFFSET_R14          equ 8
    OFFSET_R13          equ 16
    OFFSET_R12          equ 24
    OFFSET_R11          equ 32
    OFFSET_R10          equ 40
    OFFSET_R9           equ 48
    OFFSET_R8           equ 56
    OFFSET_RDI          equ 64
    OFFSET_RSI          equ 72
    OFFSET_RBP          equ 80
    OFFSET_RBX          equ 88
    OFFSET_RDX          equ 96
    OFFSET_RCX          equ 104
    OFFSET_RAX          equ 112
    OFFSET_DS           equ 120
    OFFSET_ES           equ 128
    OFFSET_FS           equ 136
    OFFSET_GS           equ 144
    OFFSET_ERROR_CODE   equ 152
    OFFSET_RIP          equ 160
    OFFSET_CS           equ 168
    OFFSET_RFLAGS       equ 176
    OFFSET_RSP          equ 184
    OFFSET_SS           equ 192
    OFFSET_VECTOR       equ 200
    OFFSET_TIMESTAMP    equ 208
    CONTEXT_SIZE        equ 224
%else
    ; x86-32 register offsets in interrupt_context
    OFFSET_EDI          equ 0
    OFFSET_ESI          equ 4
    OFFSET_EBP          equ 8
    OFFSET_ESP_DUMMY    equ 12
    OFFSET_EBX          equ 16
    OFFSET_EDX          equ 20
    OFFSET_ECX          equ 24
    OFFSET_EAX          equ 28
    OFFSET_DS           equ 32
    OFFSET_ES           equ 36
    OFFSET_FS           equ 40
    OFFSET_GS           equ 44
    OFFSET_ERROR_CODE   equ 48
    OFFSET_EIP          equ 52
    OFFSET_CS           equ 56
    OFFSET_EFLAGS       equ 60
    OFFSET_ESP          equ 64
    OFFSET_SS           equ 68
    OFFSET_VECTOR       equ 72
    OFFSET_TIMESTAMP    equ 76
    CONTEXT_SIZE        equ 84
%endif

section .text

%if ARCH_64BIT
; ========================================
; x86-64 Interrupt Service Routines
; ========================================

; Macro to save all registers and create interrupt context
%macro SAVE_CONTEXT_64 0
    ; Allocate space for interrupt context on stack
    sub rsp, CONTEXT_SIZE
    
    ; Save general purpose registers
    mov [rsp + OFFSET_RAX], rax
    mov [rsp + OFFSET_RCX], rcx
    mov [rsp + OFFSET_RDX], rdx
    mov [rsp + OFFSET_RBX], rbx
    mov [rsp + OFFSET_RBP], rbp
    mov [rsp + OFFSET_RSI], rsi
    mov [rsp + OFFSET_RDI], rdi
    mov [rsp + OFFSET_R8], r8
    mov [rsp + OFFSET_R9], r9
    mov [rsp + OFFSET_R10], r10
    mov [rsp + OFFSET_R11], r11
    mov [rsp + OFFSET_R12], r12
    mov [rsp + OFFSET_R13], r13
    mov [rsp + OFFSET_R14], r14
    mov [rsp + OFFSET_R15], r15
    
    ; Save segment registers
    mov ax, ds
    mov [rsp + OFFSET_DS], ax
    mov ax, es
    mov [rsp + OFFSET_ES], ax
    mov ax, fs
    mov [rsp + OFFSET_FS], ax
    mov ax, gs
    mov [rsp + OFFSET_GS], ax
    
    ; Set kernel data segments
    mov ax, 0x10    ; Kernel data selector
    mov ds, ax
    mov es, ax
    ; Keep fs and gs for per-CPU data
    
    ; Get current timestamp using RDTSC
    rdtsc
    shl rdx, 32
    or rax, rdx
    mov [rsp + OFFSET_TIMESTAMP], rax
%endmacro

; Macro to restore all registers from interrupt context
%macro RESTORE_CONTEXT_64 0
    ; Restore segment registers
    mov ax, [rsp + OFFSET_DS]
    mov ds, ax
    mov ax, [rsp + OFFSET_ES]
    mov es, ax
    mov ax, [rsp + OFFSET_FS]
    mov fs, ax
    mov ax, [rsp + OFFSET_GS]
    mov gs, ax
    
    ; Restore general purpose registers
    mov rax, [rsp + OFFSET_RAX]
    mov rcx, [rsp + OFFSET_RCX]
    mov rdx, [rsp + OFFSET_RDX]
    mov rbx, [rsp + OFFSET_RBX]
    mov rbp, [rsp + OFFSET_RBP]
    mov rsi, [rsp + OFFSET_RSI]
    mov rdi, [rsp + OFFSET_RDI]
    mov r8,  [rsp + OFFSET_R8]
    mov r9,  [rsp + OFFSET_R9]
    mov r10, [rsp + OFFSET_R10]
    mov r11, [rsp + OFFSET_R11]
    mov r12, [rsp + OFFSET_R12]
    mov r13, [rsp + OFFSET_R13]
    mov r14, [rsp + OFFSET_R14]
    mov r15, [rsp + OFFSET_R15]
    
    ; Deallocate context space
    add rsp, CONTEXT_SIZE
%endmacro

; Macro to create ISR stub with error code (x86-64)
%macro ISR_STUB_WITH_ERROR_64 1
isr_stub_%1:
    cli
    SAVE_CONTEXT_64

    ; Store interrupt vector number
    mov qword [rsp + OFFSET_VECTOR], %1

    ; Error code was already pushed by CPU, move it to our context
    mov rax, [rsp + CONTEXT_SIZE]     ; Get error code from stack
    mov [rsp + OFFSET_ERROR_CODE], rax

    ; Save original RSP (context address) before alignment
    mov rbx, rsp

    ; Align stack to 16 bytes for ABI compliance
    and rsp, -16

    ; Call C handler with context pointer (original RSP, NOT aligned)
    mov rdi, rbx    ; First argument: interrupt context at original RSP
    call interrupt_dispatch_handler

    ; Restore original RSP before context restoration
    mov rsp, rbx

    ; Restore context and return
    RESTORE_CONTEXT_64
    add rsp, 8      ; Remove error code from stack
    iretq
%endmacro

; Macro to create ISR stub without error code (x86-64)
%macro ISR_STUB_WITHOUT_ERROR_64 1
isr_stub_%1:
    cli
    push 0          ; Push dummy error code for consistency
    SAVE_CONTEXT_64

    ; Store interrupt vector number
    mov qword [rsp + OFFSET_VECTOR], %1

    ; Error code is dummy (0)
    mov qword [rsp + OFFSET_ERROR_CODE], 0

    ; Save original RSP (context address) before alignment
    mov rbx, rsp

    ; Align stack to 16 bytes for ABI compliance
    and rsp, -16

    ; Call C handler with context pointer (original RSP, NOT aligned)
    mov rdi, rbx    ; First argument: interrupt context at original RSP
    call interrupt_dispatch_handler

    ; Restore original RSP before context restoration
    mov rsp, rbx

    ; Restore context and return
    RESTORE_CONTEXT_64
    add rsp, 8      ; Remove dummy error code from stack
    iretq
%endmacro

; Macro to create IRQ stub (x86-64)
%macro IRQ_STUB_64 1
irq_stub_%1:
    cli
    push 0          ; IRQs don't have error codes
    SAVE_CONTEXT_64

    ; Store IRQ vector number (IRQ number + 32)
    mov qword [rsp + OFFSET_VECTOR], (%1 + 32)
    mov qword [rsp + OFFSET_ERROR_CODE], 0

    ; Save original RSP (context address) before alignment
    mov rbx, rsp

    ; Align stack to 16 bytes for ABI compliance
    and rsp, -16

    ; Call C handler with context pointer (original RSP, NOT aligned)
    mov rdi, rbx    ; First argument: interrupt context at original RSP
    call interrupt_dispatch_handler

    ; Restore original RSP before context restoration
    mov rsp, rbx

    ; Restore context and return
    RESTORE_CONTEXT_64
    add rsp, 8      ; Remove dummy error code
    iretq
%endmacro

; Generate ISR stubs (x86-64)
ISR_STUB_WITHOUT_ERROR_64 0    ; Division Error
ISR_STUB_WITHOUT_ERROR_64 1    ; Debug
ISR_STUB_WITHOUT_ERROR_64 2    ; NMI
ISR_STUB_WITHOUT_ERROR_64 3    ; Breakpoint
ISR_STUB_WITHOUT_ERROR_64 4    ; Overflow
ISR_STUB_WITHOUT_ERROR_64 5    ; Bound Range Exceeded
ISR_STUB_WITHOUT_ERROR_64 6    ; Invalid Opcode
ISR_STUB_WITHOUT_ERROR_64 7    ; Device Not Available
ISR_STUB_WITH_ERROR_64    8    ; Double Fault
ISR_STUB_WITHOUT_ERROR_64 9    ; Coprocessor Segment Overrun
ISR_STUB_WITH_ERROR_64    10   ; Invalid TSS
ISR_STUB_WITH_ERROR_64    11   ; Segment Not Present
ISR_STUB_WITH_ERROR_64    12   ; Stack-Segment Fault
ISR_STUB_WITH_ERROR_64    13   ; General Protection Fault
ISR_STUB_WITH_ERROR_64    14   ; Page Fault
ISR_STUB_WITHOUT_ERROR_64 15   ; Reserved
ISR_STUB_WITHOUT_ERROR_64 16   ; x87 Floating-Point Exception
ISR_STUB_WITH_ERROR_64    17   ; Alignment Check
ISR_STUB_WITHOUT_ERROR_64 18   ; Machine Check
ISR_STUB_WITHOUT_ERROR_64 19   ; SIMD Floating-Point Exception
ISR_STUB_WITHOUT_ERROR_64 20   ; Virtualization Exception
ISR_STUB_WITH_ERROR_64    21   ; Control Protection Exception
ISR_STUB_WITHOUT_ERROR_64 22   ; Reserved
ISR_STUB_WITHOUT_ERROR_64 23   ; Reserved
ISR_STUB_WITHOUT_ERROR_64 24   ; Reserved
ISR_STUB_WITHOUT_ERROR_64 25   ; Reserved
ISR_STUB_WITHOUT_ERROR_64 26   ; Reserved
ISR_STUB_WITHOUT_ERROR_64 27   ; Reserved
ISR_STUB_WITHOUT_ERROR_64 28   ; Hypervisor Injection Exception
ISR_STUB_WITH_ERROR_64    29   ; VMM Communication Exception
ISR_STUB_WITH_ERROR_64    30   ; Security Exception
ISR_STUB_WITHOUT_ERROR_64 31   ; Reserved

; Generate IRQ stubs (x86-64)
IRQ_STUB_64 0    ; Timer
IRQ_STUB_64 1    ; Keyboard
IRQ_STUB_64 2    ; Cascade
IRQ_STUB_64 3    ; COM2
IRQ_STUB_64 4    ; COM1
IRQ_STUB_64 5    ; LPT2
IRQ_STUB_64 6    ; Floppy
IRQ_STUB_64 7    ; LPT1
IRQ_STUB_64 8    ; RTC
IRQ_STUB_64 9    ; Free
IRQ_STUB_64 10   ; Free
IRQ_STUB_64 11   ; Free
IRQ_STUB_64 12   ; Mouse
IRQ_STUB_64 13   ; FPU
IRQ_STUB_64 14   ; Primary IDE
IRQ_STUB_64 15   ; Secondary IDE

; Generic ISR stub for vectors without dedicated handlers (x86-64)
%macro GENERIC_ISR_STUB_64 1
global isr_stub_%1
isr_stub_%1:
    cli
    push 0
    SAVE_CONTEXT_64
    mov qword [rsp + OFFSET_VECTOR], %1
    mov qword [rsp + OFFSET_ERROR_CODE], 0
    mov rbx, rsp
    and rsp, -16
    mov rdi, rbx
    call interrupt_dispatch_handler
    mov rsp, rbx
    RESTORE_CONTEXT_64
    add rsp, 8
    iretq
%endmacro

; Special handlers for x86-64
nmi_handler:
    cli
    push 0
    SAVE_CONTEXT_64
    mov qword [rsp + OFFSET_VECTOR], 2
    mov qword [rsp + OFFSET_ERROR_CODE], 0
    mov rbx, rsp
    and rsp, -16
    mov rdi, rbx
    call interrupt_dispatch_handler
    mov rsp, rbx
    RESTORE_CONTEXT_64
    add rsp, 8
    iretq

double_fault_handler:
    cli
    SAVE_CONTEXT_64
    mov qword [rsp + OFFSET_VECTOR], 8
    mov rax, [rsp + CONTEXT_SIZE]
    mov [rsp + OFFSET_ERROR_CODE], rax
    mov rbx, rsp
    and rsp, -16
    mov rdi, rbx
    call interrupt_dispatch_handler
    ; Double fault usually doesn't return
    cli
    hlt

machine_check_handler:
    cli
    push 0
    SAVE_CONTEXT_64
    mov qword [rsp + OFFSET_VECTOR], 18
    mov qword [rsp + OFFSET_ERROR_CODE], 0
    mov rbx, rsp
    and rsp, -16
    mov rdi, rbx
    call interrupt_dispatch_handler
    mov rsp, rbx
    RESTORE_CONTEXT_64
    add rsp, 8
    iretq

syscall_handler:
    cli
    push 0
    SAVE_CONTEXT_64
    mov qword [rsp + OFFSET_VECTOR], 0x80
    mov qword [rsp + OFFSET_ERROR_CODE], 0
    mov rbx, rsp
    and rsp, -16
    mov rdi, rbx
    call interrupt_dispatch_handler
    mov rsp, rbx
    RESTORE_CONTEXT_64
    add rsp, 8
    iretq

spurious_irq_handler:
    cli
    push 0
    SAVE_CONTEXT_64
    mov qword [rsp + OFFSET_VECTOR], 0xFF
    mov qword [rsp + OFFSET_ERROR_CODE], 0
    mov rbx, rsp
    and rsp, -16
    mov rdi, rbx
    call interrupt_dispatch_handler
    mov rsp, rbx
    RESTORE_CONTEXT_64
    add rsp, 8
    iretq

; Generate generic stubs for vectors 48..255 except syscall/spurious
%assign vec 48
%rep (256-48)
    %if vec = 128
        ; Syscall vector handled by syscall_handler
    %elif vec = 255
        ; Spurious vector handled by spurious_irq_handler
    %else
        GENERIC_ISR_STUB_64 vec
    %endif
%assign vec vec+1
%endrep

section .rodata
align 8
global interrupt_stub_table
interrupt_stub_table:
%assign vec 0
%rep 256
    %if vec = 128
        dq syscall_handler
    %elif vec = 255
        dq spurious_irq_handler
    %elif vec < 32
        dq isr_stub_%+vec
    %elif vec < 48
        %assign irq_index (vec-32)
        dq irq_stub_%+irq_index
    %else
        dq isr_stub_%+vec
    %endif
%assign vec vec+1
%endrep

%else ; ARCH_32BIT
; ========================================
; x86-32 Interrupt Service Routines
; ========================================
; NOTE: For 32-bit builds, the main interrupt stubs are provided by
; interrupt_stubs.asm which defines interrupt_stub_table and isr_stub_N
; with correct ELF naming (no underscore prefixes). That file also
; correctly calls interrupt_common_handler.
;
; This section is intentionally left minimal to avoid symbol conflicts.
; The interrupt_stubs.asm file handles all 256 interrupt vectors for 32-bit.
; ========================================

%endif ; ARCH_32BIT

; ========================================
; Common utility functions
; ========================================

; Fast interrupt enable/disable functions
global interrupt_enable, interrupt_disable
global interrupt_save_and_disable, interrupt_restore

interrupt_enable:
    sti
    ret

interrupt_disable:
    cli
    ret

%if ARCH_64BIT
interrupt_save_and_disable:
    pushfq
    pop rax
    cli
    ret

interrupt_restore:
    push rdi    ; flags are passed in rdi
    popfq
    ret
%else
interrupt_save_and_disable:
    pushfd
    pop eax
    cli
    ret

interrupt_restore:
    push dword [esp + 4]   ; flags are passed as parameter
    popfd
    ret
%endif

section .data
; Interrupt statistics counters
global interrupt_entry_count
global interrupt_total_time

interrupt_entry_count: dq 0
interrupt_total_time: dq 0
