#ifndef _DHCP_H
#define _DHCP_H
/**
 * DHCP Structures
 */

#include "types.h"
#include "dhcp_packet.h"

/**
 * DHCP Discover
 * Performs a search for a available, not already offered address in the
 * available block. When the block has no further available addresses 0 is returned,
 * otherwise the then reserved address. Will set a lease_timout on the lease.
 *
 * In a second step a dhcp_packet is created an send back.
 */
int dhcp_discover(int socket, dhcp_packet* discover, ddhcp_block* blocks, ddhcp_config* config);

/**
 * DHCP Request
 * Performs on base of de
 */
int dhcp_request(int socket, struct dhcp_packet* request, ddhcp_block* blocks, ddhcp_config* config);

int dhcp_nack(int socket, dhcp_packet* from_client);

/**
 * DHCP Lease Available
 * Determan iff there is a free lease in block.
 */
int dhcp_has_free(struct ddhcp_block* block);

/**
 * DHCP num Leases Available
 * Enumerate the free leases in a block
 */
int dhcp_num_free(struct ddhcp_block* block);

/**
 * Find first free lease in lease block and return its index.
 * This function asserts that there is a free lease, otherwise
 * it returns the value of block_subnet_len.
 */
uint32_t dhcp_get_free_lease(ddhcp_block* block);

/**
 * HouseKeeping: Check for timed out leases.
 */
void dhcp_check_timeouts(ddhcp_block* block);
#endif
