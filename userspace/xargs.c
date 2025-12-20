#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char cmd[128];
    char args[256];
    if (tr_read_line("xargs: base command: ", cmd, sizeof(cmd)) < 0) {
        printf("xargs: no command provided\n");
        return 1;
    }
    if (tr_read_line("xargs: arguments to append: ", args, sizeof(args)) < 0) {
        printf("xargs: no arguments provided\n");
        return 1;
    }
    printf("constructed: %s %s\n", cmd, args);
    return 0;
}
