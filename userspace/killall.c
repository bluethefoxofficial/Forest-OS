#include "tool_runtime.h"

void _start(void) {
    char name[64];
    if (tr_read_line("killall: process name: ", name, sizeof(name)) < 0) {
        printf("killall: no name provided\n");
        exit(1);
    }
    char line[96];
    snprintf(line, sizeof(line), "killall %s", name);
    tr_append_log("/tmp/forest-signals", line);
    printf("killall: recorded termination request for %s\n", name);
    exit(0);
}
