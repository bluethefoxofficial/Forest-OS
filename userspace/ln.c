#include "tool_runtime.h"

void _start(void) {
    char src[128];
    char dest[128];
    if (tr_read_line("ln: existing file: ", src, sizeof(src)) < 0) {
        printf("ln: no source provided\n");
        exit(1);
    }
    if (tr_read_line("ln: link name: ", dest, sizeof(dest)) < 0) {
        printf("ln: no link name provided\n");
        exit(1);
    }
    int status = tr_copy_file(src, dest);
    if (status == 0) {
        printf("ln: created copy placeholder at %s\n", dest);
    }
    exit(status);
}
