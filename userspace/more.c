#include "tool_runtime.h"

void _start(void) {
    char path[128];
    if (tr_read_line("more: file (default /README.txt): ", path, sizeof(path)) < 0 || path[0] == '\0') {
        strcpy(path, "/README.txt");
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("more: unable to open %s\n", path);
        exit(1);
    }
    char buf[256];
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    close(fd);
    printf("%s\n", buf);
    exit(0);
}
