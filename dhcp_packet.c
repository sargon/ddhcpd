/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - DHCP packet processing facility
 *
 *  See AUTHORS file for copyright holders
 */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "types.h"
#include "logger.h"

struct sockaddr_in broadcast = {
	.sin_family = AF_INET,
	.sin_addr = { INADDR_BROADCAST },
};

struct sockaddr_in unicast = {
	.sin_family = AF_INET,
	.sin_addr = { 0 },
};

#if LOG_LEVEL_LIMIT >= LOG_DEBUG
ATTR_NONNULL_ALL void printf_dhcp(dhcp_packet_t *packet)
{
	char tmpstr[INET_ADDRSTRLEN];

	inet_ntop(AF_INET, &(packet->ciaddr.s_addr), tmpstr, INET_ADDRSTRLEN);
	DEBUG("BOOTP [ op %i, htype %i, hlen %i, hops %i, xid %lu, secs %i, flags %i, ciaddr %s, ",
	      packet->op, packet->htype, packet->hlen, packet->hops,
	      (unsigned long)packet->xid, packet->secs, packet->flags, tmpstr);

	inet_ntop(AF_INET, &(packet->yiaddr.s_addr), tmpstr, INET_ADDRSTRLEN);
	DEBUG("yiaddr %s, ", tmpstr);

	inet_ntop(AF_INET, &(packet->siaddr.s_addr), tmpstr, INET_ADDRSTRLEN);
	DEBUG("siaddr %s, ", tmpstr);

	inet_ntop(AF_INET, &(packet->giaddr.s_addr), tmpstr, INET_ADDRSTRLEN);
	DEBUG("giaddr %s, sname: %s, file: %s ]\n", tmpstr, packet->sname,
	      packet->file);

	dhcp_option *option = packet->options;

	for (int i = 0; i < packet->options_len; i++) {
		if (option->len == 1) {
			DEBUG("DHCP OPTION [ code %i, length %i, value %i ]\n",
			      option->code, option->len, option->payload[0]);
		} else if (option->code == DHCP_CODE_PARAMETER_REQUEST_LIST) {
			DEBUG("DHCP OPTION [ code %i, length %i, value ",
			      option->code, option->len);

			for (int k = 0; k < option->len; k++)
				LOG("%i ", option->payload[k]);

			LOG("]\n");
		} else {
			DEBUG("DHCP OPTION [ code %i, length %i ]\n",
			      option->code, option->len);
		}

		option++;
	}
}
#else
#define printf_dhcp(packet)                                                    \
	{                                                                      \
	}
#endif

ATTR_NONNULL_ALL size_t _dhcp_packet_len(dhcp_packet_t *packet)
{
	size_t len = 240 + 1;
	dhcp_option *option = packet->options;

	for (int i = 0; i < packet->options_len; i++) {
		switch (option->code) {
		case DHCP_CODE_PAD:
		case DHCP_CODE_END:
			len++;
			break;
		default:
			len += 2 + (size_t)option->len;
			break;
		}

		option++;
	}

	return len;
}

ATTR_NONNULL_ALL ssize_t ntoh_dhcp_packet(dhcp_packet_t *packet, uint8_t *buf,
					  ssize_t len)
{
	uint16_t tmp16;
	uint32_t tmp32;

	if (len < 240)
		return -1;

	DEBUG("ntoh_dhcp_packet(...): package len:%zi\n", len);

	/* TODO Use macros to read from the buffer */

	packet->op = buf[0];
	packet->htype = buf[1];
	packet->hlen = buf[2];
	packet->hops = buf[3];
	memcpy(&tmp32, buf + 4, 4);
	packet->xid = ntohl(tmp32);
	memcpy(&tmp16, buf + 8, 2);
	packet->secs = ntohs(tmp16);
	memcpy(&tmp16, buf + 10, 2);
	packet->flags = ntohs(tmp16);
	memcpy(&packet->ciaddr, buf + 12, 4);
	memcpy(&packet->yiaddr, buf + 16, 4);
	memcpy(&packet->siaddr, buf + 20, 4);
	memcpy(&packet->giaddr, buf + 24, 4);
	memcpy(&packet->chaddr, buf + 28, 16);
	memcpy(&packet->sname, buf + 44, 64);
	memcpy(&packet->file, buf + 108, 128);

	/* Check for the magic cookie */
	if (!((uint8_t)buf[236] == 99 && (uint8_t)buf[237] == 130 &&
	      (uint8_t)buf[238] == 83 && (uint8_t)buf[239] == 99)) {
		WARNING("ntoh_dhcp_packet(...) -> Magic cookie not found, possibly malformed request!\n");
		return -7;
	}

	uint8_t *option = buf + 236 + 4;

	/* Count options */
	size_t options = 0;
	int exit = 0;
	int dhcp_message_type = 0;
	int dhcp_request_list = 0;

	while (option < buf + len && exit == 0) {
		switch ((uint8_t)option[0]) {
		case DHCP_CODE_PAD:
			/* JUMP the padding */
			option += 1;
			continue;
			break;
		case DHCP_CODE_END:
			options++;
			exit = 1;
			continue;
			break;
		case DHCP_CODE_MESSAGE_TYPE:
			dhcp_message_type = 1;
			break;
		case DHCP_CODE_PARAMETER_REQUEST_LIST:
			dhcp_request_list = 1;
			break;
		default:
			break;
		}

		if (option + 1 > buf + len) {
			WARNING("ntoh_dhcp_packet(...): DHCP options ended improperly, possible broken client.\n");
			return -4;
		}

		if (option + option[1] + 2 > buf + len) {
			/* Error: Malformed dhcp options */
			WARNING("ntoh_dhcp_packet(...): DHCP options smaller than len of last option suggests, possible broken client.\n");
			return -5;
		} else {
			option += (uint8_t)option[1] + 2;
			options++;
		}
	}

	if (dhcp_message_type != 1) {
		INFO("ntoh_dhcp_packet(...): Message contains no message type - invalid!\n");
		return -6;
	}

	if (dhcp_request_list != 1)
		DEBUG("ntoh_dhcp_packet(...): Message contains no DHCP request list - broken client?\n");

	packet->options_len = (uint8_t)options;
	packet->options = (dhcp_option *)calloc(options, sizeof(dhcp_option));

	if (!packet->options) {
		WARNING("ntoh_dhcp_packet(...): option memory allocation failed.\n");
		return -ENOMEM;
	}

	option = buf + 236 + 4;

	exit = 0;
	uint8_t i = 0;

	while (option < buf + len && exit == 0) {
		switch ((uint8_t)option[0]) {
		case DHCP_CODE_END:
			exit = 1;
		case DHCP_CODE_PAD:
			/* JUMP padding and end */
			packet->options[i].code = option[0];
			packet->options[i].len = 0;
			packet->options[i].payload = NULL;

			option += 1;
			i++;
			break;
		default:
			packet->options[i].code = option[0];
			packet->options[i].len = option[1];
			packet->options[i].payload = option + 2;

			option += (uint8_t)option[1] + 2;
			i++;
			break;
		}
	}

	assert(i == options);

#if LOG_LEVEL_LIMIT >= LOG_INFO
	printf_dhcp(packet);
#endif

	return 0;
}

ATTR_NONNULL_ALL ssize_t dhcp_packet_send(int socket, dhcp_packet_t *packet)
{
	uint8_t *buf = calloc(sizeof(char), _dhcp_packet_len(packet));
	DEBUG("dhcp_packet_send(socket:%i, dhcp_packet)\n", socket);
	uint16_t tmp16;
	uint32_t tmp32;

	if (!buf)
		return -ENOMEM;

	/* Header */
	buf[0] = packet->op;
	buf[1] = packet->htype;
	buf[2] = packet->hlen;
	buf[3] = packet->hops;
	tmp32 = htonl(packet->xid);
	memcpy(buf + 4, &tmp32, 4);
	tmp16 = htons(packet->secs);
	memcpy(buf + 8, &tmp16, 2);
	tmp16 = htons(packet->flags);
	memcpy(buf + 10, &tmp16, 2);
	memcpy(buf + 12, &packet->ciaddr, 4);
	memcpy(buf + 16, &packet->yiaddr, 4);
	memcpy(buf + 20, &packet->siaddr, 4);
	memcpy(buf + 24, &packet->giaddr, 4);
	memcpy(buf + 28, &packet->chaddr, 16);
	memcpy(buf + 44, &packet->sname, 64);
	memcpy(buf + 108, &packet->file, 128);

	/* Magic Cookie */
	buf[236] = 99;
	buf[237] = 130;
	buf[238] = 83;
	buf[239] = 99;

	/* Options */
	uint8_t *obuf = buf + 240;
	dhcp_option *option = packet->options;

	for (int i = 0; i < packet->options_len; i++) {
		obuf[0] = option->code;

		switch (option->code) {
		case DHCP_CODE_PAD:
		case DHCP_CODE_END:
			obuf++;
			break;

		default:
			obuf[1] = option->len;
			memcpy(obuf + 2, option->payload, option->len);
			obuf = obuf + 2 + option->len;
			break;
		}

		option++;
	}

	buf[_dhcp_packet_len(packet) - 1] = 255;
	assert(obuf + 1 == buf + _dhcp_packet_len(packet));
	/* Network send */
	printf("Message LEN: %zu\n", _dhcp_packet_len(packet));

	struct sockaddr_in *address = &broadcast;
	/* Check the broadcast flag */
	if (!(packet->flags & DHCP_BROADCAST_MASK)) {
		/* Check if client address is set to none zero */
		uint8_t zeros[4] = { 0, 0, 0, 0 };
		if (memcmp(zeros, &packet->ciaddr, 4) != 0) {
#if LOG_LEVEL_LIMIT >= LOG_DEBUG
			char ipv4_sender[INET_ADDRSTRLEN];
			DEBUG("dhcp_packet_send: Sending unicast to %s \n",
			      inet_ntop(AF_INET, &packet->ciaddr, ipv4_sender,
					INET_ADDRSTRLEN));
#endif
			address = &unicast;
			address->sin_addr = packet->ciaddr;
		}
	}
	address->sin_port = htons(68);

	ssize_t bytes_send =
		sendto(socket, buf, _dhcp_packet_len(packet), 0,
		       (struct sockaddr *)address, sizeof(broadcast));

	if (bytes_send < 0) {
		ERROR("dhcp_packet_send(...): Failed (%i): %s\n", errno,
		      strerror(errno));
	}

	free(buf);

	return bytes_send;
}

ATTR_NONNULL_ALL int dhcp_packet_copy(dhcp_packet_t *dest, dhcp_packet_t *src)
{
	int err;

	memcpy(dest, src, sizeof(dhcp_packet_t));
	dest->options = (struct dhcp_option *)calloc(
		src->options_len, sizeof(struct dhcp_option));

	if (!dest->options)
		return 1;

	dhcp_option *src_option = src->options;
	dhcp_option *dest_option = dest->options;

	for (; src_option < src->options + src->options_len; src_option++) {
		uint8_t *dest_payload =
			(uint8_t *)calloc(src_option->len, sizeof(uint8_t));

		if (!dest_payload) {
			err = -ENOMEM;
			goto fail_options;
		}

		memcpy(dest_payload, src_option->payload, src_option->len);
		dest_option->code = src_option->code;
		dest_option->len = src_option->len;
		dest_option->payload = dest_payload;
		dest_option++;
	}

	return 0;

fail_options:

	while (dest_option-- > dest->options) {
		free(dest_option->payload);
	}

	free(dest->options);
	return err;
}

ATTR_NONNULL_ALL int dhcp_packet_list_add(dhcp_packet_list *list,
					  dhcp_packet_t *packet)
{
	time_t now = time(NULL);
	/* Save dhcp packet, for further actions, later. */
	dhcp_packet_t *copy = calloc(1, sizeof(dhcp_packet_t));

	if (!copy) {
		ERROR("dhcp_packet_list_add(...): Unable to allocate memory\n");
		return 1;
	}

	dhcp_packet_copy(copy, packet);
	copy->timeout = now + 120;
	list_add_tail(&copy->packet_list, list);

	return 0;
}

ATTR_NONNULL_ALL dhcp_packet_t *
dhcp_packet_list_find(dhcp_packet_list *list, uint32_t xid, uint8_t *chaddr)
{
	DEBUG("dhcp_packet_list_find(list,xid:%u,chaddr)\n", xid);
	struct list_head *pos, *q;

	list_for_each_safe (pos, q, list) {
		dhcp_packet_t *packet =
			list_entry(pos, dhcp_packet_t, packet_list);

		if (packet->xid == xid) {
			if (memcmp(packet->chaddr, chaddr, 16) == 0) {
				DEBUG("dhcp_packet_list_find(...): packet found\n");
				list_del(pos);
				return packet;
			}
		} else {
			DEBUG("dhcp_packet_list_find(...): Packet (%u)\n",
			      packet->xid);
		}
	}

	DEBUG("dhcp_packet_list_find(...): No matching packet found\n");

	return NULL;
}

ATTR_NONNULL_ALL void dhcp_packet_list_free(dhcp_packet_list *list)
{
	DEBUG("dhcp_packet_list_free(list)\n");
	struct list_head *pos, *q;

	list_for_each_safe (pos, q, list) {
		dhcp_packet_t *packet =
			list_entry(pos, dhcp_packet_t, packet_list);
		list_del(pos);
		dhcp_packet_free(packet, 1);
		free(packet);
	}
}

ATTR_NONNULL_ALL uint8_t dhcp_packet_message_type(dhcp_packet_t *packet)
{
	dhcp_option *option = packet->options;

	for (int i = 0; i < packet->options_len; i++) {
		if (option->code == DHCP_CODE_MESSAGE_TYPE) {
			return (uint8_t)option->payload[0];
		}

		option++;
	}

	return 0;
}

ATTR_NONNULL_ALL void dhcp_packet_list_timeout(dhcp_packet_list *list)
{
	DEBUG("dhcp_packet_list_timeout(list)\n");
	struct list_head *pos, *q;
	time_t now = time(NULL);

	list_for_each_safe (pos, q, list) {
		dhcp_packet_t *packet =
			list_entry(pos, dhcp_packet_t, packet_list);

		if (packet->timeout < now) {
			list_del(pos);
			dhcp_packet_free(packet, 1);
			DEBUG("dhcp_packet_t_list_timeout(...): drop packet from cache\n");
		}
	}
}
