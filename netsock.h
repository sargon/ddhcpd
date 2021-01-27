#ifndef _NETSOCK_H
#define _NETSOCK_H

#include "types.h"
#include "epoll.h"
#include "util.h"

#define DDHCP_MULTICAST_PORT 1234
#define DDHCP_UNICAST_PORT 1235

const struct in6_addr in6addr_localmast;

/* ddhcpd_socket_init_t implementations */
ATTR_NONNULL_ALL int netsock_multicast_init(epoll_data_t data,
					    ddhcp_config_t *config);
ATTR_NONNULL_ALL int netsock_server_init(epoll_data_t data,
					 ddhcp_config_t *config);
ATTR_NONNULL_ALL int netsock_dhcp_init(epoll_data_t data,
				       ddhcp_config_t *config);
ATTR_NONNULL_ALL int netsock_control_init(epoll_data_t data,
					  ddhcp_config_t *config);

#endif
