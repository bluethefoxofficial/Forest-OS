#include "../src/include/libc/stdio.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"

static void cat_file(const char* path) {
    int fd = open(path, 0);
    if (fd < 0) {
        printf("catreadme: unable to open %s\n", path);
        return;
    }

    char buffer[256];
    ssize_t n;
    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        write(1, buffer, (size_t)n);
    }
    close(fd);
}

void _start(void) {
    printf("=== /README.txt ===\n");
    cat_file("/README.txt");
    exit(0);
}
