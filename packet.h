#ifndef _PACKET_H
#define _PACKET_H
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct ddhcp_mcast_packet {
  uint64_t node_id;
  struct in_addr prefix;
  uint8_t prefix_len;
  uint8_t blocksize;
  uint8_t command;
  uint8_t count;

  struct ddhcp_payload* payload;
};

struct ddhcp_payload {
   uint32_t block_index;
   uint16_t timeout;
   uint16_t reserved;
};

int ntoh_mcast_packet(char* buffer,int len, struct ddhcp_mcast_packet* packet);

int send_packet_mcast( struct ddhcp_mcast_packet* packet, int mulitcast_socket, uint32_t scope_id );

#endif
