#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include "epoll.h"
#include "logger.h"
#include "types.h"

void epoll_init(ddhcp_config* config) {
  int efd;

  efd = epoll_create1(0);

  if (efd == -1) {
    ERROR("Unable to initialize epoll socket\n");
    perror("epoll_create");
    abort();
  }

  config->epoll_fd = efd;
}

int hdl_epoll_hup(epoll_data_t data, ddhcp_config* config) {
  int fd = epoll_get_fd(data);
  DEBUG("hdl_epoll_hup(fd,config): Removing epoll fd:%i\n",fd);
  del_fd(config->epoll_fd, fd);
  epoll_data_free(data);
  close(fd);
  return 0;
}

void add_fd(int efd, int fd, uint32_t events, eventhandler_t epollin) {
  DEBUG("add_fd(%i,%i.%i)\n",efd,fd,events);
  struct epoll_event event = { 0 };
  event.events = events;

  ddhcp_epoll_data* ptr = (ddhcp_epoll_data*) calloc(1,sizeof(ddhcp_epoll_data));
  if (ptr == NULL) {
    FATAL("Unable to allocate memory for ddhcp_epoll_data\n");
    exit(2);
  }
  ptr->fd = fd;
  ptr->epollin = epollin;
  ptr->epollhup = hdl_epoll_hup;
  event.data.ptr = (void*) ptr;

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
