#ifndef LIBC_NETLIB_H
#define LIBC_NETLIB_H

#include "net.h"
#include <stddef.h>
#include <stdbool.h>

uint32 forest_parse_ipv4(const char* text, bool* ok);
void forest_format_ipv4(uint32 addr, char* buffer, size_t buffer_len);
int forest_dns_resolve(const char* hostname, uint32* out_addr);
int forest_echo_exchange(const char* payload, size_t length, char* reply, size_t reply_len);
int forest_port_query(uint16 port, const char* payload, size_t payload_len, char* buffer, size_t buffer_len);
int forest_http_get(const char* path, char* buffer, size_t buffer_len);
int forest_netinfo(net_socket_info_t* info, int max_entries);

#endif
