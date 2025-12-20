bits 32

global cpu_read_eflags
cpu_read_eflags:
    pushfd
    pop eax
    ret

global cpu_disable_interrupts
cpu_disable_interrupts:
    cli
    ret

global cpu_enable_interrupts
cpu_enable_interrupts:
    sti
    ret

global cpu_load_idt
cpu_load_idt:
    mov eax, [esp + 4]
    lidt [eax]
    ret

global cpu_read_cs
cpu_read_cs:
    push cs
    pop eax
    ret

global cpu_read_ds
cpu_read_ds:
    mov ax, ds
    movzx eax, ax
    ret

global cpu_read_cr2
cpu_read_cr2:
    mov eax, cr2
    ret
