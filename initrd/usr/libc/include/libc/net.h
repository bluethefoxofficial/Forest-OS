#ifndef LIBC_NET_H
#define LIBC_NET_H

#include "../types.h"

#define AF_INET    2
#define SOCK_DGRAM 2

#define INADDR_ANY       0x00000000
#define INADDR_LOOPBACK  0x7F000001

typedef uint32 socklen_t;

typedef struct {
    uint16 sa_family;
    char   sa_data[14];
} sockaddr;

typedef struct {
    uint16 sin_family;
    uint16 sin_port;
    uint32 sin_addr;
    uint8  sin_zero[8];
} sockaddr_in;

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

#endif
