bits 32

; Multiboot headers section - support both multiboot1 and multiboot2
section .multiboot_header
align 4

; Multiboot1 header (legacy)
multiboot1_header:
    MULTIBOOT1_MAGIC equ 0x2BADB002
    MULTIBOOT1_FLAGS equ 0x00000003
    MULTIBOOT1_CHECKSUM equ -(MULTIBOOT1_MAGIC + MULTIBOOT1_FLAGS)
    
    dd MULTIBOOT1_MAGIC
    dd MULTIBOOT1_FLAGS  
    dd MULTIBOOT1_CHECKSUM

; Align to 8-byte boundary for multiboot2
align 8

; Multiboot2 header
multiboot2_header_start:
    MULTIBOOT2_MAGIC equ 0xE85250D6
    MULTIBOOT2_ARCH equ 0x00000000
    MULTIBOOT2_LENGTH equ (multiboot2_header_end - multiboot2_header_start)
    MULTIBOOT2_CHECKSUM equ -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + MULTIBOOT2_LENGTH)

    dd MULTIBOOT2_MAGIC
    dd MULTIBOOT2_ARCH
    dd MULTIBOOT2_LENGTH
    dd MULTIBOOT2_CHECKSUM
    
    ; End tag
    dw 0    ; type
    dw 0    ; flags  
    dd 8    ; size
multiboot2_header_end:

section .text

global start
extern startk
extern _stack_top
extern kernel_panic_with_stack

start:
    cli                 ; Disable interrupts
    
    ; Save multiboot parameters immediately (eax = magic, ebx = info)
    mov edi, eax        ; Save multiboot magic in edi
    mov esi, ebx        ; Save multiboot info in esi
    
    ; Validate stack pointer is in reasonable range
    mov [boot_loader_stack], esp
    mov esp, _stack_top ; Initialize stack pointer
    cmp esp, 0x100000   ; Ensure stack is above 1MB
    jl stack_panic
    cmp esp, 0x200000   ; Ensure stack is below 2MB (reasonable for early boot)
    jg stack_panic
    
    ; Set up basic exception handling for early boot
    ; Install a temporary page fault handler
    mov eax, early_exception_handler
    mov [0x1040], eax   ; IDT entry 14 (page fault) offset low
    mov word [0x1044], 0x08  ; Code segment selector
    mov byte [0x1045], 0     ; Reserved
    mov byte [0x1046], 0x8E  ; Present, DPL=0, 32-bit interrupt gate
    mov word [0x1047], 0     ; IDT entry 14 offset high
    
    ; Load minimal IDT for early boot protection
    mov eax, early_idt_descriptor
    lidt [eax]
    
    ; Enable basic exception handling
    sti
    
    ; Pass saved multiboot parameters to kernel
    push esi            ; multiboot info address
    push edi            ; multiboot magic
    call startk
    
    ; If kernel returns, halt safely
    cli
    hlt

stack_panic:
    cli
    mov eax, esp                ; record failing stack pointer
    lea edx, [boot_stack_snapshot]
    mov [edx], eax
    mov ebx, [boot_loader_stack]
    mov [edx+4], ebx            ; original loader stack
    mov dword [edx+8], 0x00100000   ; lower bound
    mov dword [edx+12], 0x00200000  ; upper bound
    mov ecx, eax
    sub ecx, 0x00100000
    mov [edx+16], ecx           ; offset from lower bound
    
    mov esp, panic_safe_stack_top
    push dword 5
    push edx
    push dword stack_panic_msg
    call kernel_panic_with_stack
.hang_stack:
    hlt
    jmp .hang_stack

; Early exception handler for boot-time crashes
early_exception_handler:
    cli                 ; Disable interrupts
    mov eax, cr2
    lea edi, [boot_fault_snapshot]
    mov [edi], eax              ; Faulting address
    mov ebx, esp
    mov eax, [ebx]              ; Error code
    mov [edi+4], eax
    mov eax, [ebx+4]            ; EIP
    mov [edi+8], eax
    mov eax, [ebx+8]            ; CS
    mov [edi+12], eax
    mov eax, [ebx+12]           ; EFLAGS
    mov [edi+16], eax
    mov [edi+20], ebx           ; Stack pointer at fault
    
    mov esp, panic_safe_stack_top
    push dword 6
    push edi
    push dword early_fault_msg
    call kernel_panic_with_stack
.hang:
    hlt
    jmp .hang

; Minimal IDT for early boot
section .bss
align 16
early_idt:
    resb 8 * 256        ; 256 IDT entries, 8 bytes each

align 16
panic_safe_stack:
    resb 4096
panic_safe_stack_top:

align 16
boot_stack_snapshot:
    resd 8

align 16
boot_fault_snapshot:
    resd 8

boot_loader_stack:
    resd 1

section .data
align 4
early_idt_descriptor:
    dw 8*256 - 1        ; IDT limit
    dd early_idt        ; IDT base address

stack_panic_msg db "Boot stack validation failed",0
early_fault_msg db "Fatal exception before kernel init",0
