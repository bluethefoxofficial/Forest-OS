#include "tool_runtime.h"

void _start(void) {
    char src[128];
    char dest[128];
    if (tr_read_line("mv: source: ", src, sizeof(src)) < 0) {
        printf("mv: no source provided\n");
        exit(1);
    }
    if (tr_read_line("mv: destination: ", dest, sizeof(dest)) < 0) {
        printf("mv: no destination provided\n");
        exit(1);
    }
    if (tr_copy_file(src, dest) == 0) {
        unlink(src);
        printf("mv: moved %s -> %s\n", src, dest);
        exit(0);
    }
    exit(1);
}
