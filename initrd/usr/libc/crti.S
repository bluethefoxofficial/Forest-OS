.section .init
.global _init
.type _init, @function
_init:
    pushl %ebp
    movl %esp, %ebp
    # GCC will put constructor prologue here

.section .fini
.global _fini
.type _fini, @function
_fini:
    pushl %ebp
    movl %esp, %ebp
    # GCC will put destructor prologue here