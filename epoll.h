#ifndef _EPOLL_H
#define _EPOLL_H

#include <sys/epoll.h>
#include "types.h"

typedef int (*ddhcpd_epoll_event_t)(epoll_data_t,ddhcp_config*);

struct ddhcp_epoll_data {
  int fd;
  ddhcpd_epoll_event_t epollin;
  ddhcpd_epoll_event_t epollhup;
  
};
typedef struct ddhcp_epoll_data ddhcp_epoll_data;

#define epoll_get_fd(data) (((ddhcp_epoll_data*) data.ptr)->fd)
#define epoll_data_free(data) (free(data.ptr))

/**
 * Initialize epoll socket.
 */
void epoll_init(ddhcp_config* config);

/** 
 * Add a file descriptor to an epoll instance.
 */
void add_fd(int efd, int fd, uint32_t events,ddhcpd_epoll_event_t epollin);

/**
 * Remove a file descriptor from an epoll instance.
 */
void del_fd(int efd, int fd);

#endif
