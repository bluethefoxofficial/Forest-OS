.section .init
    # GCC will put constructor epilogue before this
    leave
    ret

.section .fini
    # GCC will put destructor epilogue before this
    leave
    ret