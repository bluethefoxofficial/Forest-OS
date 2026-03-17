; 64-bit Boot code for Forest OS
; This code runs in 32-bit protected mode (from GRUB) and switches to 64-bit long mode

bits 32

; Multiboot headers section - 64-bit optimized
section .multiboot
align 8  ; 8-byte alignment for multiboot2

; Multiboot2 header (primary for 64-bit)
multiboot2_header_start:
    MULTIBOOT2_MAGIC equ 0xE85250D6
    MULTIBOOT2_ARCH equ 0x00000000  ; x86 protected mode
    MULTIBOOT2_LENGTH equ (multiboot2_header_end - multiboot2_header_start)
    MULTIBOOT2_CHECKSUM equ -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + MULTIBOOT2_LENGTH)

    dd MULTIBOOT2_MAGIC
    dd MULTIBOOT2_ARCH
    dd MULTIBOOT2_LENGTH
    dd MULTIBOOT2_CHECKSUM

    ; Framebuffer request tag (type 5) - request current GRUB mode
    align 8
    dw 5    ; type (framebuffer)
    dw 0    ; flags
    dd 20   ; size
    dd 0    ; width (0 = use current)
    dd 0    ; height (0 = use current)
    dd 0    ; depth (0 = use current)

    ; End tag
    align 8
    dw 0    ; type
    dw 0    ; flags
    dd 8    ; size
multiboot2_header_end:

; Multiboot1 header (fallback)
align 4
multiboot1_header:
    MULTIBOOT1_MAGIC equ 0x1BADB002
    ; Memory info + page alignment + video mode request (preserve GRUB mode)
    MULTIBOOT1_FLAGS equ 0x00000007
    MULTIBOOT1_CHECKSUM equ -(MULTIBOOT1_MAGIC + MULTIBOOT1_FLAGS)

    dd MULTIBOOT1_MAGIC
    dd MULTIBOOT1_FLAGS
    dd MULTIBOOT1_CHECKSUM
    dd 0      ; mode_type (0 = graphics)
    dd 0      ; width (0 = use current)
    dd 0      ; height (0 = use current)
    dd 0      ; depth (0 = use current)

section .text.boot
global start
extern startk
extern _stack_top
extern _bss_start
extern _bss_end

start:
    cli

    ; Save multiboot info (eax = magic, ebx = info pointer)
    mov edi, eax        ; multiboot magic
    mov esi, ebx        ; multiboot info pointer

    ; Quick validation - write to VGA to confirm we're running
    mov dword [0xb8000], 0x4f4f4f42  ; "BOO" (Boot start - white on red)
    mov dword [0xb8004], 0x4f545f54  ; "T_6" (64-bit)

    ; Check if CPUID is supported
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21    ; Flip ID bit
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_cpuid

    ; Check for extended CPUID
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    ; Check for long mode support
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29   ; LM bit
    jz .no_long_mode

    ; Set up page tables for identity mapping first 4GB
    ; PML4[0] -> PDPT
    mov eax, pdpt
    or eax, 0x03        ; Present + Writable
    mov [pml4], eax

    ; PDPT[0-3] -> PD (4 entries for 4GB)
    mov eax, pd
    or eax, 0x03
    mov [pdpt], eax

    add eax, 0x1000
    mov [pdpt + 8], eax

    add eax, 0x1000
    mov [pdpt + 16], eax

    add eax, 0x1000
    mov [pdpt + 24], eax

    ; Fill page directories with 2MB pages (identity mapping)
    mov ecx, 0          ; Counter
    mov eax, 0x83       ; Present + Writable + 2MB page
.fill_pd:
    mov ebx, eax
    mov edx, ecx
    shl edx, 3          ; edx = ecx * 8
    mov [pd + edx], ebx
    mov dword [pd + edx + 4], 0

    add eax, 0x200000   ; Next 2MB
    inc ecx
    cmp ecx, 2048       ; 2048 * 2MB = 4GB
    jl .fill_pd

    ; Load PML4 address into CR3
    mov eax, pml4
    mov cr3, eax

    ; Enable PAE (Physical Address Extension)
    mov eax, cr4
    or eax, 1 << 5      ; PAE bit
    mov cr4, eax

    ; Enable long mode in EFER MSR
    mov ecx, 0xC0000080 ; EFER MSR
    rdmsr
    or eax, 1 << 8      ; LME bit (Long Mode Enable)
    wrmsr

    ; Enable paging (and thus activate long mode)
    mov eax, cr0
    or eax, 1 << 31     ; PG bit
    mov cr0, eax

    ; Load 64-bit GDT
    lgdt [gdt64.pointer]

    ; Far jump to 64-bit code
    jmp gdt64.code:long_mode_start

.no_cpuid:
    mov al, 'C'
    jmp .error

.no_long_mode:
    mov al, 'L'
    jmp .error

.error:
    ; Print error message to VGA screen (more visible)
    mov dword [0xb8000], 0x4f424f45  ; "EBO" (Boot Error)
    mov dword [0xb8004], 0x4f204f4f  ; "OT "
    mov dword [0xb8008], 0x4f414f45  ; "EA "
    mov dword [0xb800c], 0x4f524f52  ; "RR:"
    mov byte [0xb8010], al           ; Error code
    mov byte [0xb8011], 0x4f         ; Red background
    ; Fill rest of screen with pattern to make it visible
    mov ecx, 80*25*2                ; Screen size in bytes
    mov edi, 0xb8000
.fill_screen:
    mov byte [edi], ' '
    mov byte [edi+1], 0x1f          ; White on blue
    add edi, 2
    loop .fill_screen
    cli
    hlt

; 64-bit code
bits 64
section .text

long_mode_start:
    ; Reload segment registers with 64-bit null/data segments
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up 64-bit stack
    mov rsp, _stack_top
    
    ; Zero BSS section (required for proper initialization on reboot)
    mov rcx, _bss_end
    sub rcx, _bss_start
    jz .bss_done        ; Skip if BSS is empty
    mov rdi, _bss_start
    xor rax, rax        ; Clear rax
    rep stosq           ; Zero memory (8 bytes at a time)
.bss_done:
    
    ; Clear direction flag
    cld
    
    ; Zero the upper 32 bits of saved parameters
    mov eax, edi
    mov rdi, rax        ; multiboot magic in rdi (first arg)
    mov eax, esi
    mov rsi, rax        ; multiboot info in rsi (second arg)

    ; Call kernel main
    call startk

    ; Halt if kernel returns
    cli
.hang:
    hlt
    jmp .hang

; GDT for 64-bit mode
section .rodata
align 16
gdt64:
    dq 0                            ; Null descriptor
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)  ; 64-bit code segment
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)            ; 64-bit data segment
.pointer:
    dw $ - gdt64 - 1    ; GDT limit
    dq gdt64            ; GDT base

; Page tables (must be 4KB aligned)
section .bss
align 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd:
    resb 4096 * 4       ; 4 page directories for 4GB mapping
