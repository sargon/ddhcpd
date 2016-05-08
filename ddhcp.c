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
 }
 return NULL;
}

/**
 * Own a block, possibly after you have claimed it an amount of times.
 * This will also malloc and prepare a dhcp_lease_block inside the given block.
 */ 
int block_own( ddhcp_block *block ) {
  block->state = DDHCP_OURS;
  // TODO Have a preallocated list of dhcp_lease_blocks?
  uint32_t addr;
  memcpy(&addr,&block->subnet,sizeof(struct in_addr));
  addr = ntohl(addr);
  addr += block->index * block->subnet_len;
  addr = htonl(addr);
  return  dhcp_new_lease_block(&(block->lease_block),(struct in_addr*) &addr,block->subnet_len);
}

ddhcp_block* block_find_free(ddhcp_block *blocks, ddhcp_config *config) {
  ddhcp_block *block = blocks;

  ddhcp_block_list free_blocks, *tmp;
  INIT_LIST_HEAD(&free_blocks.list);
  int num_free_blocks = 0;
  for( int i = 0 ; i < config->number_of_blocks; i++ ) {
    if( block->state == DDHCP_FREE ) {
      tmp = (ddhcp_block_list*) malloc( sizeof ( ddhcp_block_list ) );
      tmp->block = block;
      list_add_tail((&tmp->list), &(free_blocks.list) );
      num_free_blocks++;
    }
    block++;
  }

  ddhcp_block *random_free = NULL;
  int r = -1;
  if ( num_free_blocks > 0 ) {
    r = rand() % num_free_blocks;
  }
  
  struct list_head *pos, *q;
  list_for_each_safe(pos, q, &free_blocks.list){
     tmp = list_entry(pos, ddhcp_block_list, list);
     block = tmp->block;
     list_del(pos);
     free(tmp);
     if(r == 0) random_free = block;
     r--;
  }
  
  return random_free;
}

int block_claim( ddhcp_block *blocks, int num_blocks , ddhcp_config *config ) {
  // Handle blocks already in claiming prozess
  struct list_head *pos, *q;
  int now = time(NULL);
  list_for_each_safe(pos,q,&(config->claiming_blocks).list) {
      ddhcp_block_list  *tmp = list_entry(pos, ddhcp_block_list, list);
      ddhcp_block *block = tmp->block;
      if ( block->claiming_counts == 3 ) {
          block_own(block);
          printf("Claiming Block: %i\n",block->index);
          num_blocks--;
          list_del(pos);
          config->claiming_blocks_amount--;
          free(tmp);
      } else if ( block->state != DDHCP_CLAIMING ) {
          list_del(pos);
          config->claiming_blocks_amount--;
          free(tmp);
      } 
  }

  // Do we still need more, then lets find some.
  if ( (unsigned int) num_blocks > config->claiming_blocks_amount ) {
    // find num_blocks - config->claiming_blocks_amount free blocks
    int needed_blocks = num_blocks - config->claiming_blocks_amount;
    for ( int i = 0 ; i < needed_blocks ; i++ ) {
      ddhcp_block *block = block_find_free( blocks, config );
      if ( block != NULL ) {
        ddhcp_block_list* list = (ddhcp_block_list*) malloc( sizeof ( ddhcp_block_list ) );
        block->state = DDHCP_CLAIMING;
        block->claiming_counts = 0;
        block->timeout = now + config->tentative_timeout;
        list->block = block;
        list_add_tail(&(list->list), &(config->claiming_blocks.list));
        config->claiming_blocks_amount++;
      } else { 
        // Can't find any new once, what to do?
        printf("Can't find free block\n");
      }
    }
  }

  // Send claim message for all blocks in claiming process.
  struct ddhcp_mcast_packet packet;
  packet.node_id = config->node_id;
  memcpy(&(packet.prefix),&config->prefix,sizeof(struct in_addr));
  packet.prefix_len = config->prefix_len;
  packet.blocksize = config->block_size;
  packet.command = 2;
  packet.count = config->claiming_blocks_amount;
  packet.payload = (struct ddhcp_payload*) malloc( sizeof(struct ddhcp_payload) * config->claiming_blocks_amount);
  int index = 0;
  list_for_each(pos,&(config->claiming_blocks).list) {
    ddhcp_block_list  *tmp = list_entry(pos, ddhcp_block_list, list);
    ddhcp_block *block = tmp->block;
    block->claiming_counts++;
    packet.payload[index].block_index = block->index;
    packet.payload[index].timeout = 0;
    packet.payload[index].reserved = 0;
    index++;
  }
  if( packet.count > 0 ) {
    send_packet_mcast( &packet , config->mcast_socket, config->mcast_scope_id );
  } 
  free(packet.payload);
  return 0;
}

int block_num_free_leases( ddhcp_block *block, ddhcp_config *config ) {
  int free_leases = 0;
  for ( int i = 0 ; i < config->number_of_blocks ; i++ ) {
    if ( block->state == DDHCP_OURS ) {
      free_leases += dhcp_num_free( block->lease_block );
    }
    block++;
  }
  return free_leases;
}

/**
 *  Update the timeout of claimed blocks and then send packets to
 *  distribute the continuations of that claim. 
 *
 *  Due to fragmented timeouts this packet may send 2 times more packets 
 *  than optimal.
 */
void block_update_claims( ddhcp_block *blocks, ddhcp_config *config ) {
  int our_blocks = 0;  
  ddhcp_block *block = blocks;
  int now = time(NULL);
  int timeout_half = floor( (double) config->block_timeout / 2 );
  for ( int i = 0 ; i < config->number_of_blocks ; i++ ) {
    if ( block->state == DDHCP_OURS && block->timeout < now + timeout_half ) {
      our_blocks++;
    }
    block++;
  }

  if( our_blocks == 0 ) return;

  struct ddhcp_mcast_packet packet;
  packet.node_id = config->node_id;
  memcpy(&packet.prefix,&config->prefix,sizeof(struct in_addr));
  packet.prefix_len = config->prefix_len;
  packet.blocksize = config->block_size;
  packet.command = 1;
  packet.count = our_blocks;
  packet.payload = (struct ddhcp_payload*) malloc(sizeof(struct ddhcp_payload) * our_blocks);
  int index = 0;
  block = blocks;
  for ( int i = 0 ; i < config->number_of_blocks ; i++ ) {
    if ( block->state == DDHCP_OURS && block->timeout < now + timeout_half ) {
      packet.payload[index].block_index = block->index;
      packet.payload[index].timeout     = config->block_timeout;
      packet.payload[index].reserved    = 0;
      index++;
      block->timeout = now + config->block_timeout;
    }
    block++;
  }
  if( packet.count > 0 ) {
    send_packet_mcast( &packet , config->mcast_socket, config->mcast_scope_id );
  } 
  free(packet.payload);
}

void block_check_timeouts( ddhcp_block *blocks, ddhcp_config *config ) {
  ddhcp_block *block = blocks;
  int now = time(NULL);
  for ( int i = 0 ; i < config->number_of_blocks ; i++ ) {
    if ( block->timeout < now && block->state != DDHCP_BLOCKED && block->state != DDHCP_FREE ) {
      printf("Freeing Block: %i\n",block->index);
      block->state = DDHCP_FREE;
      block->timeout = now + config->block_timeout;
    } 
    block++;
  }
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

