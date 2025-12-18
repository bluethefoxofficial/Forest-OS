#include "tool_runtime.h"

void _start(void) {
    char items[256];
    if (tr_read_line("tar: space-separated entries to archive: ", items, sizeof(items)) < 0) {
        printf("tar: no entries provided\n");
        exit(1);
    }
    tr_append_log("/tmp/forest-archive", items);
    printf("tar: recorded archive manifest to /tmp/forest-archive\n");
    exit(0);
}
