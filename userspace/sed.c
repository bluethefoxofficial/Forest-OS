#include "tool_runtime.h"

void _start(void) {
    char find[64];
    char replace[64];
    char text[256];
    if (tr_read_line("sed: find: ", find, sizeof(find)) < 0) {
        printf("sed: no find pattern\n");
        exit(1);
    }
    if (tr_read_line("sed: replace with: ", replace, sizeof(replace)) < 0) {
        printf("sed: no replacement\n");
        exit(1);
    }
    if (tr_read_line("sed: text: ", text, sizeof(text)) < 0) {
        printf("sed: no text\n");
        exit(1);
    }
    char *pos = strstr(text, find);
    if (!pos) {
        printf("%s\n", text);
        exit(0);
    }
    char output[256];
    size_t prefix = (size_t)(pos - text);
    size_t find_len = strlen(find);
    snprintf(output, sizeof(output), "%.*s%s%s", (int)prefix, text, replace, pos + find_len);
    printf("%s\n", output);
    exit(0);
}
