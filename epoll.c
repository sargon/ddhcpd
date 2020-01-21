#include <stdio.h>
#include <errno.h>

#include "epoll.h"
#include "logger.h"
#include "types.h"

void epoll_init(ddhcp_config* config) {
  int efd;

  efd = epoll_create1(0);

  if (efd == -1) {
    ERROR("Unable to initialize epoll socket");
    perror("epoll_create");
    abort();
  }

  config->epoll_fd = efd;
}

void add_fd(int efd, int fd, uint32_t events,void* ptr) {
  DEBUG("add_fd(%i,%i.%i)\n",efd,fd,events);
  struct epoll_event event = { 0 };
  event.data.ptr = ptr;
  event.data.fd = fd;
  event.events = events;

  int s = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event);

  if (s != 0) {
    exit(1);   //("epoll_ctl");
  }
}

void del_fd(int efd, int fd) {
  int s = epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);

  if (s < 0) {
    int errsv = errno;
    FATAL("%i", errsv);
    perror("epoll_ctl");
    exit(1);   //("epoll_ctl");
  }
}
