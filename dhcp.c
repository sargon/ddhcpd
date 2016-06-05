#include <string.h>

#include "dhcp.h"
#include "tools.h"
#include "logger.h"
#include "dhcp_options.h"

// Free an offered lease after 12 seconds.
uint16_t DHCP_OFFER_TIMEOUT = 12;
uint16_t DHCP_LEASE_TIME    = 3600;

#if LOG_LEVEL >= LOG_DEBUG
#define DEBUG_DHCP_LEASE(...) do { \
  DEBUG("DHCP LEASE [ state %i, xid %u, end %i ]\n",lease->state,lease->xid,lease->lease_end);\
} while (0);
#else
#define DEBUG_LEASE(...)
#endif

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
    memset(&(*lease_block)->addresses[index].chaddr,0,16);
    (*lease_block)->addresses[index].state = FREE;
    (*lease_block)->addresses[index].lease_end = 0;
  }
  return 0;
}

void dhcp_free_lease_block(struct dhcp_lease_block** lease_block) {
  free((*lease_block)->addresses);
  free(*lease_block);
}

dhcp_packet* build_initial_packet( dhcp_packet *from_client ) {
  DEBUG("build_initial_packet( from_client, packet )\n");

  dhcp_packet *packet = (dhcp_packet*) calloc(sizeof(dhcp_packet),1);
  if ( packet == NULL ) {
    DEBUG("build_initial_packet(...) -> memory allocation failure\n");
    return NULL;
  }
  packet->op    = 2;
  packet->htype = from_client->htype;
  packet->hlen  = from_client->hlen;
  packet->hops  = from_client->hops;
  packet->xid   = from_client->xid;
  packet->secs  = 0;
  packet->flags = from_client->flags;
  memcpy(&packet->ciaddr,&from_client->ciaddr,4);
  // yiaddr
  // siaddr
  memcpy(&packet->giaddr,&from_client->giaddr,4);
  memcpy(&packet->chaddr,&from_client->chaddr,16);
  // sname
  // file
  // options

  return packet;
}

int dhcp_discover(int socket, dhcp_packet *discover, ddhcp_block *blocks, ddhcp_config *config ) {
  DEBUG("dhcp_discover( %i, packet, blocks, config)\n",socket);
  time_t now = time(NULL);
  ddhcp_block *block = blocks;
  dhcp_lease *lease = NULL;
  dhcp_lease_block *lease_block = NULL;
  int lease_index = 0;
  int lease_ratio = config->block_size + 1; 

  for ( uint32_t i = 0; i < config->number_of_blocks; i++) {
    if ( block->state == DDHCP_OURS ) {
      int free_leases = dhcp_num_free( block->lease_block);
      if ( free_leases > 0 ) {
        DEBUG("dhcp_discover(...) -> block %i has %i free leases\n", block->index, free_leases);
        lease_block = block->lease_block;
        if ( free_leases < lease_ratio ) {
          DEBUG("dhcp_discover(...) -> block %i has best lease ratio until now\n",block->index);
          uint32_t index = dhcp_get_free_lease( lease_block );
          lease = lease_block->addresses + index;
          lease_index = index;
          lease_ratio = free_leases;
        }
      }
    }
    block++;
  }

  if ( lease == NULL ) {
    DEBUG("dhcp_discover(...) -> no free leases found");
    return 2;
  } 

  dhcp_packet* packet = build_initial_packet(discover);
  if ( ! packet ) { 
    DEBUG("dhcp_discover(...) -> memory allocation failure");
    return 1;
  }

  // Mark lease as offered and register client
  memcpy(&lease->chaddr,&discover->chaddr,16);
  lease->xid = discover->xid;
  lease->state = OFFERED;
  lease->lease_end = now + DHCP_OFFER_TIMEOUT;
 
  addr_add(&lease_block->subnet,&packet->yiaddr,lease_index);
  packet->options_len = fill_options( discover->options, discover->options_len, &config->options , 2, &packet->options) ;
  
  // TODO Error handling
  set_option( packet->options, packet->options_len, DHCP_CODE_MESSAGE_TYPE, 1, (uint8_t[]) { DHCPOFFER } );
  set_option( packet->options, packet->options_len, DHCP_CODE_ADDRESS_LEASE_TIME, 1, (uint8_t[]) { DHCP_LEASE_TIME });

  send_dhcp_packet(socket, packet);
  free(packet);
  return 0;
}

int dhcp_request( int socket, struct dhcp_packet *request, ddhcp_block* blocks, ddhcp_config *config ){
  DEBUG("dhcp_request( %i, dhcp_packet, blocks, config)\n",socket);
  // search the lease we may have offered
  time_t now = time(NULL);
  ddhcp_block *block = blocks;
  dhcp_lease_block *lease_block = NULL;
  dhcp_lease *lease = NULL ;
  int lease_index = 0;
  for ( uint32_t i = 0; i < config->number_of_blocks; i++) {
    if ( block->state == DDHCP_OURS ) {
      dhcp_lease *lease_iter = block->lease_block->addresses;
      for ( unsigned int j = 0 ; j < block->lease_block->subnet_len ; j++ ) {
        if ( lease_iter->state == OFFERED && lease_iter->xid == request->xid ) {
          if ( memcmp(request->chaddr, lease_iter->chaddr,16) == 0 ) {
            lease_block = block->lease_block;
            lease = lease_iter;
            lease_index = j;
            DEBUG("dhcp_request(...): Found requested lease\n");
            break;
          }
        }
        lease_iter++;
      }
      if ( lease ) break;
    }
    block++;
  }

  if ( !lease ) {
    DEBUG("dhcp_request(...): Requested lease not found\n");
    // Send DHCP_NACK
    dhcp_nack ( socket, request );
    return 2;
  } 

  dhcp_packet* packet = build_initial_packet(request);
  if ( ! packet ) { 
    DEBUG("dhcp_request(...) -> memory allocation failure\n");
    return 1;
  }

  // Mark lease as leased and register client
  memcpy(&lease->chaddr,&request->chaddr,16);
  lease->xid = request->xid;
  lease->state = LEASED;
  lease->lease_end = now + DHCP_LEASE_TIME;

  
  addr_add(&lease_block->subnet,&packet->yiaddr,lease_index);

  packet->options_len = fill_options( request->options, request->options_len, &(config->options) , 2, &packet->options) ;

  // TODO Error handling
  set_option( packet->options, packet->options_len, DHCP_CODE_MESSAGE_TYPE, 1, (uint8_t[]) { DHCPACK });
  // TODO correct type conversion, currently solution is simply wrong
  set_option( packet->options, packet->options_len, DHCP_CODE_ADDRESS_LEASE_TIME, 4, (uint8_t[]) { 0,0,0,DHCP_LEASE_TIME });

  send_dhcp_packet(socket, packet);
  free(packet);

  return 0;
}

int dhcp_nack( int socket, dhcp_packet *from_client ) {
  dhcp_packet* packet = build_initial_packet(from_client);
  if ( ! packet ) { 
    DEBUG("dhcp_discover(...) -> memory allocation failure\n");
    return 1;
  }

  packet->options_len = 1;
  packet->options = (dhcp_option*) calloc( sizeof(dhcp_option), 1);
  // TODO Error handling

  set_option( packet->options, packet->options_len, DHCP_CODE_MESSAGE_TYPE, 1, (uint8_t[]) { DHCPNAK });
  
  send_dhcp_packet(socket, packet);
  free(packet);

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

uint32_t dhcp_get_free_lease( dhcp_lease_block *lease_block ) {
  dhcp_lease *lease = lease_block->addresses;
  for ( uint32_t i = 0 ; i < lease_block->subnet_len ; i++ ) {
    if ( lease->state == FREE ) {
      return i;
    }
    lease++;
  }
  ERROR("dhcp_get_free_lease(...): no free lease found");
  return lease_block->subnet_len;
}

void dhcp_check_timeouts( dhcp_lease_block * lease_block ) {
  dhcp_lease *lease = lease_block->addresses;
  time_t now = time(NULL);
  for ( unsigned int i = 0 ; i < lease_block->subnet_len ; i++ ) {
    if ( lease->state != FREE && lease->lease_end < now ) {
      printf("Free Lease\n");
      memset(lease->chaddr,0,16);
      lease->xid   = 0;
      lease->state = FREE;
    }
    lease++;
  }
}
