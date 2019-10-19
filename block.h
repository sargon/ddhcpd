#ifndef _BLOCK_H
#define _BLOCK_H

#include "types.h"
#include "packet.h"

/**
 * Allocate block.
 * This will also malloc and prepare a dhcp_lease_block inside the given block.
 */
int block_alloc(ddhcp_block* block);

/**
 * Own a block, possibly after you have claimed it an amount of times.
 * This will also malloc and prepare a dhcp_lease_block inside the given block.
 */
ATTR_NONNULL(2) int block_own(ddhcp_block* block, ddhcp_config* config);

/**
 * Free a block and release dhcp_lease_block when allocated.
 */
ATTR_NONNULL_ALL void block_free(ddhcp_block* block);

/**
 * Find a free block and return it or otherwise NULL.
 * A block is called free, when no other node claims it.
 */
ATTR_NONNULL_ALL ddhcp_block* block_find_free(ddhcp_config* config);

/**
 * Claim a block! A block is only claimable when it is free.
 * Returns a value greater 0 if something goes sideways.
 */
ATTR_NONNULL_ALL int block_claim(int32_t num_blocks, ddhcp_config* config);

/**
 * Sum the number of free leases in blocks you own.
 */
ATTR_NONNULL_ALL uint32_t block_num_free_leases(ddhcp_config* config);

/**
 * Find and return claimed block with free leases. Try to
 * reduce fragmentation of lease usage by returning already
 * used blocks.
 */
ATTR_NONNULL_ALL ddhcp_block* block_find_free_leases(ddhcp_config* config);

/**
 * Drop the youngest unused block.
 * In this context drop is equivalent to forget our claim
 * and stop updating the claim. Freeing the block after its
 * timeout.
 */
ATTR_NONNULL_ALL void block_drop_unused(ddhcp_config* config);

/**
 *  Update the timeout of claimed blocks and send packets to
 *  distribute the continuations of that claim.
 *
 *  Due to fragmented timeouts this packet may send 2 times more packets
 *  than optimal. TODO fixthis
 */
ATTR_NONNULL_ALL void block_update_claims(ddhcp_config* config);

/**
 * Check the timeout of all blocks, and mark timed out once as FREE.
 * Blocks which are marked as BLOCKED are ignored in this process.
 */
ATTR_NONNULL_ALL void block_check_timeouts(ddhcp_config* config);

/**
 * Free block claim list structure.
 */
#define block_free_claims(config) \
  INIT_LIST_HEAD(&(config)->claiming_blocks);

/**
 * Show Block Status
 */
ATTR_NONNULL_ALL void block_show_status(int fd, ddhcp_config* config);

/**
 * Reset needless markers in all blocks
 */
void block_unmark_needless(ddhcp_config* config);

#endif
