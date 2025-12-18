#include "tool_runtime.h"

void _start(void) {
    char line[256];
    printf("forest sh: type a command (echo only)\n> ");
    if (tr_read_line(NULL, line, sizeof(line)) < 0) {
        printf("sh: no input\n");
        exit(1);
    }
    printf("you entered: %s\n", line);
    exit(0);
}
