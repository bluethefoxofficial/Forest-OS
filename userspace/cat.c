#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"

static int print_file(const char *path) {
    int fd = open(path, 0);
    if (fd < 0) {
        printf("cat: %s: No such file or directory\n", path);
        return 1;
    }

    char buffer[256];
    while (1) {
        int n = read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        write(1, buffer, (size_t)n);
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    int status = 0;
    
    if (argc == 1) {
        char buffer[256];
        while (1) {
            int n = read(0, buffer, sizeof(buffer));
            if (n <= 0) {
                break;
            }
            write(1, buffer, (size_t)n);
        }
    } else {
        for (int i = 1; i < argc; i++) {
            if (print_file(argv[i]) != 0) {
                status = 1;
            }
        }
    }
    
    return status;
}
