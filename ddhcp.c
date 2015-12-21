#include <stdlib.h>
#include <stdio.h>

#include "ddhcp.h"
#include "netsock.h"
#include "packet.h"

const int NET = 0;
const int NUMBER_OF_BLOCKS = 10;
const int NET_LEN = 10;

struct ddhcp_block* blocks;

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
  if ( netsock_open(interface,&interface_msock) == -1) {
    return 1;
  }

  char* buffer = (char*) malloc( sizeof(struct ddhcp_mcast_packet));
  printf("Socket: %i\n",interface_msock);
  struct ddhcp_mcast_packet packet;
  while(1) {
    int byte = recv(interface_msock, buffer, 1000,0);
    if ( byte > 0 ) {
      ntoh_mcast_packet(buffer,byte, &packet);
    }
  }

  free(blocks);
  return 0;
}
