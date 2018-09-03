#ifndef _TYPES_H
#define _TYPES_H

#include <arpa/inet.h>
#include <time.h>

#include "list.h"
#include "dhcp_packet.h"

#define NODE_ID_CMP(id1,id2) memcmp((char*) (id1), (char*) (id2), sizeof(ddhcp_node_id))

// node ident

typedef uint8_t ddhcp_node_id[8];
#define NODE_ID_CLEAR(id) memset(id,'\0',sizeof(ddhcp_node_id))
#define NODE_ID_CP(dest,src) memcpy(dest,src,sizeof(ddhcp_node_id))

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
  uint8_t  subnet_len;
  uint8_t claiming_counts;
  ddhcp_node_id node_id;
  struct in6_addr owner_address;
  time_t timeout;
  // Only iff state is equal to CLAIMED lease_block is not equal to NULL.
  struct dhcp_lease* addresses;

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
  uint8_t* payload;

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


// state

// TODO Rename to state
struct ddhcp_config {
  ddhcp_node_id node_id;
  uint32_t number_of_blocks;
  uint16_t block_timeout;
  uint16_t block_refresh_factor;
  uint16_t tentative_timeout;
  uint8_t block_size;
  uint8_t spare_blocks_needed;
  struct in_addr prefix;
  uint8_t prefix_len;
  uint8_t disable_dhcp;

  // Global Stuff
  time_t next_wakeup;
  uint32_t loop_timeout;
  unsigned int claiming_blocks_amount;
  ddhcp_block* blocks;
  ddhcp_block_list claiming_blocks;

  // DHCP packets for later use.
  dhcp_packet_list dhcp_packet_cache;

  // DHCP Options
  dhcp_option_list options;

  // Network
  int mcast_socket;
  int server_socket;
  int client_socket;
  uint32_t mcast_scope_id;
  uint32_t server_scope_id;
  uint32_t client_scope_id;

  // Control
  int control_socket;
  char* control_path;
  int client_control_socket;

  // Hook
  char* hook_command;

  // DHCP
  uint16_t dhcp_port;
};
typedef struct ddhcp_config ddhcp_config;

#endif
