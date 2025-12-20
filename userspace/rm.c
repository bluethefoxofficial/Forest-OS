#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"

int main(int argc, char **argv) {
    int force = 0;
    int verbose = 0;
    int recursive = 0;
    int interactive = 0;
    int src_start = 1;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'f':
                        force = 1;
                        break;
                    case 'v':
                        verbose = 1;
                        break;
                    case 'r':
                    case 'R':
                        recursive = 1;
                        break;
                    case 'i':
                        interactive = 1;
                        break;
                    default:
                        printf("rm: invalid option -- '%c'\n", argv[i][j]);
                        return 1;
                }
            }
            src_start++;
        } else {
            break;
        }
    }
    
    if (argc < src_start + 1) {
        printf("rm: missing operand\n");
        printf("Usage: rm [-frvi] file...\n");
        return 1;
    }
    
    int status = 0;
    for (int i = src_start; i < argc; i++) {
        const char *path = argv[i];
        
        if (interactive) {
            printf("rm: remove '%s'? (y/n): ", path);
            char response[4];
            if (read(0, response, sizeof(response) - 1) <= 0 || 
                (response[0] != 'y' && response[0] != 'Y')) {
                continue;
            }
        }
        
        if (unlink(path) == 0) {
            if (verbose) {
                printf("removed '%s'\n", path);
            }
        } else {
            if (!force) {
                printf("rm: cannot remove '%s': No such file or directory\n", path);
                status = 1;
            }
        }
    }
    
    return status;
}
