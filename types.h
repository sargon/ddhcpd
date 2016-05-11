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
  struct dhcp_lease_block* lease_block;
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

struct dhcp_lease_block {
  struct in_addr subnet;
  uint32_t subnet_len;  
  struct dhcp_lease* addresses;
};
typedef struct dhcp_lease_block dhcp_lease_block;

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
  unsigned int claiming_blocks_amount;
  ddhcp_block_list claiming_blocks;

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
