; 64-bit Boot code for Forest OS
; This code runs in 32-bit protected mode (from GRUB) and switches to 64-bit long mode

bits 32

; Multiboot headers section
section .multiboot_header
align 4

; Multiboot1 header
multiboot1_header:
    MULTIBOOT1_MAGIC equ 0x1BADB002
    MULTIBOOT1_FLAGS equ 0x00000003  ; Page alignment + memory info
    MULTIBOOT1_CHECKSUM equ -(MULTIBOOT1_MAGIC + MULTIBOOT1_FLAGS)

    dd MULTIBOOT1_MAGIC
    dd MULTIBOOT1_FLAGS
    dd MULTIBOOT1_CHECKSUM

; Multiboot2 header
align 8
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
    align 8
    dw 0
    dw 0
    dd 8
multiboot2_header_end:

section .text.boot
global start
extern startk
extern _stack_top

start:
    cli

    ; Save multiboot info (eax = magic, ebx = info pointer)
    mov edi, eax        ; multiboot magic
    mov esi, ebx        ; multiboot info pointer

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
    ; Print error character to VGA
    mov dword [0xb8000], 0x4f524f45  ; "ER"
    mov dword [0xb8004], 0x4f3a4f52  ; "R:"
    mov byte [0xb8008], al
    mov byte [0xb8009], 0x4f
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
