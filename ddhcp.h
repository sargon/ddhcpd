#ifndef _DDHCP_H
#define _DDHCP_H
/** 
 *  DDHCP Structs
 */

#include "list.h"

#include "dhcp.h"
#include "packet.h"

enum ddhcp_block_state { 
  DDHCP_FREE,
  DDHCP_TENTATIVE,
  DDHCP_CLAIMED,
  DDHCP_CLAIMING,
  DDHCP_OURS,
  DDHCP_BLOCKED
};

/** 
 * DDHCP BLOCK
 *
 * lease_block: Only when the block is ours then there should be an allocated
 *              dhcp_lease_block available, otherwise this field is NULL.
 */
struct ddhcp_block {
  uint32_t index;
  enum ddhcp_block_state state;
  uint32_t subnet;
  uint8_t  subnet_len;
  uint32_t address;
  time_t valid_until;
  uint8_t claiming_counts; 
  struct dhcp_lease_block* lease_block;
};
typedef struct ddhcp_block ddhcp_block;

struct ddhcp_block_list {
  struct ddhcp_block* block;
  struct list_head list;
};
typedef struct ddhcp_block_list ddhcp_block_list;

// This should be named state ...
struct ddhcp_config {
  uint16_t node_id;
  uint8_t number_of_blocks;
  uint8_t tentative_timeout;
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
  uint32_t mcast_scope_id;
  
};
typedef struct ddhcp_config ddhcp_config;
#endif
