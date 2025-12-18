#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/string.h"
#include "../src/include/libc/unistd.h"

#ifndef O_RDONLY
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512
#endif

static ssize_t read_line(const char *prompt, char *buffer, size_t buffer_len) {
    if (prompt) {
        printf("%s", prompt);
    }
    ssize_t n = read(0, buffer, buffer_len - 1);
    if (n <= 0) {
        return -1;
    }
    buffer[n] = '\0';
    char *newline = strchr(buffer, '\n');
    if (newline) {
        *newline = '\0';
    }
    return n;
}

static int copy_file(const char *src, const char *dest) {
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) {
        printf("cp: unable to open source %s\n", src);
        return 1;
    }

    int out_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC);
    if (out_fd < 0) {
        printf("cp: unable to open destination %s\n", dest);
        close(in_fd);
        return 1;
    }

    char buffer[256];
    while (1) {
        ssize_t bytes_read = read(in_fd, buffer, sizeof(buffer));
        if (bytes_read <= 0) {
            break;
        }
        ssize_t offset = 0;
        while (offset < bytes_read) {
            ssize_t written = write(out_fd, buffer + offset, (size_t)(bytes_read - offset));
            if (written <= 0) {
                printf("cp: failed while writing to %s\n", dest);
                close(in_fd);
                close(out_fd);
                return 1;
            }
            offset += written;
        }
    }

    close(in_fd);
    close(out_fd);
    return 0;
}

void _start(void) {
    char src[128];
    char dest[128];

    if (read_line("cp: enter source file path: ", src, sizeof(src)) < 0) {
        printf("cp: no source provided\n");
        exit(1);
    }
    if (read_line("cp: enter destination file path: ", dest, sizeof(dest)) < 0) {
        printf("cp: no destination provided\n");
        exit(1);
    }

    int status = copy_file(src, dest);
    exit(status);
}
