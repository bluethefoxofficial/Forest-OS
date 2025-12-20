section .text
global _start
extern main
extern exit

_start:
    ; Clear direction flag for string operations
    cld
    
    ; Set up initial stack frame
    mov ebp, 0
    
    ; Simple setup - just pass argc=0, argv=NULL, envp=NULL
    push 0         ; envp = NULL
    push 0         ; argv = NULL
    push 0         ; argc = 0
    
    ; Call main function
    call main
    
    ; Exit with main's return value
    push eax
    call exit
    
    ; Should never reach here, but just in case
    hlt