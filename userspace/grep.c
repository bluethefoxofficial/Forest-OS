#include "tool_runtime.h"

void _start(void) {
    char pattern[64];
    char text[256];
    if (tr_read_line("grep: pattern: ", pattern, sizeof(pattern)) < 0) {
        printf("grep: no pattern provided\n");
        exit(1);
    }
    if (tr_read_line("grep: text to search: ", text, sizeof(text)) < 0) {
        printf("grep: no text provided\n");
        exit(1);
    }
    if (strstr(text, pattern)) {
        printf("%s\n", text);
        exit(0);
    }
    printf("grep: no match\n");
    exit(1);
}
