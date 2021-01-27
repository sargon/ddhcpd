/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - Command interface abstraction
 *
 *  See AUTHORS file for copyright holders
 */

#ifndef _DDHCP_CONTROL_H
#define _DDHCP_CONTROL_H

#include "types.h"

enum { DDHCPCTL_BLOCK_SHOW = 1,
       DDHCPCTL_DHCP_OPTIONS_SHOW,
       DDHCPCTL_DHCP_OPTION_SET,
       DDHCPCTL_DHCP_OPTION_REMOVE,
       DDHCPCTL_LOG_LEVEL_SET,
       DDHCPCTL_STATISTICS,
       DDHCPCTL_STATISTICS_RESET,
};

ATTR_NONNULL_ALL int handle_command(int socket, uint8_t *buf, ssize_t msglen,
				    ddhcp_config *config);

#endif
