#include "tool_runtime.h"

void _start(void) {
    char pid[32];
    if (tr_read_line("kill: pid: ", pid, sizeof(pid)) < 0) {
        printf("kill: no pid provided\n");
        exit(1);
    }
    char line[80];
    snprintf(line, sizeof(line), "kill %s", pid);
    tr_append_log("/tmp/forest-signals", line);
    printf("kill: signal recorded for pid %s\n", pid);
    exit(0);
}
