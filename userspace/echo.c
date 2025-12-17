#include "../src/include/libc/stdio.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"

void _start(void) {
    char buffer[128];
    printf("echo: type a line and press enter\n> ");
    ssize_t read_bytes = read(0, buffer, sizeof(buffer) - 1);
    if (read_bytes > 0) {
        buffer[read_bytes] = '\0';
        printf("you said: %s\n", buffer);
    } else {
        printf("echo: no input available\n");
    }
    exit(0);
}
