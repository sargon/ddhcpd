#ifndef _DHCP_H
#define _DHCP_H
/**
 * DHCP Structures
 */

#include "types.h"
#include "dhcp_packet.h"

#define DHCP_LEASE_TIME 60;

/**
 * DHCP Discover
 * Performs a search for a available, not already offered address in the
 * available block. When the block has no further available addresses 0 is returned,
 * otherwise the then reserved address. Will set a lease_timout on the lease.
 *
 * In a second step a dhcp_packet is created an send back.
 */
int dhcp_hdl_discover(int socket, dhcp_packet* discover, ddhcp_block* blocks, ddhcp_config* config);

/**
 * DHCP Request
 * Performs on base of de
 */
int dhcp_hdl_request(int socket, struct dhcp_packet* request, ddhcp_block* blocks, ddhcp_config* config);

/**
 * DDHCP Remote Request (Renew)
 */
int dhcp_rhdl_request(uint32_t* address, ddhcp_block* blocks, ddhcp_config* config);
/**
 * DDHCP Remote Answer (Ack)
 */
int dhcp_rhdl_ack(int socket, struct dhcp_packet* request, ddhcp_block* blocks, ddhcp_config* config);

/**
 * DHCP Release
 */
void dhcp_hdl_release(dhcp_packet* packet, ddhcp_block* block, ddhcp_config* config);

int dhcp_nack(int socket, dhcp_packet* from_client);
int dhcp_ack(int socket, dhcp_packet* request, ddhcp_block* lease_block, uint32_t lease_index, ddhcp_config* config);

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
 * Find lease for given address and mark it as free.
 * When no address is found no return value is given,
 * since there is no reply to a dhcp release packet
 * no further internal handling is needed.
 */
void dhcp_release_lease(uint32_t address, ddhcp_block* blocks, ddhcp_config* config);

/**
 * HouseKeeping: Check for timed out leases.
 * Return the number of free leases in the block.
 */
int dhcp_check_timeouts(ddhcp_block* block);

#endif
