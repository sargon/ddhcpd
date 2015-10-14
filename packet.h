#ifndef _PACKET_H
#define _PACKET_H

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/select.h>

struct ddhcp_mcast_packet {
  struct in_addr prefix;
  uint8_t prefix_len;
  uint8_t blocksize;
  uint8_t command;
  uint8_t count;
};

#endif
