#include <stdlib.h>
#include <stdio.h>

#include "ddhcp.h"
#include "netsock.h"
#include "packet.h"

const int NET = 0;
const int NUMBER_OF_BLOCKS = 10;
const int NET_LEN = 10;

struct ddhcp_block* blocks;

int main() {

  // init block stucture

  blocks = (struct ddhcp_block*) malloc( sizeof(struct ddhcp_block) * NUMBER_OF_BLOCKS);

  for ( int index = 0; index < NUMBER_OF_BLOCKS; index++ ) {
    blocks[index].index = index;
    blocks[index].state = DDHCP_BLOCKED;
    blocks[index].subnet = NET + (index * NET_LEN);
    blocks[index].subnet_len = NET_LEN;
    blocks[index].address = 0;
    blocks[index].valid_until = 0;
    blocks[index].lease_block = NULL;
  }

  // init network and event loops
  // TODO
  int interface_msock = 4;
  char* interface = "veth2";
  netsock_open(interface,&interface_msock);

  char* buffer = (char*) malloc( sizeof(struct ddhcp_mcast_packet));
  printf("Socket: %i\n",interface_msock);
  while(1) {

  int byte = recv(interface_msock, buffer, sizeof(struct ddhcp_mcast_packet),0);
  if ( byte > 0 ) {
    printf("ByteRead: %i, Command: %i",byte,errno,((struct ddhcp_mcast_packet*) buffer)->command);
  }
  }

  free(blocks);
  return 0;
}
