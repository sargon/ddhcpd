#ifndef _EPOLL_H
#define _EPOLL_H

#include <sys/epoll.h>
#include "types.h"

/** 
 * Add a file descriptor to an epoll instance.
 */
void add_fd(int efd, int fd, uint32_t events,void* ptr);

/**
 * Remove a file descriptor from an epoll instance.
 */
void del_fd(int efd, int fd);

#endif
