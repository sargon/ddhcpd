#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "ddhcp.h"
#include "netsock.h"
#include "packet.h"

const int NET = 0;
const uint32_t NUMBER_OF_BLOCKS = 10;
const int NET_LEN = 10;

struct ddhcp_block* blocks;

int ddhcp_block_init(int number_of_blocks, struct ddhcp_block **blocks){
  *blocks = (struct ddhcp_block*) malloc( sizeof(struct ddhcp_block) * number_of_blocks);

  if ( blocks == 0 ) {
    return 1;
  }

  struct ddhcp_block *block = (*blocks);
  for ( int index = 0; index < number_of_blocks; index++ ) {
    
    block->index = index;
    block->state = DDHCP_BLOCKED;
    block->subnet = NET + (index * NET_LEN);
    block->subnet_len = NET_LEN;
    block->address = 0;
    block->valid_until = 0;
    block->lease_block = NULL;
  }

  return 0;
}

void ddhcp_block_process_claims( struct ddhcp_block *blocks , struct ddhcp_mcast_packet *packet ) {
  assert(packet->command == 1);
  time_t now = time(NULL);
  for ( int i = 0 ; i < packet->count ; i++ ) {
    struct ddhcp_payload *claim = ((struct ddhcp_payload*) packet->payload)+i;
    if ( claim->block_index >= NUMBER_OF_BLOCKS ) {
      printf("warning: malformed block number");
      continue;
    }
    blocks[claim->block_index].state = DDHCP_CLAIMED;
    blocks[claim->block_index].valid_until = now + claim->timeout;
  }
}

int main(int argc, char **argv) {

  char* interface = "veth2";

  int c;
  while (( c = getopt(argc,argv,"i:")) != -1 ) {
    switch(c) {
      case 'i': 
        interface = optarg;
        printf("Using interface: %s\n",interface);
        break;
      default:
        printf("ARGC: %i",argc);
        abort ();
    }
  }

  printf("Number of blocks: %i\n",NUMBER_OF_BLOCKS);

  // init block stucture
  ddhcp_block_init(NUMBER_OF_BLOCKS,&blocks);

  // init network and event loops
  // TODO
  int interface_msock = 4;
  if ( netsock_open(interface,&interface_msock) == -1) {
    return 1;
  }

  char* buffer = (char*) malloc( 10000 );
  struct ddhcp_mcast_packet packet;
  while(1) {
    int byte = recv(interface_msock, buffer, 10000,0);
    if ( byte > 0 ) {
      ntoh_mcast_packet(buffer,byte, &packet);
      switch(packet.command) {
        case 1:
          ddhcp_block_process_claims(blocks,&packet);
        break;
        default:
        break;
      }
      free(packet.payload);
    }
  }

  free(blocks);
  return 0;
}
