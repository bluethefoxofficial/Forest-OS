#ifndef USERSPACE_TOOL_RUNTIME_H
#define USERSPACE_TOOL_RUNTIME_H

#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/string.h"
#include "../src/include/libc/time.h"
#include "../src/include/libc/unistd.h"

#ifndef O_RDONLY
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#define O_APPEND 1024
#endif
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_END 2
#endif

static inline ssize_t tr_read_line(const char *prompt, char *buffer, size_t buffer_len) {
    if (!buffer || buffer_len < 2) {
        return -1;
    }
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

static inline void tr_append_log(const char *path, const char *line) {
    if (!path || !line) {
        return;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0) {
        return;
    }
    write(fd, line, strlen(line));
    write(fd, "\n", 1);
    close(fd);
}

static inline int tr_copy_file(const char *src, const char *dest) {
    if (!src || !dest) {
        return 1;
    }
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) {
        printf("unable to open source %s\n", src);
        return 1;
    }
    int out_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC);
    if (out_fd < 0) {
        printf("unable to open destination %s\n", dest);
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
                printf("write failure while copying to %s\n", dest);
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

static inline long tr_count_file_bytes(const char *path) {
    if (!path) {
        return -1;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    long total = 0;
    char buf[256];
    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        total += n;
    }
    close(fd);
    return total;
}

#endif
