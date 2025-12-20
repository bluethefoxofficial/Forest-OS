#include "tool_runtime.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char pattern[64];
    char line[256];
    if (tr_read_line("awk: pattern? ", pattern, sizeof(pattern)) < 0) {
        printf("awk: no pattern provided\n");
        return 1;
    }
    if (tr_read_line("awk: input line? ", line, sizeof(line)) < 0) {
        printf("awk: no input provided\n");
        return 1;
    }
    if (strstr(line, pattern)) {
        printf("%s\n", line);
    } else {
        printf("awk: no match for '%s'\n", pattern);
    }
    return 0;
}
