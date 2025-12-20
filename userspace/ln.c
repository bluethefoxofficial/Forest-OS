#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char src[128];
    char dest[128];
    if (tr_read_line("ln: existing file: ", src, sizeof(src)) < 0) {
        printf("ln: no source provided\n");
        return 1;
    }
    if (tr_read_line("ln: link name: ", dest, sizeof(dest)) < 0) {
        printf("ln: no link name provided\n");
        return 1;
    }
    int status = tr_copy_file(src, dest);
    if (status == 0) {
        printf("ln: created copy placeholder at %s\n", dest);
    }
    return status;
}
