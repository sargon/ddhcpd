/**
 * DHCP Structures
 */

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "dhcp.h"
#include "tools.h"

int DHCP_OFFER_TIMEOUT = 600;
int DHCP_LEASE_TIME    = 3600;

void printf_lease(dhcp_lease *lease) {
  printf("DHCP LEASE [ cid %i, state %i, xid %u, end %i ]\n",lease->client_id,lease->state,lease->xid,lease->lease_end); 
}

int dhcp_new_lease_block(struct dhcp_lease_block** lease_block,struct in_addr *subnet,uint32_t subnet_len) {
  *lease_block = (struct dhcp_lease_block*) malloc(sizeof(struct dhcp_lease_block));
  if ( ! *lease_block ) return 1;
  memcpy(&(*lease_block)->subnet,subnet,sizeof(struct in_addr));
  (*lease_block)->subnet_len = subnet_len;
  (*lease_block)->addresses = (struct dhcp_lease*) calloc(subnet_len,sizeof(struct dhcp_lease));
  if ( ! (*lease_block)->addresses ) {
    free(*lease_block);
    return 1;
  }
  for ( unsigned int index = 0; index < subnet_len; index++ ) {
    (*lease_block)->addresses[index].client_id = 0;
    (*lease_block)->addresses[index].state = FREE;
    (*lease_block)->addresses[index].lease_end = 0;
  }
  return 0;
}

void dhcp_free_lease_block(struct dhcp_lease_block** lease_block) {
  free((*lease_block)->addresses);
  free(*lease_block);
}

int dhcp_discover(int socket, dhcp_packet *discover,struct dhcp_lease_block *lease_block, uint32_t client_id) {
  time_t now = time(NULL);
  dhcp_lease *lease = NULL;
  int lease_index = 0;
  for( unsigned int index = 0; index < lease_block->subnet_len && lease == NULL; index++) {
    if ( lease_block->addresses[index].state == FREE
       || ( lease_block->addresses[index].state == OFFERED
          && lease_block->addresses[index].lease_end < now )) {
      lease = lease_block->addresses + index;
      lease_index = index;
      lease->client_id = client_id;
      lease->xid = discover->xid;
      lease->state = OFFERED;
      lease->lease_end = now + DHCP_OFFER_TIMEOUT;
      printf("SET\n");
    }
  }

  if ( lease == NULL ) {
    return -1;
  }

  //struct in_addr tmpaddr;

  dhcp_packet* packet = (dhcp_packet*) calloc(sizeof(dhcp_packet),1);
  packet->op    = 2;
  packet->htype = discover->htype;
  packet->hlen  = discover->hlen;
  packet->hops  = discover->hops;
  packet->xid   = discover->xid;
  packet->secs  = 0;
  packet->flags = discover->flags;
  // ciaddr
  addr_add(&lease_block->subnet,&packet->yiaddr,lease_index);
  // siaddr
  memcpy(&packet->giaddr,&discover->giaddr,4);
  memcpy(&packet->chaddr,&discover->chaddr,16);
  // sname
  // file
  packet->options_len = 5;
  packet->options = (dhcp_option*) calloc(sizeof(dhcp_option) , packet->options_len);
  packet->options[0].code = 53;
  packet->options[0].len = 1;
  packet->options[0].payload = (uint8_t*)  malloc(sizeof(uint8_t) * 1 );
  packet->options[0].payload[0] = 2;
  
  send_dhcp_packet(socket, packet);
  free(packet);
  return 0;
}

int dhcp_request(struct dhcp_lease_block** lease_block,uint32_t client_id,uint32_t offer){
  time_t now = time(NULL);
  if ( (*lease_block)->subnet_len < offer) {
    // Requested offer is out of scope
    return 1;
  }
  if ( (*lease_block)->addresses[offer].client_id != client_id) {
    // Requested offer is not offered to client
    return 2;
  }
  if ( (*lease_block)->addresses[offer].state != OFFERED ) {
    // Address has not been offered
    return 3;
  }
  if ( (*lease_block)->addresses[offer].lease_end < now) {
    // Offer has timed out
    // Q: Are we to restrictive here?
    return 4;
  }
  (*lease_block)->addresses[offer].state = LEASED;
  (*lease_block)->addresses[offer].lease_end = now + DHCP_LEASE_TIME;
  return 0;
}

int dhcp_has_free(struct dhcp_lease_block *lease_block) {
  dhcp_lease *lease = lease_block->addresses;
  for ( unsigned int i = 0 ; i < lease_block->subnet_len ; i++ ) {
    if ( lease->state == FREE ) {
      return 1;
    }
    lease++;
  }
  return 0;
}

int dhcp_num_free( struct dhcp_lease_block *lease_block ) {
  int num = 0;
  dhcp_lease *lease = lease_block->addresses;
  for ( unsigned int i = 0 ; i < lease_block->subnet_len ; i++ ) {
    if ( lease->state == FREE ) {
      num++;
    }
    lease++;
  }
  return num;
}
