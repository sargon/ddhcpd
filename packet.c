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

  switch(packet->command){
    case 1:
      packet->payload = (void*) malloc(sizeof(struct ddhcp_update_claim)*packet->count);
      struct ddhcp_update_claim* payload = ((struct ddhcp_update_claim*) packet->payload);
      for ( int i = 0 ; i < packet->count ; i++ ){
        uint32_t tmp32;
        uint16_t tmp16;
        memcpy(&tmp32, buffer   ,4 ); payload->block_index = ntohl(tmp32);
        memcpy(&tmp16, buffer+4 ,2 ); payload->timeout =ntohs(tmp16);
        memcpy(&(payload->usage) , buffer+6 ,1 );
        printf("UPDATE_CLAIM: BLOCK:%i TIMEOUT:%i USAGE:%i \n",
            ((struct ddhcp_update_claim*) packet->payload)[i].block_index,
            ((struct ddhcp_update_claim*) packet->payload)[i].timeout,
            ((struct ddhcp_update_claim*) packet->payload)[i].usage
            );
        payload = payload+8;
        buffer = buffer + 8;
      }
    break;
    default:
    break;
  }

  return 0;
}
