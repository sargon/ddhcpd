/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - Raw packet handling
 *
 *  See AUTHORS file for copyright holders
 */

#ifndef _DDHCP_PACKET_H
#define _DDHCP_PACKET_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "types.h"
#include "epoll.h"

#define DDHCP_MSG_UPDATECLAIM 1
#define DDHCP_MSG_INQUIRE 2
#define DDHCP_MSG_RENEWLEASE 16
#define DDHCP_MSG_LEASEACK 17
#define DDHCP_MSG_LEASENAK 18
#define DDHCP_MSG_RELEASE 19

struct ddhcp_mcast_packet {
	ddhcp_node_id node_id;
	struct in_addr prefix;
	uint8_t prefix_len;
	uint8_t blocksize;
	uint8_t command;
	uint8_t count;

	struct sockaddr_in6 *sender;

	union {
		struct ddhcp_payload *payload;
		struct ddhcp_renew_payload *renew_payload;
	};
};
typedef struct ddhcp_mcast_packet ddhcp_mcast_packet;

struct ddhcp_payload {
	uint32_t block_index;
	uint16_t timeout;
	uint16_t reserved;
};
typedef struct ddhcp_payload ddhcp_payload;

struct ddhcp_renew_payload {
	uint8_t chaddr[16];
	uint32_t address;
	uint32_t xid;
	uint32_t lease_seconds;
};
typedef struct ddhcp_renew_payload ddhcp_renew_payload;

ATTR_NONNULL_ALL struct ddhcp_mcast_packet *
new_ddhcp_packet(uint8_t command, ddhcp_config_t *config);
ATTR_NONNULL_ALL ssize_t ntoh_mcast_packet(uint8_t *buf, ssize_t len,
					   struct ddhcp_mcast_packet *packet);

ATTR_NONNULL_ALL ssize_t send_packet_mcast(struct ddhcp_mcast_packet *packet,
					   ddhcp_epoll_data *data);
ATTR_NONNULL_ALL ssize_t send_packet_direct(struct ddhcp_mcast_packet *packet,
					    struct in6_addr *dest,
					    ddhcp_epoll_data *data);

#endif
