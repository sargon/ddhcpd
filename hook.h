#ifndef _HOOK_H
#define _HOOK_H

#include "types.h"

#define HOOK_LEASE 1
#define HOOK_RELEASE 2

void hook(uint8_t type, struct in_addr* address, uint8_t* chaddr, ddhcp_config* config);

#endif
