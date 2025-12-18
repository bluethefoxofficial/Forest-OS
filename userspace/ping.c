#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"

void _start(void) {
    char payload[] = "forest-ping";
    char reply[128];

    printf("PING loopback %s\n", "127.0.0.1");
    for (int i = 0; i < 4; i++) {
        int got = forest_echo_exchange(payload, sizeof(payload) - 1, reply, sizeof(reply));
        if (got > 0) {
            reply[got] = '\0';
            printf("64 bytes from 127.0.0.1: seq=%d payload='%s'\n", i, reply);
        } else {
            printf("ping timeout on seq=%d\n", i);
        }
    }
    exit(0);
}
