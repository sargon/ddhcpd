/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - DHCP server engine
 *
 *  See AUTHORS file for copyright holders
 */

#ifndef _DDHCP_DHCP_H
#define _DDHCP_DHCP_H
/**
 * DHCP Structures
 */

#include "types.h"
#include "dhcp_packet.h"

/**
 * DHCP Process Packet
 */
ATTR_NONNULL_ALL int dhcp_process(uint8_t *buf, ssize_t len,
				  ddhcp_config *config);

/**
 * DHCP Discover
 * Performs a search for a available, not already offered address in the
 * available block. When the block has no further available addresses 0 is returned,
 * otherwise the then reserved address. Will set a lease_timout on the lease.
 *
 * In a second step a dhcp_packet is created an send back.
 */
ATTR_NONNULL_ALL int dhcp_handle_discover(int socket, dhcp_packet_t *discover,
					  ddhcp_config *config);

/**
 * DHCP Request
 * Performs on base of de
 */
ATTR_NONNULL_ALL int dhcp_handle_request(int socket, dhcp_packet_t *request,
					 ddhcp_config *config);

/**
 * DDHCP Remote Request (Renew)
 */
ATTR_NONNULL_ALL int dhcp_rhdl_request(uint32_t *address, ddhcp_config *config);
/**
 * DDHCP Remote Answer (Ack)
 */
ATTR_NONNULL_ALL int dhcp_rhdl_ack(int socket, dhcp_packet_t *request,
				   ddhcp_config *config);

/**
 * DHCP Release
 */
ATTR_NONNULL_ALL void dhcp_handle_release(dhcp_packet_t *packet,
					  ddhcp_config *config);

/**
 * DHCP Inform
 */
ATTR_NONNULL_ALL void dhcp_handle_inform(int socket, dhcp_packet_t *packet,
					 ddhcp_config *config);

ATTR_NONNULL_ALL int dhcp_nack(int socket, dhcp_packet_t *from_client,
			       ddhcp_config *config);
ATTR_NONNULL_ALL int dhcp_ack(int socket, dhcp_packet_t *request,
			      ddhcp_block_t *lease_block, uint32_t lease_index,
			      ddhcp_config *config);

/**
 * DHCP Lease Available
 * Determan iff there is a free lease in block.
 */
ATTR_NONNULL_ALL int dhcp_has_free(ddhcp_block_t *block);

/**
 * DHCP num Leases Available
 * Enumerate the free leases in a block
 */
ATTR_NONNULL_ALL uint32_t dhcp_num_free(ddhcp_block_t *block);

/**
 * Number of leases offered to clients.
 */
ATTR_NONNULL_ALL uint32_t dhcp_num_offered(ddhcp_block_t *block);

/**
 * Find first free lease in lease block and return its index.
 * This function asserts that there is a free lease, otherwise
 * it returns the value of block_subnet_len.
 */
ATTR_NONNULL_ALL uint32_t dhcp_get_free_lease(ddhcp_block_t *block);

/**
 * Find lease for given address and mark it as free.
 * When no address is found no return value is given,
 * since there is no reply to a dhcp release packet
 * no further internal handling is needed.
 */
ATTR_NONNULL_ALL void dhcp_release_lease(uint32_t address,
					 ddhcp_config *config);

/**
 * HouseKeeping: Check for timed out leases.
 * Return the number of free leases in the block.
 */
ATTR_NONNULL_ALL int dhcp_check_timeouts(ddhcp_block_t *block);

#endif
