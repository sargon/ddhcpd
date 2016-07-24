#ifndef _TYPES_H
#define _TYPES_H

#include <arpa/inet.h>
#include <time.h>

#include "list.h"

// block structures

enum ddhcp_block_state {
  DDHCP_FREE,
  DDHCP_TENTATIVE,
  DDHCP_CLAIMED,
  DDHCP_CLAIMING,
  DDHCP_OURS,
  DDHCP_BLOCKED
};

struct ddhcp_block {
  uint32_t index;
  enum ddhcp_block_state state;
  struct in_addr subnet;
  uint8_t  subnet_len;
  uint32_t address;
  time_t timeout;
  uint8_t claiming_counts;
  // Only iff state is equal to CLAIMED lease_block is not equal to NULL.
  struct dhcp_lease* addresses;
};
typedef struct ddhcp_block ddhcp_block;

struct ddhcp_block_list {
  struct ddhcp_block* block;
  struct list_head list;
};
typedef struct ddhcp_block_list ddhcp_block_list;

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
  uint32_t lease_end;
};
typedef struct dhcp_lease dhcp_lease;

struct dhcp_option {
  uint8_t code;
  uint8_t len;
  uint8_t *payload;
};
typedef struct dhcp_option dhcp_option;

struct dhcp_option_list {
  struct dhcp_option *option;
  struct list_head list;
};
typedef struct dhcp_option_list dhcp_option_list;

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

// state

// TODO Rename to state
struct ddhcp_config {
  uint64_t node_id;
  uint32_t number_of_blocks;
  uint16_t block_timeout;
  uint16_t tentative_timeout;
  uint8_t block_size;
  uint8_t spare_blocks_needed;
  struct in_addr prefix;
  uint8_t prefix_len;

  // Global Stuff
  time_t next_wakeup;
  uint32_t loop_timeout;
  unsigned int claiming_blocks_amount;
  ddhcp_block_list claiming_blocks;


  // DHCP Options
  dhcp_option_list options;

  // Network
  int mcast_socket;
  int client_socket;
  uint32_t mcast_scope_id;
  uint32_t client_scope_id;

  // DHCP
  uint16_t dhcp_port;
};
typedef struct ddhcp_config ddhcp_config;

#endif
