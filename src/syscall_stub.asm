bits 32

extern syscall_handle
extern g_kernel_data_selector

global isr128

isr128:
    push ds
    push es
    push fs
    push gs

    mov ax, [g_kernel_data_selector]
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    pushad
    mov eax, esp
    push eax
    call syscall_handle
    add esp, 4
    popad

    pop gs
    pop fs
    pop es
    pop ds
    iretd
