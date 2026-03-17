bits 32

; Multiboot headers section - support both multiboot1 and multiboot2
section .multiboot
align 8

; Multiboot2 header (preferred)
multiboot2_header_start:
    MULTIBOOT2_MAGIC equ 0xE85250D6
    MULTIBOOT2_ARCH equ 0x00000000
    MULTIBOOT2_LENGTH equ (multiboot2_header_end - multiboot2_header_start)
    MULTIBOOT2_CHECKSUM equ -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + MULTIBOOT2_LENGTH)

    dd MULTIBOOT2_MAGIC
    dd MULTIBOOT2_ARCH
    dd MULTIBOOT2_LENGTH
    dd MULTIBOOT2_CHECKSUM

    ; Framebuffer request tag (type 5) - request current GRUB mode
    align 8
    dw 5        ; type: framebuffer
    dw 0        ; flags
    dd 20       ; size
    dd 0        ; width (0 = use current)
    dd 0        ; height (0 = use current)
    dd 0        ; bpp (0 = use current)

    ; End tag
    align 8
    dw 0    ; type
    dw 0    ; flags  
    dd 8    ; size
multiboot2_header_end:

; Multiboot1 header (legacy fallback)
align 4
multiboot1_header:
    MULTIBOOT1_MAGIC equ 0x1BADB002
    ; Memory info + page alignment + video mode request (preserve GRUB mode)
    MULTIBOOT1_FLAGS equ 0x00000007
    MULTIBOOT1_CHECKSUM equ -(MULTIBOOT1_MAGIC + MULTIBOOT1_FLAGS)

    dd MULTIBOOT1_MAGIC
    dd MULTIBOOT1_FLAGS
    dd MULTIBOOT1_CHECKSUM
    dd 0        ; mode_type (0 = graphics)
    dd 0        ; width (0 = use current)
    dd 0        ; height (0 = use current)
    dd 0        ; depth (0 = use current)

section .text

global start
extern startk
extern _stack_top
extern _bss_start
extern _bss_end
extern kernel_panic_with_stack

section .data
align 4
boot_params:
saved_magic:
    resd 1
saved_mbi:
    resd 1

section .text

start:
    cli                 ; Disable interrupts
    
    ; Save multiboot parameters immediately to fixed memory locations
    mov [saved_magic], eax  ; Save multiboot magic to memory
    mov [saved_mbi], ebx    ; Save multiboot info address to memory
    mov edi, eax            ; Also keep in EDI for potential debugging
    mov esi, ebx            ; Also keep in ESI
    
    ; Validate stack pointer is in reasonable range
    mov [boot_loader_stack], esp
    mov esp, _stack_top ; Initialize stack pointer
    cmp esp, 0x100000   ; Ensure stack is above 1MB
    jl stack_panic
    
    ; Zero BSS section (required for proper initialization on reboot)
    mov ecx, _bss_end
    sub ecx, _bss_start
    jz .bss_done        ; Skip if BSS is empty
    mov edi, _bss_start
    xor eax, eax        ; Clear eax
    rep stosb           ; Zero memory
.bss_done:
    
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
    
    ; Pass saved multiboot parameters to kernel (read from memory to be safe)
    mov eax, [saved_magic]   ; Reload magic from memory
    mov ebx, [saved_mbi]     ; Reload MBI from memory
    push ebx            ; multiboot info address
    push eax            ; multiboot magic
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

align 4096
panic_safe_stack:
    resb 4096
panic_safe_stack_top:

align 16
boot_fault_snapshot:
    resd 8

boot_loader_stack:
    resd 1

; Boot snapshot placed in .data to avoid corruption by panic stack
section .data
align 4
boot_stack_snapshot:
    resd 8

section .data
align 4
early_idt_descriptor:
    dw 8*256 - 1        ; IDT limit
    dd early_idt        ; IDT base address

stack_panic_msg db "Boot stack validation failed",0
early_fault_msg db "Fatal exception before kernel init",0
