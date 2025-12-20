#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char line[256];
    printf("forest sh: type a command (echo only)\n> ");
    if (tr_read_line(NULL, line, sizeof(line)) < 0) {
        printf("sh: no input\n");
        return 1;
    }
    printf("you entered: %s\n", line);
    return 0;
}
