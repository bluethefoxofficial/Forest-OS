#include "tool_runtime.h"

void _start(void) {
    char pattern[64];
    if (tr_read_line("find: substring to locate in /proc/mounts: ", pattern, sizeof(pattern)) < 0) {
        printf("find: no pattern provided\n");
        exit(1);
    }
    char buf[512];
    int fd = open("/proc/mounts", O_RDONLY);
    if (fd < 0) {
        printf("find: unable to read /proc/mounts\n");
        exit(1);
    }
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    close(fd);
    char *line = strtok(buf, "\n");
    int shown = 0;
    while (line) {
        if (strstr(line, pattern)) {
            printf("%s\n", line);
            shown++;
        }
        line = strtok(NULL, "\n");
    }
    if (!shown) {
        printf("find: no entries matched '%s'\n", pattern);
    }
    exit(0);
}
