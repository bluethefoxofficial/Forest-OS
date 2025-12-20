#include "tool_runtime.h"

extern char **environ;

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    if (!environ) {
        printf("env: environment unavailable\n");
        return 1;
    }
    for (char **e = environ; *e; ++e) {
        printf("%s\n", *e);
    }
    return 0;
}
