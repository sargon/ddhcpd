#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdint.h>
#include <time.h>
#include <sys/select.h>
#include <sys/types.h>
#include "packet.h"

#define DDHCP_MULTICAST_PORT 1234

int netsock_open(char* interface,int* interface_mcast);
