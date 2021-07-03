/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - epoll abstraction layer
 *
 *  See AUTHORS file for copyright holders
 */

#ifndef _DDHCP_EPOLL_H
#define _DDHCP_EPOLL_H

#include <sys/epoll.h>
#include "types.h"

/* epoll_data_t is always a void pointer to a ddhcp_epoll_data struct. */
typedef int (*ddhcpd_epoll_event_t)(epoll_data_t, ddhcp_config *);
typedef int (*ddhcpd_socket_init_t)(epoll_data_t, ddhcp_config *);

struct ddhcp_epoll_data {
	int fd;
	int interface_id;
	char *interface_name;
	void *data;
	ddhcpd_socket_init_t setup;
	ddhcpd_epoll_event_t epollin;
	ddhcpd_epoll_event_t epollhup;
};
typedef struct ddhcp_epoll_data ddhcp_epoll_data;

#define epoll_get_fd(data) (((ddhcp_epoll_data *)data.ptr)->fd)
#define epoll_data_free(data) (free(data.ptr))
#define epoll_data_call(data, method, config)                                  \
	data->method((epoll_data_t){ .ptr = (void *)data }, config)

/**
 * Initialize epoll socket.
 */
void epoll_init(ddhcp_config *config);

/**
 * Initializing a new ddhcp_epoll_data structure
 */
ddhcp_epoll_data *epoll_data_new(char *interface_name,
				 ddhcpd_socket_init_t setup,
				 ddhcpd_epoll_event_t epollin,
				 ddhcpd_epoll_event_t epollhup);

/**
 * Add a file descriptor to an epoll instance.
 */
void epoll_add_fd(int efd, ddhcp_epoll_data *data, uint32_t events,
		  ddhcp_config *config);

/**
 * Remove a file descriptor from an epoll instance.
 */
void epoll_del_fd(int efd, int fd);

#endif
