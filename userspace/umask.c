#include "tool_runtime.h"

static int current_mask = 022;

void _start(void) {
    char mask_str[16];
    if (tr_read_line("umask (current 0%o): ", mask_str, sizeof(mask_str)) < 0 || mask_str[0] == '\0') {
        printf("umask remains 0%o\n", current_mask);
        exit(0);
    }
    current_mask = (int)strtol(mask_str, NULL, 8);
    printf("umask set to 0%o\n", current_mask);
    exit(0);
}
