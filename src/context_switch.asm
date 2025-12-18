; src/context_switch.asm
; Provides the low-level assembly for context switching.

bits 32

; void task_switch_asm(uint32* old_esp_ptr, uint32 new_esp_val, uint32 new_page_directory_phys)
; old_esp_ptr: Pointer to where the current ESP should be saved (e.g., &prev_task->kernel_stack)
; new_esp_val: The ESP value to load for the next task (e.g., next_task->kernel_stack)
; new_page_directory_phys: Physical address of the next task's page directory
global task_switch_asm
task_switch_asm:
    push ebp
    mov ebp, esp

    pushf
    pusha

    mov eax, [ebp + 8]      ; old_esp_ptr
    cmp eax, 0              ; Check if old_esp_ptr is NULL (first task switch)
    je skip_save
    mov [eax], esp          ; save pointer to current stack frame

skip_save:
    mov eax, [ebp + 12]     ; new_esp_val
    mov esp, eax

    mov eax, [ebp + 16]     ; new_page_directory_phys
    mov cr3, eax

    popa
    popf

    pop ebp
    ret

; void enter_usermode_asm(uint32 entry_point, uint32 user_stack, uint32 user_cs, uint32 user_ds)
; Direct entry into user mode for initial task start
global enter_usermode_asm
enter_usermode_asm:
    push ebp
    mov ebp, esp
    
    mov eax, [ebp + 16]     ; user_ds
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Build the iret frame on the stack
    push eax                ; SS (user data segment)
    push dword [ebp + 8]    ; ESP (user stack)
    pushf                   ; EFLAGS
    pop eax
    or eax, 0x200           ; Set IF (interrupt enable flag)
    push eax
    push dword [ebp + 12]   ; CS (user code segment)
    push dword [ebp + 4]    ; EIP (entry point)
    
    iret
