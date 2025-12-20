#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("Hello from Forest OS userland!\n");
    printf("Try running uname.elf or catreadme.elf for more demos.\n");
    return 0;
}
