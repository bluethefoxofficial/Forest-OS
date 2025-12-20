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

int main(int argc, char **argv) {
    int recursive = 0;
    int verbose = 0;
    int src_start = 1;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'r':
                    case 'R':
                        recursive = 1;
                        break;
                    case 'v':
                        verbose = 1;
                        break;
                    default:
                        printf("cp: invalid option -- '%c'\n", argv[i][j]);
                        return 1;
                }
            }
            src_start++;
        } else {
            break;
        }
    }
    
    if (argc < src_start + 2) {
        printf("cp: missing file operand\n");
        printf("Usage: cp [-rv] source dest\n");
        return 1;
    }
    
    const char *src = argv[src_start];
    const char *dest = argv[src_start + 1];
    
    if (verbose) {
        printf("'%s' -> '%s'\n", src, dest);
    }
    
    int status = copy_file(src, dest);
    return status;
}
