#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"

static void process_substitute(const char *pattern, const char *replacement, int global_replace) {
    char line[512];
    int bytes_read = 0;
    
    while (1) {
        int n = read(0, line + bytes_read, sizeof(line) - bytes_read - 1);
        if (n <= 0) {
            break;
        }
        bytes_read += n;
        line[bytes_read] = '\0';
        
        char *line_start = line;
        char *line_end;
        
        while ((line_end = strchr(line_start, '\n')) != NULL) {
            *line_end = '\0';
            
            char output[512];
            char *current = line_start;
            char *output_ptr = output;
            int remaining_output = sizeof(output) - 1;
            
            while (remaining_output > 0) {
                char *match = strstr(current, pattern);
                if (!match) {
                    int len = strlen(current);
                    if (len <= remaining_output) {
                        strcpy(output_ptr, current);
                        output_ptr += len;
                        remaining_output -= len;
                    }
                    break;
                }
                
                int prefix_len = match - current;
                if (prefix_len <= remaining_output) {
                    strncpy(output_ptr, current, prefix_len);
                    output_ptr += prefix_len;
                    remaining_output -= prefix_len;
                }
                
                int repl_len = strlen(replacement);
                if (repl_len <= remaining_output) {
                    strcpy(output_ptr, replacement);
                    output_ptr += repl_len;
                    remaining_output -= repl_len;
                }
                
                current = match + strlen(pattern);
                
                if (!global_replace) {
                    int len = strlen(current);
                    if (len <= remaining_output) {
                        strcpy(output_ptr, current);
                        output_ptr += len;
                        remaining_output -= len;
                    }
                    break;
                }
            }
            
            *output_ptr = '\0';
            printf("%s\n", output);
            
            line_start = line_end + 1;
        }
        
        int remaining = line + bytes_read - line_start;
        if (remaining > 0) {
            memmove(line, line_start, remaining);
            bytes_read = remaining;
        } else {
            bytes_read = 0;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("sed: missing script\n");
        printf("Usage: sed 's/pattern/replacement/[g]' [file]\n");
        return 1;
    }
    
    const char *script = argv[1];
    
    if (script[0] == 's' && script[1] == '/') {
        char pattern[64];
        char replacement[64];
        int global_replace = 0;
        
        const char *start = script + 2;
        const char *delim1 = strchr(start, '/');
        if (!delim1) {
            printf("sed: invalid substitute command\n");
            return 1;
        }
        
        int pattern_len = delim1 - start;
        if (pattern_len >= sizeof(pattern)) {
            pattern_len = sizeof(pattern) - 1;
        }
        strncpy(pattern, start, pattern_len);
        pattern[pattern_len] = '\0';
        
        const char *repl_start = delim1 + 1;
        const char *delim2 = strchr(repl_start, '/');
        if (!delim2) {
            printf("sed: invalid substitute command\n");
            return 1;
        }
        
        int repl_len = delim2 - repl_start;
        if (repl_len >= sizeof(replacement)) {
            repl_len = sizeof(replacement) - 1;
        }
        strncpy(replacement, repl_start, repl_len);
        replacement[repl_len] = '\0';
        
        const char *flags = delim2 + 1;
        if (*flags == 'g') {
            global_replace = 1;
        }
        
        if (argc > 2) {
            // For now, just skip file input since dup2 isn't available
            printf("sed: file input not supported in this version\n");
            return 1;
        }
        
        process_substitute(pattern, replacement, global_replace);
    } else {
        printf("sed: unsupported command\n");
        return 1;
    }
    
    return 0;
}
