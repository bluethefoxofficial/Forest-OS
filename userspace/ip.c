#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"
#include "../src/include/libc/stdlib.h"

static void print_interface(void) {
    char addr[32];
    forest_format_ipv4(INADDR_LOOPBACK, addr, sizeof(addr));
    printf("loopback: inet %s scope host up\n", addr);
}

void _start(void) {
    printf("Forest ip (loopback view)\n");
    print_interface();

    net_socket_info_t sockets[16];
    int count = forest_netinfo(sockets, 16);
    if (count < 0) {
        count = 0;
    }
    printf("active sockets: %d\n", count);
    for (int i = 0; i < count; i++) {
        printf(" fd=%d port=%u rx=%u tx=%u queue=%u/%u last_peer=%u\n",
               64 + i, sockets[i].port, sockets[i].bytes_received, sockets[i].bytes_sent,
               sockets[i].queue_depth, sockets[i].queue_capacity, sockets[i].last_peer_port);
    }
    exit(0);
}
