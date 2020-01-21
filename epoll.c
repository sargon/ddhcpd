#include <stdio.h>
#include <errno.h>

#include "epoll.h"
#include "logger.h"

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
