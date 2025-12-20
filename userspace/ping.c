#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"
#include "../src/include/libc/stdlib.h"

int main(int argc, char **argv) {
    const char *target = "127.0.0.1";
    int count = 4;
    int interval = 1;
    int quiet = 0;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'c':
                        if (i + 1 < argc) {
                            count = atoi(argv[++i]);
                        } else {
                            printf("ping: option -c requires an argument\n");
                            return 1;
                        }
                        break;
                    case 'i':
                        if (i + 1 < argc) {
                            interval = atoi(argv[++i]);
                        } else {
                            printf("ping: option -i requires an argument\n");
                            return 1;
                        }
                        break;
                    case 'q':
                        quiet = 1;
                        break;
                    default:
                        printf("ping: invalid option -- '%c'\n", argv[i][j]);
                        return 1;
                }
            }
        } else {
            target = argv[i];
        }
    }
    
    char payload[] = "forest-ping";
    char reply[128];
    int success_count = 0;

    if (!quiet) {
        printf("PING %s\n", target);
    }
    
    for (int i = 0; i < count; i++) {
        int got = forest_echo_exchange(payload, sizeof(payload) - 1, reply, sizeof(reply));
        if (got > 0) {
            reply[got] = '\0';
            success_count++;
            if (!quiet) {
                printf("64 bytes from %s: seq=%d payload='%s'\n", target, i, reply);
            }
        } else {
            if (!quiet) {
                printf("ping timeout on seq=%d\n", i);
            }
        }
        
        if (i < count - 1 && interval > 0) {
            struct timespec ts = { interval, 0 };
            nanosleep(&ts, NULL);
        }
    }
    
    if (!quiet) {
        printf("\n--- %s ping statistics ---\n", target);
        printf("%d packets transmitted, %d received, %d%% packet loss\n", 
               count, success_count, 
               count > 0 ? (100 * (count - success_count)) / count : 0);
    }
    
    return success_count > 0 ? 0 : 1;
}
