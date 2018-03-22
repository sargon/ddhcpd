#ifndef _CONTROL_H
#define _CONTROL_H

#include "types.h"

int handle_command(int socket, uint8_t* buffer, int msglen, ddhcp_config* config);

#endif
