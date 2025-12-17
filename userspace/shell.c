#include <stdbool.h>
#include <stddef.h>
#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/string.h"
#include "../src/include/libc/unistd.h"

#define MAX_INPUT 128

static bool starts_with(const char* text, const char* prefix) {
    size_t idx = 0;
    while (prefix[idx]) {
        if (text[idx] != prefix[idx]) {
            return false;
        }
        idx++;
    }
    return true;
}

static void handle_help(void) {
    printf("Forest shell built-ins:\n");
    printf("  help        - show this help\n");
    printf("  uname       - print kernel identity\n");
    printf("  time        - show fake system time\n");
    printf("  catreadme   - dump /README.txt\n");
    printf("  echo <text> - display text\n");
    printf("  exit        - return to kernel\n");
}

static void handle_uname(void) {
    extern int uname(void *);
    typedef struct {
        char sysname[32];
        char nodename[32];
        char release[32];
        char version[32];
        char machine[32];
    } utsname_t;
    utsname_t info;
    if (uname(&info) == 0) {
        printf("%s %s (%s) %s\n", info.sysname, info.release, info.version, info.machine);
    } else {
        printf("uname: syscall unavailable\n");
    }
}

static void handle_time(void) {
    int now = time(NULL);
    printf("Kernel reports epoch: %d\n", now);
}

static void handle_catreadme(void) {
    int fd = open("/README.txt", 0);
    if (fd < 0) {
        printf("catreadme: unable to open /README.txt\n");
        return;
    }
    char buffer[256];
    ssize_t n;
    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        write(1, buffer, (size_t)n);
    }
    close(fd);
    printf("\n");
}

static void handle_echo(const char* text) {
    if (!text || !*text) {
        printf("echo: missing text\n");
        return;
    }
    printf("%s\n", text);
}

static int read_line(char* buffer, size_t max_len) {
    ssize_t read_bytes = read(0, buffer, max_len - 1);
    if (read_bytes <= 0) {
        return 0;
    }
    buffer[read_bytes] = '\0';
    // remove trailing newline
    size_t len = strlen(buffer);
    if (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        buffer[len - 1] = '\0';
    }
    return 1;
}

void _start(void) {
    printf("Forest Shell (userspace)\nType 'help' for a list of commands.\n");
    char line[MAX_INPUT];

    while (1) {
        printf("forest> ");
        if (!read_line(line, sizeof(line))) {
            continue;
        }

        if (strcmp(line, "help") == 0) {
            handle_help();
        } else if (strcmp(line, "uname") == 0) {
            handle_uname();
        } else if (strcmp(line, "time") == 0) {
            handle_time();
        } else if (strcmp(line, "catreadme") == 0) {
            handle_catreadme();
        } else if (strcmp(line, "exit") == 0) {
            printf("logout\n");
            exit(0);
        } else if (starts_with(line, "echo ")) {
            handle_echo(line + 5);
        } else if (line[0] == '\0') {
            continue;
        } else {
            printf("Unknown command: %s\n", line);
        }
    }
}
