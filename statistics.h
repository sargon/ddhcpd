/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - Statistics glue
 *
 *  See AUTHORS file for copyright holders
 */

#ifndef _STATISTICS_H
#define _STATISTICS_H

#include "types.h"

#ifdef DDHCPD_STATISTICS
#define statistics_record(config, type, count)                                 \
	do {                                                                   \
		(config)->statistics[type] += count;                           \
	} while (0)
ATTR_NONNULL_ALL void statistics_show(int socket, uint8_t reset,
				      ddhcp_config *config);
#else
#define statistics_record(...)
#define statistics_show(...)
#endif

#endif
