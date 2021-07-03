/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - Helper functions
 *
 *  See AUTHORS file for copyright holders
 */

#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <netinet/in.h>
#include <stdlib.h>

#include "util.h"
#include "logger.h"

ATTR_NONNULL_ALL void addr_add(struct in_addr *subnet, struct in_addr *result,
			       int add)
{
	struct in_addr addr;
	memcpy(&addr, subnet, sizeof(struct in_addr));
	addr.s_addr = ntohl(addr.s_addr);
	addr.s_addr += (in_addr_t)add;
	addr.s_addr = htonl(addr.s_addr);
	memcpy(result, &addr, sizeof(struct in_addr));
}

dhcp_option *parse_option()
{
	char *len_s = strchr(optarg, ':');
	/* size_t optlen = strlen(optarg); */

	if (!len_s) {
		ERROR("parse_option(...): Malformed dhcp option '%s'\n",
		      optarg);
		exit(1);
	}

	len_s++ [0] = '\0';

	char *payload_s = strchr(len_s, ':');

	if (!payload_s) {
		ERROR("parse_option(...): Malformed dhcp option '%s'\n",
		      optarg);
		exit(1);
	}

	payload_s++ [0] = '\0';

	uint8_t len = (uint8_t)atoi(len_s);
	uint8_t code = (uint8_t)atoi(optarg);

	dhcp_option *option = (dhcp_option *)malloc(sizeof(dhcp_option));

	if (!option) {
		ERROR("parse_option(...): Failed to allocate memory for dhcp option '%s'\n",
		      optarg);
		exit(1);
	}

	option->code = code;
	option->len = len;
	option->payload = (uint8_t *)calloc(len, sizeof(uint8_t));

	if (!option->payload) {
		ERROR("parse_option(...): Failed to allocate memory for dhcp option payload '%s'\n",
		      optarg);
		free(option);
		exit(1);
	}

	for (int i = 0; i < len; i++) {
		char *next_payload_s = strchr(payload_s, '.');

		if (!next_payload_s && (i + 1 < len)) {
			ERROR("parse_option(...): Malformed dhcp option '%s': too little payload\n",
			      optarg);
			exit(1);
		}

		if (i + 1 < len)
			next_payload_s++ [0] = '\0';

		uint8_t payload = (uint8_t)strtoul(payload_s, NULL, 0);
		option->payload[i] = payload;
		payload_s = next_payload_s;
	}

	return option;
}

ATTR_NONNULL_ALL char *hwaddr2c(uint8_t *hwaddr)
{
	char *str = calloc(18, sizeof(char));

	if (!str) {
		FATAL("hwaddr2c(...): Failed to allocate buffer.\n");
		return NULL;
	}

	snprintf(str, 18, "%02X:%02X:%02X:%02X:%02X:%02X", hwaddr[0], hwaddr[1],
		 hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);

	return str;
}
