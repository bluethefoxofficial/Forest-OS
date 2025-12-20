#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char target[128];
    if (tr_read_line("umount: target: ", target, sizeof(target)) < 0) {
        printf("umount: no target provided\n");
        return 1;
    }
    char line[160];
    snprintf(line, sizeof(line), "umount %s", target);
    tr_append_log("/tmp/forest-mounts", line);
    printf("umount: recorded unmount for %s\n", target);
    return 0;
}
