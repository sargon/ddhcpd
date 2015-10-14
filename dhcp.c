/**
 * DHCP Structures
 */

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "dhcp.h"

int dhcp_new_lease_block(struct dhcp_lease_block** lease_block,uint32_t subnet_begin,uint32_t subnet_len) {
  *lease_block = (struct dhcp_lease_block*) malloc(sizeof(struct dhcp_lease_block));
  if ( ! *lease_block ) return 1;
  (*lease_block)->subnet_begin = subnet_begin;
  (*lease_block)->subnet_len = subnet_len;
  (*lease_block)->addresses = (struct dhcp_lease*) malloc(sizeof(struct dhcp_lease) * subnet_len);
  if ( ! (*lease_block)->addresses ) {
    free(*lease_block);
    return 1;
  }
  for ( int index = 0; index < subnet_len; index++ ) {
    (*lease_block)->addresses[index].client_id = 0; 
    (*lease_block)->addresses[index].state = FREE;
    (*lease_block)->addresses[index].lease_end = 0;
  }   
  return 0;
}

void dhcp_free_lease_block(struct dhcp_lease_block** lease_block){
  free((*lease_block)->addresses);
  free(*lease_block);
}

int32_t dhcp_discover(struct dhcp_lease_block** lease_block, uint32_t client_id) {
  time_t now = time(NULL);
  for(int index = 0; index < (*lease_block)->subnet_len; index++) {
    if ( (*lease_block)->addresses[index].state == FREE
       || ( (*lease_block)->addresses[index].state == OFFERED 
          && (*lease_block)->addresses[index].lease_end < now )) {
      (*lease_block)->addresses[index].client_id = client_id;
      (*lease_block)->addresses[index].state = OFFERED;
      (*lease_block)->addresses[index].lease_end = now + DHCP_OFFER_TIMEOUT;
      return index;
    } 
  }
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

int main() {

  struct dhcp_lease_block * block;
  dhcp_new_lease_block(&block,1,10);
  
  int32_t client0 = 123456;
  int32_t client1 = 123457;

  int32_t offer0 = dhcp_discover(&block,client0);
  printf("Offer: %i\n",offer0);
  int32_t offer1 = dhcp_discover(&block,client1);
  printf("Offer: %i\n",offer1);

  int request0 = dhcp_request(&block,client0,offer0);
  printf("Request: %i\n",request0);

  for ( int index = 0; index < block->subnet_len; index++ ) {
    printf("%i:%i;",block->addresses[index].state,block->addresses[index].lease_end);
  }
  printf("\n");

  dhcp_free_lease_block(&block);

  return 0;
}
