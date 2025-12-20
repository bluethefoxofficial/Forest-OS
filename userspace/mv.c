#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"

#ifndef O_RDONLY
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512
#endif

static int copy_file(const char *src, const char *dest) {
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) {
        return 1;
    }
    
    int out_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC);
    if (out_fd < 0) {
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
    int verbose = 0;
    int src_start = 1;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'v':
                        verbose = 1;
                        break;
                    default:
                        printf("mv: invalid option -- '%c'\n", argv[i][j]);
                        return 1;
                }
            }
            src_start++;
        } else {
            break;
        }
    }
    
    if (argc < src_start + 2) {
        printf("mv: missing file operand\n");
        printf("Usage: mv [-v] source dest\n");
        return 1;
    }
    
    const char *src = argv[src_start];
    const char *dest = argv[src_start + 1];
    
    if (copy_file(src, dest) == 0) {
        if (unlink(src) == 0) {
            if (verbose) {
                printf("'%s' -> '%s'\n", src, dest);
            }
            return 0;
        }
    }
    
    printf("mv: cannot move '%s' to '%s'\n", src, dest);
    return 1;
}
