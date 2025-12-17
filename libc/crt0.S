.section .text
.global _start
.type _start, @function

_start:
    # Clear direction flag for string operations
    cld
    
    # Set up initial stack frame
    movl $0, %ebp
    
    # Get argc from stack
    movl 4(%esp), %edi      # argc
    
    # Get argv from stack  
    leal 8(%esp), %esi      # argv = esp + 8
    
    # Calculate envp = argv + (argc + 1) * 4
    movl %edi, %eax         # eax = argc
    incl %eax               # eax = argc + 1
    shll $2, %eax           # eax = (argc + 1) * 4
    addl %esi, %eax         # eax = argv + (argc + 1) * 4
    movl %eax, %edx         # edx = envp
    
    # Call constructors
    call __libc_init_array
    
    # Set up arguments for main
    pushl %edx              # envp
    pushl %esi              # argv
    pushl %edi              # argc
    
    # Call main function
    call main
    
    # Save main's return value
    movl %eax, %edi
    
    # Call destructors
    call __libc_fini_array
    
    # Exit with main's return value
    pushl %edi
    call exit
    
    # Should never reach here, but just in case
    hlt

.size _start, .-_start

# Weak symbols for constructor/destructor support
.weak __libc_init_array
.weak __libc_fini_array

__libc_init_array:
__libc_fini_array:
    ret