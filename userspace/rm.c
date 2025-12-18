#include "tool_runtime.h"

void _start(void) {
    char path[128];
    if (tr_read_line("rm: file to remove: ", path, sizeof(path)) < 0) {
        printf("rm: no file provided\n");
        exit(1);
    }
    if (unlink(path) == 0) {
        printf("rm: removed %s\n", path);
        exit(0);
    }
    printf("rm: unable to remove %s\n", path);
    exit(1);
}
