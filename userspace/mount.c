#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char src[128];
    char dest[128];
    if (tr_read_line("mount: device: ", src, sizeof(src)) < 0) {
        printf("mount: no source provided\n");
        return 1;
    }
    if (tr_read_line("mount: mountpoint: ", dest, sizeof(dest)) < 0) {
        printf("mount: no mountpoint provided\n");
        return 1;
    }
    char line[260];
    snprintf(line, sizeof(line), "mount %s %s", src, dest);
    tr_append_log("/tmp/forest-mounts", line);
    printf("mount: recorded %s on %s\n", src, dest);
    return 0;
}
