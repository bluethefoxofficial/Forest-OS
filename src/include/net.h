#ifndef NET_H
#define NET_H

#include "types.h"
#include "driver.h"
#include <stdbool.h>

#define AF_INET    2
#define SOCK_DGRAM 2

#define INADDR_ANY       0x00000000
#define INADDR_LOOPBACK  0x7F000001

#ifndef SOCKLEN_T_DEFINED
#define SOCKLEN_T_DEFINED
typedef uint32 socklen_t;
#endif

typedef struct {
    uint16 sa_family;
    char   sa_data[14];
} sockaddr_t;

typedef struct {
    uint16 sin_family;
    uint16 sin_port;
    uint32 sin_addr;
    uint8  sin_zero[8];
} sockaddr_in_t;

static inline uint16 htons(uint16 value) {
    return (uint16)((value << 8) | (value >> 8));
}

static inline uint16 ntohs(uint16 value) {
    return htons(value);
}

static inline uint32 htonl(uint32 value) {
    return ((value & 0x000000ffU) << 24) |
           ((value & 0x0000ff00U) << 8)  |
           ((value & 0x00ff0000U) >> 8)  |
           ((value & 0xff000000U) >> 24);
}

static inline uint32 ntohl(uint32 value) {
    return htonl(value);
}

bool net_init(void);
bool net_is_fd(uint32 fd);
int32 net_close(uint32 fd);
int32 net_socket_create(uint32 domain, uint32 type, uint32 protocol);
int32 net_bind(uint32 fd, uint16 port);
int32 net_send_datagram(uint32 fd, const uint8* buffer, uint32 length,
                        uint32 dest_addr, uint16 dest_port);
int32 net_recv_datagram(uint32 fd, uint8* buffer, uint32 length,
                        uint32* out_addr, uint16* out_port);

#endif
