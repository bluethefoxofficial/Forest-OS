#include "tool_runtime.h"

void _start(void) {
    char pattern[64];
    char line[256];
    if (tr_read_line("awk: pattern? ", pattern, sizeof(pattern)) < 0) {
        printf("awk: no pattern provided\n");
        exit(1);
    }
    if (tr_read_line("awk: input line? ", line, sizeof(line)) < 0) {
        printf("awk: no input provided\n");
        exit(1);
    }
    if (strstr(line, pattern)) {
        printf("%s\n", line);
    } else {
        printf("awk: no match for '%s'\n", pattern);
    }
    exit(0);
}
