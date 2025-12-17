#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"

void _start(void) {
    printf("Hello from Forest OS userland!\n");
    printf("Try running uname.elf or catreadme.elf for more demos.\n");
    exit(0);
}
