#ifndef _CONTROL_H
#define _CONTROL_H

#include "types.h"

#define DDHCPCTL_BLOCK_SHOW 1
#define DDHCPCTL_DHCP_OPTIONS_SHOW 2
#define DDHCPCTL_DHCP_OPTION_SET 3
#define DDHCPCTL_DHCP_OPTION_REMOVE 4

int handle_command(int socket, uint8_t* buffer, int msglen, ddhcp_config* config);

#endif
