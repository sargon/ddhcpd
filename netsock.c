/*
 * Copyright (C) 2012-2015  B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include "netsock.h"
#include "packet.h"
#include "logger.h"

// ff02::1234.1234
struct in6_addr in6addr_localmcast = { { { 0xff, 0x02, 0x00, 0x00, 0x00, 0x00,
					   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					   0x00, 0x00, 0x12, 0x34 } } };

struct in6_addr *get_ipv6_linklokal(char *interface)
{
	struct ifaddrs *ifaddr, *ifa;
	int n;

	if (getifaddrs(&ifaddr) == -1) {
		ERROR("get_ipv6_linklokal(...): getifaddrs failed\n");
		return NULL;
	}

	for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
		if (ifa->ifa_addr == NULL) {
			continue;
		}

		if (strcmp(ifa->ifa_name, interface) == 0) {
			if (ifa->ifa_addr->sa_family == AF_INET6) {
				if (((struct sockaddr_in6 *)ifa->ifa_addr)
					    ->sin6_scope_id > 0) {
					struct in6_addr *address =
						malloc(sizeof(struct in6_addr));
					memcpy(address,
					       &((struct sockaddr_in6 *)
							 ifa->ifa_addr)
							->sin6_addr,
					       sizeof(struct in6_addr));
					freeifaddrs(ifaddr);
					return address;
				}
			}
		}
	}

	freeifaddrs(ifaddr);
	return NULL;
}

ATTR_NONNULL_ALL int netsock_unix_socket_open(ddhcp_epoll_data *data)
{
	int ctl_sock;
	struct sockaddr_un s_un;
	int ret;

	ctl_sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	memset(&s_un, 0, sizeof(s_un));
	s_un.sun_family = AF_UNIX;

	strncpy(s_un.sun_path, data->interface_name, sizeof(s_un.sun_path));

	unlink(data->interface_name);

	if (bind(ctl_sock, (struct sockaddr *)&s_un, sizeof(s_un)) < 0) {
		perror("can't bind control socket");
		goto err;
	}

	ret = fcntl(ctl_sock, F_GETFL, 0);
	ret = fcntl(ctl_sock, F_SETFL, ret | O_NONBLOCK);

	if (ret < 0) {
		perror("failed to set file status flags");
		goto err;
	}

	if (listen(ctl_sock, 2) < 0) {
		perror("failed ot listen");
		goto err;
	}

	data->fd = ctl_sock;
	data->interface_id = 0;
	return 0;

err:
	close(ctl_sock);
	return -1;
}

ATTR_NONNULL_ALL void netsocket_unix_socket_close(ddhcp_config *config)
{
	close(config->control_socket);
	remove(config->control_path);
}

ATTR_NONNULL_ALL int netsock_open_socket_v6(ddhcp_epoll_data *data,
					    struct in6_addr *addr,
					    uint16_t port)
{
	struct sockaddr_in6 sin6 = { 0 };

	sin6.sin6_family = AF_INET6;
	memcpy(&sin6.sin6_addr, addr, sizeof(struct in6_addr));
	sin6.sin6_port = htons(port);
	sin6.sin6_scope_id = if_nametoindex(data->interface_name);

	int sock = socket(PF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
	if (sock < 0) {
		FATAL("netsock_open_socket_v6(...): unable to create socket\n");
	}

	if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, data->interface_name,
		       (socklen_t)strlen(data->interface_name))) {
		FATAL("netsock_open_socket_v6(...): setsockopt: can't bind to device '%s'\n",
		      data->interface_name);
		goto error;
	}

	if (bind(sock, (struct sockaddr *)&sin6, sizeof(struct sockaddr_in6)) <
	    0) {
		FATAL("netsock_open_socket_v6(...): unable to bind %s\n",
		      data->interface_name);
		perror("bind");
		goto error;
	}

	data->fd = sock;
	data->interface_id = sin6.sin6_scope_id;

	return 1;
error:
	close(sock);
	return -1;
}

ATTR_NONNULL_ALL int netsock_multicast_join(ddhcp_epoll_data *data,
					    struct in6_addr *addr)
{
	unsigned int zero = 0;
	struct ipv6_mreq mreq = { 0 };

	memcpy(&mreq.ipv6mr_multiaddr, addr, sizeof(struct in6_addr));
	mreq.ipv6mr_interface = data->interface_id;

	if (setsockopt(data->fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq,
		       sizeof(struct ipv6_mreq))) {
		perror("can't add multicast membership");
		goto error;
	}

	if (setsockopt(data->fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &zero,
		       sizeof(unsigned int))) {
		perror("can't unset multicast loop");
		goto error;
	}

	return 1;

error:
	close(data->fd);
	return -1;
}

ATTR_NONNULL_ALL int netsock_open_dhcp(ddhcp_epoll_data *data, uint16_t port)
{
	int sock;
	struct sockaddr_in sin;
	struct in_addr address_client;
	unsigned int broadcast = 1;

	sock = socket(PF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);

	if (sock < 0) {
		perror("can't open broadcast socket");
		return -1;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_port = htons(port);
	sin.sin_family = AF_INET;

	inet_aton("0.0.0.0", &address_client);
	memcpy(&sin.sin_addr, &address_client, sizeof(sin.sin_addr));

	if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, data->interface_name,
		       (socklen_t)strlen(data->interface_name) + 1)) {
		perror("can't bind to broadcast device");
		close(sock);
		return -1;
	}

	if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		perror("can't bind broadcast socket");
		close(sock);
		return -1;
	}

	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast,
		       sizeof(unsigned int))) {
		perror("can't set broadcast on client socket");
		close(sock);
		return -1;
	}

	data->fd = sock;
	data->interface_id = if_nametoindex(data->interface_name);

	return sock;
}

// DDHCPD_SOCKET_INIT_T for all the different socket types we handle

ATTR_NONNULL_ALL int netsock_multicast_init(epoll_data_t data,
					    ddhcp_config *config)
{
	ddhcp_epoll_data *ptr = (ddhcp_epoll_data *)data.ptr;
	if ((netsock_open_socket_v6(ptr, &in6addr_localmcast,
				    DDHCP_MULTICAST_PORT)) < 0) {
		FATAL("netsock_init(...): Unable to open multicast socket\n");
		return -1;
	}

	if (netsock_multicast_join(ptr, &in6addr_localmcast) < 0) {
		FATAL("netsock_init(...): Unable to join multicast group\n");
		close(ptr->fd);
		return -1;
	}
	UNUSED(config);
	return 0;
}

ATTR_NONNULL_ALL int netsock_server_init(epoll_data_t data,
					 ddhcp_config *config)
{
	ddhcp_epoll_data *ptr = (ddhcp_epoll_data *)data.ptr;
	if ((netsock_open_socket_v6(ptr, (struct in6_addr *)&in6addr_any,
				    DDHCP_UNICAST_PORT)) < 0) {
		FATAL("netsock_init(...): Unable to open server socket\n");
		return -1;
	}
	UNUSED(config);
	return 0;
}

ATTR_NONNULL_ALL int netsock_dhcp_init(epoll_data_t data, ddhcp_config *config)
{
	ddhcp_epoll_data *ptr = (ddhcp_epoll_data *)data.ptr;
	if ((netsock_open_dhcp(ptr, config->dhcp_port)) < 0) {
		FATAL("netsock_init(...): Unable to open dhcp socket\n");
		return -1;
	}
	UNUSED(config);
	return 0;
}

ATTR_NONNULL_ALL int netsock_control_init(epoll_data_t data,
					  ddhcp_config *config)
{
	ddhcp_epoll_data *ptr = (ddhcp_epoll_data *)data.ptr;
	if ((netsock_unix_socket_open(ptr)) < 0) {
		FATAL("netsock_init(...): Unable to open control socket\n");
		return -1;
	}
	UNUSED(config);
	return 0;
}
