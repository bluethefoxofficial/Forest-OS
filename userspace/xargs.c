#include "tool_runtime.h"

void _start(void) {
    char cmd[128];
    char args[256];
    if (tr_read_line("xargs: base command: ", cmd, sizeof(cmd)) < 0) {
        printf("xargs: no command provided\n");
        exit(1);
    }
    if (tr_read_line("xargs: arguments to append: ", args, sizeof(args)) < 0) {
        printf("xargs: no arguments provided\n");
        exit(1);
    }
    printf("constructed: %s %s\n", cmd, args);
    exit(0);
}
