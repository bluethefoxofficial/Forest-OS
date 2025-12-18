#include "tool_runtime.h"

void _start(void) {
    char src[128];
    char dest[128];
    if (tr_read_line("xz: input file: ", src, sizeof(src)) < 0) {
        printf("xz: no source provided\n");
        exit(1);
    }
    if (tr_read_line("xz: output file: ", dest, sizeof(dest)) < 0) {
        printf("xz: no destination provided\n");
        exit(1);
    }
    int status = tr_copy_file(src, dest);
    if (status == 0) {
        printf("xz: stored uncompressed copy at %s\n", dest);
    }
    exit(status);
}
