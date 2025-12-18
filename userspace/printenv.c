#include "tool_runtime.h"

extern char **environ;

void _start(void) {
    if (!environ) {
        printf("printenv: environment unavailable\n");
        exit(1);
    }
    for (char **e = environ; *e; ++e) {
        printf("%s\n", *e);
    }
    exit(0);
}
