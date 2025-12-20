#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char path[128];
    if (tr_read_line("du: file to inspect (default /README.txt): ", path, sizeof(path)) < 0 || path[0] == '\0') {
        strcpy(path, "/README.txt");
    }
    long bytes = tr_count_file_bytes(path);
    if (bytes < 0) {
        printf("du: unable to read %s\n", path);
        return 1;
    }
    printf("%ld\t%s\n", bytes, path);
    return 0;
}
