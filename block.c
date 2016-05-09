#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include "dhcp.h"
#include "block.h"

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
    if ( block->state == DDHCP_OURS ) {
      dhcp_check_timeouts( block->lease_block );
    }
    block++;
  }
}
