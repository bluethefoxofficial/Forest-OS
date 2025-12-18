#include "tool_runtime.h"

void _start(void) {
    char path[128];
    if (tr_read_line("du: file to inspect (default /README.txt): ", path, sizeof(path)) < 0 || path[0] == '\0') {
        strcpy(path, "/README.txt");
    }
    long bytes = tr_count_file_bytes(path);
    if (bytes < 0) {
        printf("du: unable to read %s\n", path);
        exit(1);
    }
    printf("%ld\t%s\n", bytes, path);
    exit(0);
}
