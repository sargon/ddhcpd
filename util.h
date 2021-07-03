/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - Helper functions
 *
 *  See AUTHORS file for copyright holders
 */

#ifndef _DDHCP_UTIL_H
#define _DDHCP_UTIL_H

#include <arpa/inet.h>

#include "types.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define UNUSED(x) (void)(x)

ATTR_NONNULL_ALL void addr_add(struct in_addr *subnet, struct in_addr *result,
			       int add);
dhcp_option *parse_option();
ATTR_NONNULL_ALL char *hwaddr2c(uint8_t *hwaddr);

#endif
