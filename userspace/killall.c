#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char name[64];
    if (tr_read_line("killall: process name: ", name, sizeof(name)) < 0) {
        printf("killall: no name provided\n");
        return 1;
    }
    char line[96];
    snprintf(line, sizeof(line), "killall %s", name);
    tr_append_log("/tmp/forest-signals", line);
    printf("killall: recorded termination request for %s\n", name);
    return 0;
}
