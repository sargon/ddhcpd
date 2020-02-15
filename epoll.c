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

ddhcp_epoll_data* epoll_data_new(
    char* interface_name
    , ddhcpd_socket_init_t setup
    , ddhcpd_epoll_event_t epollin
    , ddhcpd_epoll_event_t epollhup
    ) {
  ddhcp_epoll_data* ptr = (ddhcp_epoll_data*) calloc(1,sizeof(ddhcp_epoll_data));
  if (ptr == NULL) {
    FATAL("Unable to allocate memory for ddhcp_epoll_data\n");
    exit(2);
  }
  ptr->interface_name = interface_name;
  ptr->epollin = epollin;
  ptr->setup = setup;
  if (epollhup == NULL) {
    ptr->epollhup = hdl_epoll_hup;
  } else {
    ptr->epollhup = epollhup;
  }
  return ptr;
}

void epoll_add_fd(int efd, ddhcp_epoll_data *data, uint32_t events,ddhcp_config* config) {
  DEBUG("epoll_add_fd(%i,%i)\n",efd,events);
  // Initializing socket if needed
  if (data->fd == 0) {
    if (epoll_data_call(data,setup,config) != 0) {
      FATAL("epoll_add_fd(...): Failure while initializing socket");
      exit(2);
    }
  }

  // Setup epoll event
  struct epoll_event event = { 0 };
  event.events = events;
  event.data.ptr = (void*) data;

  if(epoll_ctl(efd, EPOLL_CTL_ADD, data->fd, &event) != 0) {
    FATAL("epoll_add_fd(...): Unable to register file descriptor to epoll"); 
    exit(1);
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
