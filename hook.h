#pragma once

#include <stdint.h>

#include <netinet/in.h>

#include "types.h"

#define HOOK_LEASE 1
#define HOOK_RELEASE 2
#define HOOK_INFORM 3

ATTR_NONNULL_ALL void hook(uint8_t type, struct in_addr* address, uint8_t* chaddr, ddhcp_config* config);
void hook_init();
