#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char path[128];
    if (tr_read_line("df: file to inspect (default /README.md): ", path, sizeof(path)) < 0 || path[0] == '\0') {
        strcpy(path, "/README.md");
    }
    long bytes = tr_count_file_bytes(path);
    if (bytes < 0) {
        printf("df: unable to read %s\n", path);
        return 1;
    }
    printf("Filesystem stats for %s: %ld bytes used\n", path, bytes);
    return 0;
}
