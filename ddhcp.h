/** 
 *  DDHCP Structs
 */

#include "dhcp.h"

enum ddhcp_block_state { 
  DDHCP_FREE,
  DDHCP_TENTATIVE,
  DDHCP_CLAIMED,
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
  uint32_t valid_until;
  struct dhcp_lease_block* lease_block;
};

