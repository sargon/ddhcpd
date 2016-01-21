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
  struct in_addr subnet;
  uint8_t  subnet_len;
  uint32_t address;
  time_t timeout;
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
  uint64_t node_id;
  uint8_t number_of_blocks;
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

int ddhcp_block_init(struct ddhcp_block **blocks, ddhcp_config *config);
void ddhcp_block_process_claims( struct ddhcp_block *blocks , struct ddhcp_mcast_packet *packet ,ddhcp_config *config) ;
void ddhcp_block_process_inquire( struct ddhcp_block *blocks , struct ddhcp_mcast_packet *packet ,ddhcp_config *config);
ddhcp_block* block_find_lease( ddhcp_block *blocks , ddhcp_config *config);
int block_own( ddhcp_block *block );
int block_claim( ddhcp_block *blocks, int num_blocks , ddhcp_config *config );
int block_num_free_leases( ddhcp_block *block, ddhcp_config *config );
void block_update_claims( ddhcp_block *blocks, ddhcp_config *config );
void house_keeping( ddhcp_block *blocks, ddhcp_config *config );

#endif
