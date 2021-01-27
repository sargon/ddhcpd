/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - Handling of decentral block negotiation
 *
 *  See AUTHORS file for copyright holders
 */

#ifndef _DDHCP_H
#define _DDHCP_H

#include "types.h"
#include "list.h"
#include "block.h"

/**
 * Initialise the blocks data structure in the global configuration state.
 * It should only be called by the main program.
 */
ATTR_NONNULL_ALL int ddhcp_block_init(ddhcp_config_t *config);
/**
 * This is the inverse function to ddhcp_block_init.
 */
ATTR_NONNULL_ALL void ddhcp_block_free(ddhcp_config_t *config);

/**
 * ddhcp_block_process is part of the network message processing chain.
 * Here messages related to the block consensus are processed.
 */
ATTR_NONNULL_ALL void ddhcp_block_process(uint8_t *buf, ssize_t len,
					  struct sockaddr_in6 sender,
					  ddhcp_config_t *config);
/**
 * A node sends a claim message for each block he thinks he owns, handling these
 * message is handled in ddhcp_block_process_claims.
 */
ATTR_NONNULL_ALL void
ddhcp_block_process_claims(struct ddhcp_mcast_packet *packet,
			   ddhcp_config_t *config);
/**
 * Before claiming a block, a node inquires that block three times, by sending
 * an inquire message. Handling these messages is done in ddhcp_block_process_inquire.
 */
ATTR_NONNULL_ALL void
ddhcp_block_process_inquire(struct ddhcp_mcast_packet *packet,
			    ddhcp_config_t *config);

/**
 * ddhcp_dhcp_process is part of the network message processing chain.
 * In this part of the chain dhcp packages which are forwarded between ddhcpd nodes
 * or related messages are processed.
 */
ATTR_NONNULL_ALL void ddhcp_dhcp_process(uint8_t *buf, ssize_t len,
					 struct sockaddr_in6 sender,
					 ddhcp_config_t *config);
/**
 * When a ddhcpd node gets a dhcp renew request for an address in a block that
 * is owned by another node, it requests a renew from the owning node. The
 * function ddhcp_dhcp_renewlease handles receiving such a request.
 */
ATTR_NONNULL_ALL void ddhcp_dhcp_renewlease(struct ddhcp_mcast_packet *packet,
					    ddhcp_config_t *config);
/**
 * After this node has send a renew request to another node. The other node
 * may send an acknowledge package. We handle receiving such a message
 * in ddhcp_dhcp_leaseack.
 */
ATTR_NONNULL_ALL void ddhcp_dhcp_leaseack(struct ddhcp_mcast_packet *packet,
					  ddhcp_config_t *config);
/**
 * After this node has send a renew request to another node. The other node
 * may send an no acknowledge package. We handle receiving such a message
 * in ddhcp_dhcp_leasenack.
 */
ATTR_NONNULL_ALL void ddhcp_dhcp_leasenak(struct ddhcp_mcast_packet *packet,
					  ddhcp_config_t *config);
/**
 * When a node receives a dhcp release packet for an address in a block owned
 * by another node, it should forward a release request to the owner.
 * The ddhcp_dhcp_release function handles processing of such a package.
 */
ATTR_NONNULL_ALL void ddhcp_dhcp_release(struct ddhcp_mcast_packet *packet,
					 ddhcp_config_t *config);

ATTR_NONNULL_ALL ddhcp_block_t *block_find_lease(ddhcp_config_t *config);

ATTR_NONNULL_ALL void house_keeping(ddhcp_config_t *config);

#endif
