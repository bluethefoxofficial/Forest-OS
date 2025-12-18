#include "tool_runtime.h"

void _start(void) {
    char path[128];
    char mode[16];
    if (tr_read_line("chmod: target path: ", path, sizeof(path)) < 0) {
        printf("chmod: no target provided\n");
        exit(1);
    }
    if (tr_read_line("chmod: mode (e.g. 644): ", mode, sizeof(mode)) < 0) {
        printf("chmod: no mode provided\n");
        exit(1);
    }
    char line[180];
    snprintf(line, sizeof(line), "chmod %s %s", mode, path);
    tr_append_log("/tmp/forest-permissions", line);
    printf("chmod: recorded %s for %s\n", mode, path);
    exit(0);
}
