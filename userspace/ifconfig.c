#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"

void _start(void) {
    char addr[32];
    forest_format_ipv4(INADDR_LOOPBACK, addr, sizeof(addr));
    printf("lo: flags=up loopback\n    inet %s\n", addr);

    net_socket_info_t sockets[16];
    int count = forest_netinfo(sockets, 16);
    if (count < 0) {
        count = 0;
    }
    printf("\nSockets bound on loopback (%d)\n", count);
    for (int i = 0; i < count; i++) {
        printf("  port %u bytes rx %u tx %u queue %u/%u\n",
               sockets[i].port, sockets[i].bytes_received, sockets[i].bytes_sent,
               sockets[i].queue_depth, sockets[i].queue_capacity);
    }
    exit(0);
}
