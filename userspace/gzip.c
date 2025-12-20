#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char src[128];
    char dest[128];
    if (tr_read_line("gzip: input file: ", src, sizeof(src)) < 0) {
        printf("gzip: no source provided\n");
        return 1;
    }
    if (tr_read_line("gzip: output file: ", dest, sizeof(dest)) < 0) {
        printf("gzip: no destination provided\n");
        return 1;
    }
    int status = tr_copy_file(src, dest);
    if (status == 0) {
        printf("gzip: stored uncompressed copy at %s\n", dest);
    }
    return status;
}
