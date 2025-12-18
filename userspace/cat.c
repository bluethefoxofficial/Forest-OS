#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"

static void print_default_file(void) {
    int fd = open("/README.txt", 0);
    if (fd < 0) {
        printf("cat: unable to open default /README.txt\n");
        return;
    }

    char buffer[256];
    while (1) {
        int n = read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        write(1, buffer, (size_t)n);
    }
    close(fd);
}

void _start(void) {
    printf("cat: argument parsing not yet wired, defaulting to /README.txt\n");
    print_default_file();
    exit(0);
}
