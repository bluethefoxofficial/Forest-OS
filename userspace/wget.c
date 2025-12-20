#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/string.h"

#ifndef O_RDONLY
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512
#endif

static const char *extract_filename(const char *url) {
    const char *last_slash = strrchr(url, '/');
    if (last_slash && last_slash[1]) {
        return last_slash + 1;
    }
    return "index.html";
}

int main(int argc, char **argv) {
    const char *url = "/";
    const char *output_file = NULL;
    int quiet = 0;
    int continue_download = 0;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'O':
                        if (i + 1 < argc) {
                            output_file = argv[++i];
                        } else {
                            printf("wget: option -O requires an argument\n");
                            return 1;
                        }
                        break;
                    case 'q':
                        quiet = 1;
                        break;
                    case 'c':
                        continue_download = 1;
                        break;
                    default:
                        printf("wget: invalid option -- '%c'\n", argv[i][j]);
                        return 1;
                }
            }
        } else {
            url = argv[i];
        }
    }
    
    if (!output_file) {
        output_file = extract_filename(url);
    }
    
    char buffer[512];
    int received = forest_http_get(url, buffer, sizeof(buffer) - 1);
    
    if (received > 0) {
        buffer[received] = '\0';
        
        if (!quiet) {
            printf("--2024-12-18 00:00:00--  http://localhost%s\n", url);
            printf("Resolving localhost... 127.0.0.1\n");
            printf("Connecting to localhost|127.0.0.1|:80... connected.\n");
            printf("HTTP request sent, awaiting response... 200 OK\n");
            printf("Length: %d [text/html]\n", received);
            printf("Saving to: '%s'\n", output_file);
        }
        
        if (strcmp(output_file, "-") == 0) {
            printf("%s", buffer);
        } else {
            int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC);
            if (fd < 0) {
                printf("wget: cannot write to '%s'\n", output_file);
                return 1;
            }
            write(fd, buffer, received);
            close(fd);
            
            if (!quiet) {
                printf("\n'%s' saved [%d/%d]\n", output_file, received, received);
            }
        }
    } else {
        printf("wget: unable to connect to remote host\n");
        return 1;
    }
    
    return 0;
}
