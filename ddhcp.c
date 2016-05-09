#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include "ddhcp.h"
#include "dhcp.h"
#include "dhcp_packet.h"
#include "netsock.h"
#include "packet.h"
#include "tools.h"

const int NET = 0;
const int NET_LEN = 10;

struct ddhcp_block* blocks;

int ddhcp_block_init(struct ddhcp_block **blocks, ddhcp_config *config){
  *blocks = (struct ddhcp_block*) malloc( sizeof(struct ddhcp_block) * config->number_of_blocks);
  int now = time(NULL);

  if ( blocks == 0 ) {
    return 1;
  }

  // TODO Maybe we should allocate number_of_blocks dhcp_lease_blocks previous
  //      and assign one here instead of NULL. Performance boost, Memory defrag?
  struct ddhcp_block *block = (*blocks);
  for ( int index = 0; index < config->number_of_blocks; index++ ) {
    block->index = index;
    block->state = DDHCP_FREE;
    addr_add(&config->prefix,&block->subnet,index * config->block_size);
    block->subnet_len = config->block_size;
    block->address = 0;
    block->timeout = now + config->block_timeout;
    block->claiming_counts = 0;
    block->lease_block = NULL;
    block++;
  }

  return 0;
}

void ddhcp_block_process_claims( struct ddhcp_block *blocks , struct ddhcp_mcast_packet *packet ,ddhcp_config *config) {
  assert(packet->command == 1);
  time_t now = time(NULL);
  for ( unsigned int i = 0 ; i < packet->count ; i++ ) {
    struct ddhcp_payload *claim = ((struct ddhcp_payload*) packet->payload)+i;
    uint32_t block_index = claim->block_index;
    if ( block_index >= config->number_of_blocks ) {
      printf("warning: malformed block number");
    } 
    if ( blocks[block_index].state == DDHCP_OURS ) {
      printf("Someone tries to steel our block");
    } else {
        blocks[block_index].state = DDHCP_CLAIMED;
        blocks[block_index].timeout = now + claim->timeout;
        printf("UpdateClaim [ node_id: %lu, block_index: %i, timeout: %i ]\n",packet->node_id,block_index,claim->timeout);
    }
  }
}

void ddhcp_block_process_inquire( struct ddhcp_block *blocks , struct ddhcp_mcast_packet *packet ,ddhcp_config *config) {
  assert(packet->command == 2);
  time_t now = time(NULL);
  for ( unsigned int i = 0 ; i < packet->count ; i++ ) {
    struct ddhcp_payload *tmp = ((struct ddhcp_payload*) packet->payload)+i;
    if ( tmp->block_index >= config->number_of_blocks ) {
      printf("warning: malformed block number");
      continue;
    }
    printf("Inquire [ node_id: %lu, block_index: %i ]\n",packet->node_id,tmp->block_index);
    if ( blocks[tmp->block_index].state == DDHCP_OURS ) {
      // Update Claims
      blocks[tmp->block_index].timeout = 0;
      block_update_claims( blocks, config );
    } else if ( blocks[tmp->block_index].state == DDHCP_CLAIMING ) {
      if ( packet->node_id > config->node_id ) {
        blocks[tmp->block_index].state = DDHCP_TENTATIVE;
        blocks[tmp->block_index].timeout = now + config->tentative_timeout;
      } 
      // otherwise keep inquiring, the other node should see our inquires and step back.
    } else {
      printf("Marking block as  tentative %i\n",tmp->block_index);
      blocks[tmp->block_index].state = DDHCP_TENTATIVE;
      blocks[tmp->block_index].timeout = now + config->tentative_timeout;
    }
  }
}

/**
 * Either find and return a block with free leases or otherwise return NULL.
 */
ddhcp_block* block_find_lease( ddhcp_block *blocks , ddhcp_config *config) {
  ddhcp_block *block = blocks;
  for ( int i = 0 ; i < config->number_of_blocks; i++ ) {
    if ( block->state == DDHCP_OURS ) {
      if ( dhcp_has_free(block->lease_block) ) {
        return block;
      }
    }
    block++;
 }
 return NULL;
}

/** 
 * House Keeping
 * 
 * - Free timed-out DHCP leases. 
 * - Refresh timed-out blocks.
 * + Claim new blocks if we are low on spare leases.
 * + Update our claims.
 */
void house_keeping( ddhcp_block *blocks, ddhcp_config *config ) {
 int spares = block_num_free_leases( blocks, config );  
 int spare_blocks = ceil( (double) spares / (double) config->block_size );
 int blocks_needed = config->spare_blocks_needed - spare_blocks;
 block_claim( blocks, blocks_needed, config );
 block_update_claims( blocks, config );
 block_check_timeouts( blocks, config );
}

void add_fd(int efd, int fd, uint32_t events) {
  struct epoll_event event = {};
  event.data.fd = fd;
  event.events = events;

  int s = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event);
  if (s == -1)
    exit(1);  //("epoll_ctl");
}

int main(int argc, char **argv) {
  
  srand(time(NULL));

  ddhcp_config *config = (ddhcp_config*) malloc( sizeof(ddhcp_config) );
  config->node_id = 0xffffffffffffffff;
  config->block_size = 4;
  config->spare_blocks_needed = 1;
  config->claiming_blocks_amount = 0;

  inet_aton("10.0.0.0",&config->prefix);
  config->prefix_len = 27;
  printf("Prefix: %s/%i\n", inet_ntoa(config->prefix),config->prefix_len);
  config->number_of_blocks = pow(2, floor((32 - config->prefix_len - log(config->block_size))));
  config->spare_blocks_needed = config->number_of_blocks - 1;
  config->block_timeout = 30;
  config->tentative_timeout = 15;

  // DHCP
  config->dhcp_port = 67;

  INIT_LIST_HEAD(&(config->claiming_blocks).list);

  char* interface = "server0";
  char* interface_client = "client0";

  int c;
  while (( c = getopt(argc,argv,"i:")) != -1 ) {
    switch(c) {
      case 'i': 
        interface = optarg;
        break;
      case 'c':
        interface_client = optarg;
      default:
        printf("ARGC: %i",argc);
        abort ();
    }
  }
  printf("Using client interface: %s\n",interface_client);
  printf("Using interface: %s\n",interface);
  printf("Number of blocks: %i\n",config->number_of_blocks);

  // init block stucture
  ddhcp_block_init(&blocks,config);

  // init network and event loops
  // TODO
  if ( netsock_open(interface,interface_client,config) == -1 ) {
    return 1;
  }

  uint8_t* buffer = (uint8_t*) malloc( sizeof(uint8_t) * 1500 );
  struct ddhcp_mcast_packet packet;
  struct dhcp_packet dhcp_packet;
  int ret = 0, bytes = 0;

  int efd;
  int maxevents = 64;
  struct epoll_event *events;

  efd = epoll_create1(0);
  if (efd == -1) {
    perror("epoll_create");
    abort();
  }

  add_fd(efd, config->mcast_socket, EPOLLIN | EPOLLET);
  add_fd(efd, config->client_socket, EPOLLIN | EPOLLET);

  /* Buffer where events are returned */
  events = calloc(maxevents, sizeof(struct epoll_event));

  while(1) {
    int n;
    n = epoll_wait(efd, events, maxevents, 500);

    for( int i = 0; i < n; i++ ) {
      if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
        fprintf(stderr, "epoll error\n");
        close(events[i].data.fd);
      } else if (config->mcast_socket == events[i].data.fd) {
        bytes = read(config->mcast_socket, buffer, 1500);
        // TODO Error Handling
        ret = ntoh_mcast_packet(buffer,bytes, &packet);
        if ( ret == 0 ) {
          switch(packet.command) {
            case DHCPDISCOVER:

              ddhcp_block_process_claims(blocks,&packet,config);
              break;
            case 2:
              ddhcp_block_process_inquire(blocks,&packet,config);
            default:
              break;
          }
          free(packet.payload);
        } else {
          printf("%i",ret);
        }
        house_keeping( blocks, config );
      } else if ( config->client_socket == events[i].data.fd) {
        bytes = read(config->client_socket,buffer, 1500);
  
        // TODO Error Handling
        ret = ntoh_dhcp_packet(&dhcp_packet,buffer,bytes);
        if ( ret == 0 ) {
          int message_type = dhcp_packet_message_type(&dhcp_packet);
          ddhcp_block *block;
          switch( message_type ) {
            case DHCPDISCOVER:
              block = block_find_lease( blocks , config);
              if ( block != NULL ) {
                dhcp_discover(config->client_socket,&dhcp_packet,block->lease_block);
              } else {
                printf("Warning: No block with free lease!\n");
              }
              break;
            default:
              printf("Warning: Unknown DHCP Message Type: %i\n",message_type);
              break; 
          }
          if( dhcp_packet.options_len > 0 )
            free(dhcp_packet.options);
        }
      }
    }
    house_keeping( blocks, config );
  }
  // TODO free dhcp_leases
  free(events);
  free(blocks);
  free(buffer);
  return 0;
}

