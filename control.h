#ifndef _CONTROL_H
#define _CONTROL_H

#include "types.h"

enum {
  DDHCPCTL_BLOCK_SHOW = 1,
  DDHCPCTL_DHCP_OPTIONS_SHOW,
  DDHCPCTL_DHCP_OPTION_SET,
  DDHCPCTL_DHCP_OPTION_REMOVE,
  DDHCPCTL_LOG_LEVEL_SET,
};

int handle_command(int socket, uint8_t* buffer, int msglen, ddhcp_config* config);

#endif
