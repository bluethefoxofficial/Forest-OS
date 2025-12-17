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
    mov [eax], esp          ; save pointer to current stack frame

    mov eax, [ebp + 12]     ; new_esp_val
    mov esp, eax

    mov eax, [ebp + 16]     ; new_page_directory_phys
    mov cr3, eax

    popa
    popf

    pop ebp
    ret
