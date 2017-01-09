#ifndef _NETSOCK_H
#define _NETSOCK_H

#include "types.h"

#define DDHCP_MULTICAST_PORT 1234

int control_open(ddhcp_config* state);
int control_connect(ddhcp_config* state);
int netsock_open(char* interface, char* interface_client, ddhcp_config* state);

#endif
