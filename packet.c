#include "packet.h"
#include <endian.h>
#include <assert.h>

int ntoh_mcast_packet(char* buffer,int len, struct ddhcp_mcast_packet* packet){
  
  // Header
  uint64_t node;
  memcpy (&node,buffer,8); node = be64toh(node);

  // The Python implementation prefixes with a node number?
  // prefix address
  memcpy(&(packet->prefix),buffer+8,sizeof(struct in_addr));
  // prefix length
  packet->prefix_len  = buffer[12];
  // size of a block
  packet->blocksize   = buffer[13];
  // the command
  packet->command     = buffer[14];
  // count of payload entries
  packet->count       = buffer[15];

  char str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(packet->prefix), str, INET_ADDRSTRLEN);
  printf("NODE: %lu PREFIX: %s/%i BLOCKSIZE: %i COMMAND: %i ALLOCATIONS: %i\n", 
      (long unsigned int) node,
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

  switch(packet->command){
    // UpdateClaim
    case 1:
      packet->payload = (struct ddhcp_payload*) malloc(sizeof(struct ddhcp_payload)*packet->count);
      payload = packet->payload;
      for ( int i = 0 ; i < packet->count ; i++ ){
        memcpy(&tmp32, buffer   ,4 ); payload->block_index = ntohl(tmp32);
        memcpy(&tmp16, buffer+4 ,2 ); payload->timeout =ntohs(tmp16);
        memcpy(&tmp8, buffer+6 ,1 ); payload->reserved = tmp8;
        printf("UPDATE_CLAIM: BLOCK:%i TIMEOUT:%i USAGE:%i \n",
            packet->payload[i].block_index,
            packet->payload[i].timeout,
            packet->payload[i].reserved
            );
        payload++;
        buffer = buffer + 7;
      }
    break;
    // InquireBlock
    case 2:
      packet->payload = (struct ddhcp_payload*) malloc(sizeof(struct ddhcp_payload)*packet->count);
      payload = packet->payload;
      for ( int i = 0 ; i < packet->count ; i++ ){
        memcpy(&tmp32, buffer   ,4 ); payload->block_index = ntohl(tmp32);
        printf("INQUIRE_BLOCK: BLOCK:%i \n",
            packet->payload[i].block_index
            );
        payload++;
        buffer = buffer + 8;
      }
    break;
    default:
      return 1;
    break;
  }

  printf("\n");

  return 0;
}
