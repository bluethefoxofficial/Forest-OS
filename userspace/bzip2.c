#include "tool_runtime.h"

void _start(void) {
    char src[128];
    char dest[128];
    if (tr_read_line("bzip2: input file: ", src, sizeof(src)) < 0) {
        printf("bzip2: no source provided\n");
        exit(1);
    }
    if (tr_read_line("bzip2: output file: ", dest, sizeof(dest)) < 0) {
        printf("bzip2: no destination provided\n");
        exit(1);
    }
    int status = tr_copy_file(src, dest);
    if (status == 0) {
        printf("bzip2: stored uncompressed copy at %s\n", dest);
    }
    exit(status);
}
