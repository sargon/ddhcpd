/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - Raw packet handling
 *
 *  See AUTHORS file for copyright holders
 */

#include <endian.h>
#include <assert.h>
#include <errno.h>

#include "packet.h"
#include "logger.h"
#include "netsock.h"

extern const struct in6_addr in6addr_localmcast;

#define copy_buf_to_var_inc(buf, type, var)                                    \
	do {                                                                   \
		type *tmp = &(var);                                            \
		memcpy(tmp, (buf), sizeof(type));                              \
		buf = (__typeof__(buf))((char *)(buf) + sizeof(type));         \
	} while (0);

#define copy_var_to_buf_inc(buf, type, var)                                    \
	do {                                                                   \
		type *tmp = &(var);                                            \
		memcpy((buf), tmp, sizeof(type));                              \
		buf = (__typeof__(buf))((char *)(buf) + sizeof(type));         \
	} while (0);

ssize_t _packet_size(uint8_t command, ssize_t payload_count)
{
	ssize_t len = 0;

	switch (command) {
	case DDHCP_MSG_UPDATECLAIM:
		len = 16 + payload_count * 7;
		break;
	case DDHCP_MSG_INQUIRE:
		len = 16 + payload_count * 4;
		break;
	case DDHCP_MSG_LEASEACK:
	case DDHCP_MSG_LEASENAK:
	case DDHCP_MSG_RENEWLEASE:
		len = 16 + sizeof(struct ddhcp_renew_payload);
		break;
	default:
		WARNING("_packet_size(...): Unknown command: %i/%li\n", command,
			payload_count);
		return -1;
		break;
	}

	if (!len)
		ERROR("_packet_size(%i,%li): calculated zero length!", command,
		      payload_count);

	return len;
}

ATTR_NONNULL_ALL struct ddhcp_mcast_packet *
new_ddhcp_packet(uint8_t command, ddhcp_config_t *config)
{
	struct ddhcp_mcast_packet *packet = (struct ddhcp_mcast_packet *)calloc(
		sizeof(struct ddhcp_mcast_packet), 1);

	if (!packet)
		return NULL;

	memcpy(&packet->node_id, config->node_id, 8);
	memcpy(&packet->prefix, &config->prefix, sizeof(struct in_addr));

	packet->prefix_len = config->prefix_len;
	packet->blocksize = config->block_size;
	packet->command = command;
	packet->sender = NULL;

	return packet;
}

ATTR_NONNULL_ALL ssize_t ntoh_mcast_packet(uint8_t *buf, ssize_t len,
					   struct ddhcp_mcast_packet *packet)
{
	/* Header */
	copy_buf_to_var_inc(buf, ddhcp_node_id, packet->node_id);

	/* The Python implementation prefixes with a node number?
	 * prefix address
	 */
	copy_buf_to_var_inc(buf, struct in_addr, packet->prefix);

	/* prefix length */
	copy_buf_to_var_inc(buf, uint8_t, packet->prefix_len);
	/* size of a block */
	copy_buf_to_var_inc(buf, uint8_t, packet->blocksize);
	/* the command */
	copy_buf_to_var_inc(buf, uint8_t, packet->command);
	/* count of payload entries */
	copy_buf_to_var_inc(buf, uint8_t, packet->count);

	ssize_t should_len = _packet_size(packet->command, packet->count);

	if (should_len != len) {
		WARNING("ntoh_mcast_packet(...): Calculated length differs from packet length: Got %li, expected %li",
			len, should_len);
		return 1;
	}

	char str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(packet->prefix), str, INET_ADDRSTRLEN);
	DEBUG("NODE: %lu PREFIX: %s/%i BLOCKSIZE: %i COMMAND: %i ALLOCATIONS: %i\n",
	      (long unsigned int)packet->node_id, str, packet->prefix_len,
	      packet->blocksize, packet->command, packet->count);

	/* Payload */
	uint8_t tmp8;
	uint16_t tmp16;
	uint32_t tmp32;
	struct ddhcp_payload *payload;

	switch (packet->command) {
	/* UpdateClaim */
	case DDHCP_MSG_UPDATECLAIM:
		packet->payload = (struct ddhcp_payload *)calloc(
			sizeof(struct ddhcp_payload), packet->count);
		payload = packet->payload;

		if (payload == NULL) {
			WARNING("ntoh_mcast_packet(...): Failed to allocate packet payload\n");
			return -ENOMEM;
		}

		for (int i = 0; i < packet->count; i++) {
			copy_buf_to_var_inc(buf, uint32_t, tmp32);
			payload->block_index = ntohl(tmp32);

			copy_buf_to_var_inc(buf, uint16_t, tmp16);
			payload->timeout = ntohs(tmp16);

			copy_buf_to_var_inc(buf, uint8_t, tmp8);
			payload->reserved = tmp8;

			payload++;
		}

		break;
	case DDHCP_MSG_INQUIRE: /* inquire block */
		packet->payload = (struct ddhcp_payload *)calloc(
			sizeof(struct ddhcp_payload), packet->count);
		payload = packet->payload;

		if (payload == NULL) {
			WARNING("ntoh_mcast_packet(...): Failed to allocate packet payload\n");
			return -ENOMEM;
		}

		for (int i = 0; i < packet->count; i++) {
			copy_buf_to_var_inc(buf, uint32_t, tmp32);
			payload->block_index = ntohl(tmp32);

			payload++;
		}

		break;
	case DDHCP_MSG_RENEWLEASE: /* ReNEWLease */
	case DDHCP_MSG_LEASEACK:
	case DDHCP_MSG_LEASENAK:
	case DDHCP_MSG_RELEASE:
		packet->renew_payload = (struct ddhcp_renew_payload *)calloc(
			sizeof(struct ddhcp_renew_payload), 1);

		if (packet->renew_payload == NULL) {
			WARNING("ntoh_mcast_packet(...): Failed to allocate packet payload\n");
			return -ENOMEM;
		}

		copy_buf_to_var_inc(buf, uint32_t, tmp32);
		packet->renew_payload->address = ntohl(tmp32);
		copy_buf_to_var_inc(buf, uint32_t, tmp32);
		packet->renew_payload->xid = ntohl(tmp32);
		copy_buf_to_var_inc(buf, uint32_t, tmp32);
		packet->renew_payload->lease_seconds = ntohl(tmp32);
		memcpy(&packet->renew_payload->chaddr, buf, 16);
		break;
	default:
		DEBUG("noth_mcast_packet(...): Unknown packet type\n");
		return 2;
	}

	return 0;
}

ATTR_NONNULL_ALL int hton_packet(struct ddhcp_mcast_packet *packet, char *buf)
{
	struct ddhcp_payload *payload;
	uint32_t tmp32;
	uint16_t tmp16;
	uint8_t tmp8;

	/* Header */
	copy_var_to_buf_inc(buf, ddhcp_node_id, packet->node_id);

	/* The Python implementation prefixes with a node number?
	 * prefix address
	 */
	copy_var_to_buf_inc(buf, struct in_addr, packet->prefix);

	/* prefix length */
	copy_var_to_buf_inc(buf, uint8_t, packet->prefix_len);
	/* size of a block */
	copy_var_to_buf_inc(buf, uint8_t, packet->blocksize);
	/* the command */
	copy_var_to_buf_inc(buf, uint8_t, packet->command);
	/* count of payload entries */
	copy_var_to_buf_inc(buf, uint8_t, packet->count);

	switch (packet->command) {
	case DDHCP_MSG_UPDATECLAIM:
		payload = packet->payload;

		for (unsigned int index = 0; index < packet->count; index++) {
			tmp32 = htonl(payload->block_index);
			copy_var_to_buf_inc(buf, uint32_t, tmp32);

			tmp16 = htons(payload->timeout);
			copy_var_to_buf_inc(buf, uint16_t, tmp16);

			tmp8 = (uint8_t)payload->reserved;
			copy_var_to_buf_inc(buf, uint8_t, tmp8);

			payload++;
		}

		break;
	case DDHCP_MSG_INQUIRE:
		payload = packet->payload;

		for (unsigned int index = 0; index < packet->count; index++) {
			tmp32 = htonl(payload->block_index);
			copy_var_to_buf_inc(buf, uint32_t, tmp32);

			payload++;
		}

		break;
	case DDHCP_MSG_LEASEACK:
	case DDHCP_MSG_LEASENAK:
	case DDHCP_MSG_RELEASE:
	case DDHCP_MSG_RENEWLEASE:
		tmp32 = htonl(packet->renew_payload->address);
		copy_var_to_buf_inc(buf, uint32_t, tmp32);
		tmp32 = htonl(packet->renew_payload->xid);
		copy_var_to_buf_inc(buf, uint32_t, tmp32);
		tmp32 = htonl(packet->renew_payload->lease_seconds);
		copy_var_to_buf_inc(buf, uint32_t, tmp32);
		memcpy(buf, &packet->renew_payload->chaddr, 16);
		break;

	default:
		DEBUG("hton_packet(...): Unknown packet type: %i\n",
		      packet->command);
		break;
	}

	return 0;
}

ATTR_NONNULL_ALL ssize_t send_packet_mcast(struct ddhcp_mcast_packet *packet,
					   ddhcp_epoll_data *data)
{
	size_t len = (size_t)_packet_size(packet->command, packet->count);
	char *buf = (char *)calloc(1, len);

	if (buf == NULL)
		return -1;

	errno = 0;

	hton_packet(packet, buf);

	struct sockaddr_in6 dest_addr = { .sin6_family = AF_INET6,
					  .sin6_port =
						  htons(DDHCP_MULTICAST_PORT),
					  .sin6_scope_id = data->interface_id };

	memcpy(&dest_addr.sin6_addr, &in6addr_localmcast,
	       sizeof(in6addr_localmcast));

	ssize_t bytes_send =
		sendto(data->fd, buf, len, 0, (struct sockaddr *)&dest_addr,
		       sizeof(dest_addr));

	free(buf);

	if (bytes_send < 0)
		ERROR("send_packet_mcast(...): Failed (%i): %s\n", errno,
		      strerror(errno));

	return bytes_send;
}

ATTR_NONNULL_ALL ssize_t send_packet_direct(struct ddhcp_mcast_packet *packet,
					    struct in6_addr *dest,
					    ddhcp_epoll_data *data)
{
	DEBUG("send_packet_direct(packet,dest,mcsocket:%i,scope:%u)\n",
	      data->fd, data->interface_id);
	size_t len = (size_t)_packet_size(packet->command, packet->count);

	char *buf = (char *)calloc(1, len);

	if (buf == NULL) {
		ERROR("send_packet_direct(...): Failed to allocate send buf\n");
		return -1;
	}

	struct sockaddr_in6 dest_addr = { .sin6_family = AF_INET6,
					  .sin6_port =
						  htons(DDHCP_UNICAST_PORT),
					  .sin6_scope_id = data->interface_id };

	memcpy(&dest_addr.sin6_addr, dest, sizeof(struct in6_addr));

#if LOG_LEVEL_LIMIT >= LOG_DEBUG
	char ipv6_sender[INET6_ADDRSTRLEN];

	DEBUG("Sending message to %s\n",
	      inet_ntop(AF_INET6, dest, ipv6_sender, INET6_ADDRSTRLEN));

#endif

	hton_packet(packet, buf);

	ssize_t bytes_send =
		sendto(data->fd, buf, len, 0, (struct sockaddr *)&dest_addr,
		       sizeof(struct sockaddr_in6));

	free(buf);

	if (bytes_send < 0) {
		ERROR("send_packet_direct(...): Failed (%i): %s\n", errno,
		      strerror(errno));
	}

	return bytes_send;
}
