#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char path[128];
    char owner[64];
    if (tr_read_line("chown: target path: ", path, sizeof(path)) < 0) {
        printf("chown: no target provided\n");
        return 1;
    }
    if (tr_read_line("chown: new owner: ", owner, sizeof(owner)) < 0) {
        printf("chown: no owner provided\n");
        return 1;
    }
    char line[200];
    snprintf(line, sizeof(line), "chown %s %s", owner, path);
    tr_append_log("/tmp/forest-permissions", line);
    printf("chown: recorded %s for %s\n", owner, path);
    return 0;
}
