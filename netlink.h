/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - Netlink abstraction layer
 *
 *  See AUTHORS file for copyright holders
 */

#ifndef _DDHCP_NETLINK_H
#define _DDHCP_NETLINK_H

#include "types.h"
#include "epoll.h"

ATTR_NONNULL_ALL int netlink_init(epoll_data_t data, ddhcp_config *config);
ATTR_NONNULL_ALL int netlink_in(epoll_data_t data, ddhcp_config *config);
ATTR_NONNULL_ALL int netlink_close(epoll_data_t, ddhcp_config *config);
#endif
