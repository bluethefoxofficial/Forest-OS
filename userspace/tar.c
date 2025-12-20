#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char items[256];
    if (tr_read_line("tar: space-separated entries to archive: ", items, sizeof(items)) < 0) {
        printf("tar: no entries provided\n");
        return 1;
    }
    tr_append_log("/tmp/forest-archive", items);
    printf("tar: recorded archive manifest to /tmp/forest-archive\n");
    return 0;
}
