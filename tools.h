#pragma once

#include <stdint.h>

#include <netinet/in.h>

#include "types.h"

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))
#define UNUSED(x) (void)(x)

ATTR_NONNULL_ALL void addr_add(struct in_addr* subnet, struct in_addr* result, int add);
dhcp_option* parse_option();
ATTR_NONNULL_ALL char* hwaddr2c(uint8_t* hwaddr);
