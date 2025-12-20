#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"

static int grep_file(const char *pattern, const char *filename, int line_numbers, int invert_match, int count_only) {
    int fd = open(filename, 0);
    if (fd < 0) {
        printf("grep: %s: No such file or directory\n", filename);
        return 1;
    }
    
    char buffer[1024];
    char line[256];
    int line_count = 0;
    int match_count = 0;
    int bytes_read = 0;
    int line_pos = 0;
    
    while (1) {
        int n = read(fd, buffer + bytes_read, sizeof(buffer) - bytes_read - 1);
        if (n <= 0) {
            break;
        }
        bytes_read += n;
        buffer[bytes_read] = '\0';
        
        char *line_start = buffer;
        char *line_end;
        
        while ((line_end = strchr(line_start, '\n')) != NULL) {
            int line_len = line_end - line_start;
            if (line_len < sizeof(line) - 1) {
                strncpy(line, line_start, line_len);
                line[line_len] = '\0';
                
                line_count++;
                int has_match = strstr(line, pattern) != NULL;
                if (invert_match) {
                    has_match = !has_match;
                }
                
                if (has_match) {
                    match_count++;
                    if (!count_only) {
                        if (line_numbers) {
                            printf("%d:", line_count);
                        }
                        printf("%s\n", line);
                    }
                }
            }
            
            line_start = line_end + 1;
        }
        
        int remaining = buffer + bytes_read - line_start;
        if (remaining > 0) {
            memmove(buffer, line_start, remaining);
            bytes_read = remaining;
        } else {
            bytes_read = 0;
        }
    }
    
    if (count_only) {
        printf("%d\n", match_count);
    }
    
    close(fd);
    return match_count > 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    int line_numbers = 0;
    int invert_match = 0;
    int count_only = 0;
    int ignore_case = 0;
    const char *pattern = NULL;
    int arg_start = 1;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'n':
                        line_numbers = 1;
                        break;
                    case 'v':
                        invert_match = 1;
                        break;
                    case 'c':
                        count_only = 1;
                        break;
                    case 'i':
                        ignore_case = 1;
                        break;
                    default:
                        printf("grep: invalid option -- '%c'\n", argv[i][j]);
                        return 1;
                }
            }
        } else if (!pattern) {
            pattern = argv[i];
            arg_start = i + 1;
            break;
        }
    }
    
    if (!pattern) {
        printf("grep: missing pattern\n");
        printf("Usage: grep [-nvc] pattern [file...]\n");
        return 1;
    }
    
    if (arg_start >= argc) {
        char buffer[256];
        while (1) {
            int n = read(0, buffer, sizeof(buffer) - 1);
            if (n <= 0) {
                break;
            }
            buffer[n] = '\0';
            if (strstr(buffer, pattern)) {
                printf("%s", buffer);
            }
        }
        return 0;
    }
    
    int status = 0;
    for (int i = arg_start; i < argc; i++) {
        if (grep_file(pattern, argv[i], line_numbers, invert_match, count_only) != 0) {
            status = 1;
        }
    }
    
    return status;
}
