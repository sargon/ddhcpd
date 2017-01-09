#include "packet.h"

#include <endian.h>
#include <assert.h>
#include <errno.h>

int _packet_size(int command, int payload_count) {
  int len = 0;

  switch (command) {
  case 1:
    len = 16 + payload_count * 7;
    break;

  case 2:
    len = 16 + payload_count * 4;
    break;

  default:
    printf("Error: unknown command: %i/%i \n", command, payload_count);
    return -1;
    break;
  }

  return len;
}

int ntoh_mcast_packet(uint8_t* buffer, int len, struct ddhcp_mcast_packet* packet) {

  // Header
  uint64_t node;
  memcpy(&node, packet->node_id, 8);


  // The Python implementation prefixes with a node number?
  // prefix address
  memcpy(&(packet->prefix), buffer + 8, sizeof(struct in_addr));
  // prefix length
  packet->prefix_len  = buffer[12];
  // size of a block
  packet->blocksize   = buffer[13];
  // the command
  packet->command     = buffer[14];
  // count of payload entries
  packet->count       = buffer[15];

  int should_len = _packet_size(packet->command, packet->count);

  if (should_len != len) {
    printf("Wrong length: %i/%i", len, should_len);
    return 1;
  }

  char str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(packet->prefix), str, INET_ADDRSTRLEN);
  printf("NODE: %lu PREFIX: %s/%i BLOCKSIZE: %i COMMAND: %i ALLOCATIONS: %i\n",
         (long unsigned int) packet->node_id,
         str,
         packet->prefix_len,
         packet->blocksize,
         packet->command,
         packet->count
        );

  buffer = buffer + 16;

  // Payload
  uint8_t  tmp8;
  uint16_t tmp16;
  uint32_t tmp32;
  struct ddhcp_payload* payload;

  switch (packet->command) {
  // UpdateClaim
  case 1:
    packet->payload = (struct ddhcp_payload*) malloc(sizeof(struct ddhcp_payload) * packet->count);
    payload = packet->payload;

    for (int i = 0 ; i < packet->count ; i++) {
      memcpy(&tmp32, buffer, 4);
      payload->block_index = ntohl(tmp32);
      memcpy(&tmp16, buffer + 4, 2);
      payload->timeout = ntohs(tmp16);
      memcpy(&tmp8, buffer + 6, 1);
      payload->reserved = tmp8;
      payload++;
      buffer = buffer + 7;
    }

    break;

  // InquireBlock
  case 2:
    packet->payload = (struct ddhcp_payload*) malloc(sizeof(struct ddhcp_payload) * packet->count);
    payload = packet->payload;

    for (int i = 0 ; i < packet->count ; i++) {
      memcpy(&tmp32, buffer, 4);
      payload->block_index = ntohl(tmp32);
      payload++;
      buffer = buffer + 8;
    }

    break;

  // ReNEWLease
  case 16:
    packet->payload = (struct ddhcp_payload*) malloc(sizeof(struct ddhcp_payload) * packet->count);

  default:
    return 2;
    break;
  }

  printf("\n");

  return 0;
}

int send_packet_mcast(struct ddhcp_mcast_packet* packet, int mulitcast_socket, uint32_t scope_id) {
  int len = _packet_size(packet->command, packet->count);
  char* buffer = (char*) calloc(1, len);
  errno = 0;

  memcpy(buffer, &(packet->node_id), 8);
  memcpy(buffer + 8, &(packet->prefix), sizeof(struct in_addr));

  // prefix length
  buffer[12] = packet->prefix_len;
  // size of a block
  buffer[13] = packet->blocksize;
  // the command
  buffer[14] = packet->command;
  // count of payload entries
  buffer[15] = packet->count;

  uint16_t tmp16;
  uint32_t tmp32;
  struct ddhcp_payload* payload;

  char* pbuf = buffer + 16;

  switch (packet->command) {
  case 1:
    payload = packet->payload;

    for (unsigned int index = 0 ; index < packet->count ; index++) {
      tmp32 = htonl(payload->block_index);
      memcpy(pbuf, &tmp32, 4);
      tmp16 = htons(payload->timeout);
      memcpy(pbuf + 4, &tmp16, 2);
      memcpy(pbuf + 6, &payload->reserved, 1);
      payload++;
      pbuf += 7;
    }

    break;

  case 2:
    payload = packet->payload;

    for (unsigned int index = 0 ; index < packet->count ; index++) {
      tmp32 = htonl(payload->block_index);
      memcpy(pbuf, &tmp32, 4);
      payload++;
      pbuf += 4;
    }

    break;
  }

  struct sockaddr_in6 dest_addr = {
    .sin6_family = AF_INET6,
    .sin6_port = htons(1234),
    .sin6_scope_id = scope_id
  };

  const struct in6_addr dest =
  {
    {
      {
        0xff, 0x02, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x12, 0x34
      }
    }
  };

  memcpy(&dest_addr.sin6_addr, &dest, sizeof(dest));

  sendto(mulitcast_socket, buffer, len, 0, (struct sockaddr*) &dest_addr, sizeof(dest_addr));

  free(buffer);

  return 0;
}
