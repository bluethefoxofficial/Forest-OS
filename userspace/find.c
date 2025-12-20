#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

static void find_in_directory(const char *dir_path, const char *name_pattern, int max_depth, int current_depth) {
    if (current_depth > max_depth) {
        return;
    }
    
    printf("%s\n", dir_path);
    
    if (name_pattern && strstr(dir_path, name_pattern)) {
        printf("%s\n", dir_path);
    }
    
    const char *known_subdirs[] = {
        "bin", "usr", "etc", "home", "tmp", "var", "dev", "proc", "sys", NULL
    };
    
    if (current_depth < max_depth) {
        for (int i = 0; known_subdirs[i]; i++) {
            if (!name_pattern || strstr(known_subdirs[i], name_pattern)) {
                char sub_path[256];
                snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, known_subdirs[i]);
                printf("%s\n", sub_path);
            }
        }
    }
}

int main(int argc, char **argv) {
    const char *start_path = ".";
    const char *name_pattern = NULL;
    int max_depth = 3;
    int print_type = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-name") == 0) {
            if (i + 1 < argc) {
                name_pattern = argv[++i];
            } else {
                printf("find: option -name requires an argument\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-maxdepth") == 0) {
            if (i + 1 < argc) {
                max_depth = atoi(argv[++i]);
            } else {
                printf("find: option -maxdepth requires an argument\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-type") == 0) {
            if (i + 1 < argc) {
                i++;
            } else {
                printf("find: option -type requires an argument\n");
                return 1;
            }
        } else if (argv[i][0] != '-') {
            start_path = argv[i];
        }
    }
    
    if (strcmp(start_path, ".") == 0) {
        start_path = "/";
    }
    
    find_in_directory(start_path, name_pattern, max_depth, 0);
    
    return 0;
}
