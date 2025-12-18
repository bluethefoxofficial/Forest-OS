#include "tool_runtime.h"

void _start(void) {
    const char *entries[] = {".", "..", "README.md", "README.txt", "tmp", "initrd", 0};
    printf("listing known entries:\n");
    for (int i = 0; entries[i]; i++) {
        printf("%s\n", entries[i]);
    }
    exit(0);
}
