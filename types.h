/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - Common type definitions
 *
 *  See AUTHORS file for copyright holders
 */

#ifndef _TYPES_H
#define _TYPES_H

#include <arpa/inet.h>
#include <time.h>

#define ATTR_NONNULL_ALL __attribute__((nonnull))
#define ATTR_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))

#include "list.h"
#include "dhcp_packet.h"

#define NODE_ID_CMP(id1, id2)                                                  \
	memcmp((char *)(id1), (char *)(id2), sizeof(ddhcp_node_id))

// node ident

typedef uint8_t ddhcp_node_id[8];
#define NODE_ID_CLEAR(id) memset(id, '\0', sizeof(ddhcp_node_id))
#define NODE_ID_CP(dest, src) memcpy(dest, src, sizeof(ddhcp_node_id))

// statistic fields
#ifdef DDHCPD_STATISTICS
enum { STAT_MCAST_RECV_PKG,
       STAT_MCAST_SEND_PKG,
       STAT_MCAST_RECV_BYTE,
       STAT_MCAST_SEND_BYTE,
       STAT_MCAST_SEND_UPDATECLAIM,
       STAT_MCAST_RECV_UPDATECLAIM,
       STAT_MCAST_SEND_INQUIRE,
       STAT_MCAST_RECV_INQUIRE,
       STAT_DIRECT_RECV_PKG,
       STAT_DIRECT_SEND_PKG,
       STAT_DIRECT_RECV_BYTE,
       STAT_DIRECT_SEND_BYTE,
       STAT_DIRECT_RECV_RENEWLEASE,
       STAT_DIRECT_SEND_RENEWLEASE,
       STAT_DIRECT_RECV_LEASEACK,
       STAT_DIRECT_SEND_LEASEACK,
       STAT_DIRECT_RECV_LEASENAK,
       STAT_DIRECT_SEND_LEASENAK,
       STAT_DIRECT_RECV_RELEASE,
       STAT_DIRECT_SEND_RELEASE,
       STAT_DHCP_RECV_PKG,
       STAT_DHCP_SEND_PKG,
       STAT_DHCP_RECV_BYTE,
       STAT_DHCP_SEND_BYTE,
       STAT_DHCP_RECV_DISCOVER,
       STAT_DHCP_SEND_OFFER,
       STAT_DHCP_RECV_REQUEST,
       STAT_DHCP_SEND_ACK,
       STAT_DHCP_SEND_NAK,
       STAT_DHCP_RECV_RELEASE,
       STAT_DHCP_RECV_INFORM,
       STAT_NUM_OF_FIELDS };
#endif

// block structures

enum ddhcp_block_state {
	DDHCP_FREE,
	DDHCP_TENTATIVE,
	DDHCP_CLAIMED,
	DDHCP_CLAIMING,
	DDHCP_OURS,
	DDHCP_BLOCKED
};

// List of ddhcp_block
typedef struct list_head ddhcp_block_list;

struct ddhcp_block {
	uint32_t index;
	enum ddhcp_block_state state;
	struct in_addr subnet;
	uint8_t subnet_len;
	uint8_t claiming_counts;
	ddhcp_node_id node_id;
	struct in6_addr owner_address;
	time_t first_claimed;
	time_t needless_since;
	time_t timeout;
	// Only iff state is equal to CLAIMED lease_block is not equal to NULL.
	struct dhcp_lease *addresses;

	ddhcp_block_list tmp_list;
	ddhcp_block_list claim_list;
};
typedef struct ddhcp_block ddhcp_block;

// DHCP structures

enum dhcp_lease_state {
	FREE,
	OFFERED,
	LEASED,
};

struct dhcp_lease {
	uint8_t chaddr[16];
	enum dhcp_lease_state state;
	uint32_t xid;
	time_t lease_end;
};
typedef struct dhcp_lease dhcp_lease;

// List of dhcp_option
typedef struct list_head dhcp_option_list;

struct dhcp_option {
	uint8_t code;
	uint8_t len;
	uint8_t *payload;

	dhcp_option_list option_list;
};
typedef struct dhcp_option dhcp_option;

enum dhcp_option_code {
	// RFC 2132
	DHCP_CODE_PAD = 0,
	DHCP_CODE_SUBNET_MASK = 1,
	DHCP_CODE_TIME_OFFSET = 2,
	DHCP_CODE_ROUTER = 3,
	DHCP_CODE_BROADCAST_ADDRESS = 28,
	DHCP_CODE_REQUESTED_ADDRESS = 50,
	DHCP_CODE_ADDRESS_LEASE_TIME = 51,
	DHCP_CODE_MESSAGE_TYPE = 53,
	DHCP_CODE_SERVER_IDENTIFIER = 54,
	DHCP_CODE_PARAMETER_REQUEST_LIST = 55,
	DHCP_CODE_END = 255,
};

// network socket
#define SKT_MCAST 0
#define SKT_SERVER 1
#define SKT_DHCP 2
#define SKT_CONTROL 3

#define DDHCP_SKT_MCAST(config) ((ddhcp_epoll_data *)config->sockets[SKT_MCAST])
#define DDHCP_SKT_SERVER(config)                                               \
	((ddhcp_epoll_data *)config->sockets[SKT_SERVER])
#define DDHCP_SKT_DHCP(config) ((ddhcp_epoll_data *)config->sockets[SKT_DHCP])
#define DDHCP_SKT_CONTROL(config)                                              \
	((ddhcp_epoll_data *)config->sockets[SKT_CONTROL])

// configuration and global state
struct ddhcp_config {
	ddhcp_node_id node_id;
	uint32_t number_of_blocks;
	uint16_t block_timeout;
	uint16_t block_refresh_factor;
	uint16_t block_needless_timeout;
	uint16_t tentative_timeout;
	uint8_t block_size;
	uint8_t spare_leases_needed;
	struct in_addr prefix;
	uint8_t prefix_len;
	uint8_t disable_dhcp;

	// Global Stuff
	time_t next_wakeup;
	uint32_t loop_timeout;
	uint8_t claiming_blocks_amount;
	uint8_t needless_marks;
	ddhcp_block *blocks;
	ddhcp_block_list claiming_blocks;

	// DHCP packets for later use.
	dhcp_packet_list dhcp_packet_cache;

	// DHCP Options
	dhcp_option_list options;

	// Network
	int epoll_fd;
	void *sockets[4];

	// Control
	int control_socket;
	char *control_path;
	int client_control_socket;

	// Hook
	char *hook_command;

	// DHCP
	uint16_t dhcp_port;

	// Statistics
#ifdef DDHCPD_STATISTICS
	long int statistics[STAT_NUM_OF_FIELDS];
#endif
};
typedef struct ddhcp_config ddhcp_config;

union in_addr_storage {
	struct in_addr in_addr;
	struct in6_addr in6_addr;
};

typedef union in_addr_storage in_addr_storage;

#endif
