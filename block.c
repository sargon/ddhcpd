#include "block.h"

#include <math.h>

#include "dhcp.h"
#include "logger.h"

int block_alloc(ddhcp_block* block) {
  DEBUG("block_alloc(block)\n");
  block->addresses = (struct dhcp_lease*) calloc(sizeof(struct dhcp_lease), block->subnet_len);

  if (block->addresses == NULL) {
    return 1;
  }

  for (unsigned int index = 0; index < block->subnet_len; index++) {
    block->addresses[index].state = FREE;
    block->addresses[index].lease_end = 0;
  }

  return 0;
}

int block_own(ddhcp_block* block) {
  if (block_alloc(block)) {
    return 1;
  } else {
    block->state = DDHCP_OURS;
    return 0;
  }
}

void block_free(ddhcp_block* block) {
  DEBUG("block_free(%i)\n", block->index);

  if (block->state == DDHCP_OURS) {
    block->state = DDHCP_FREE;
  }

  if (block->addresses) {
    DEBUG("Free DHCP leases for Block %i\n", block->index);
    free(block->addresses);
    block->addresses = NULL;
  }
}

ddhcp_block* block_find_free(ddhcp_block* blocks, ddhcp_config* config) {
  DEBUG("block_find_free(blocks,config)\n");
  ddhcp_block* block = blocks;

  ddhcp_block_list free_blocks, *tmp;
  INIT_LIST_HEAD(&free_blocks.list);
  uint32_t num_free_blocks = 0;

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_FREE) {
      tmp = (ddhcp_block_list*) malloc(sizeof(ddhcp_block_list));
      tmp->block = block;
      list_add_tail((&tmp->list), &(free_blocks.list));
      num_free_blocks++;
    }

    block++;
  }

  DEBUG("block_find_free(...): found %i free blocks\n", num_free_blocks);

  ddhcp_block* random_free = NULL;
  int r = -1;

  if (num_free_blocks > 0) {
    r = rand() % num_free_blocks;
  } else {
    DEBUG("block_find_free(...) -> no free block found\n");
    return NULL;
  }

  struct list_head* pos, *q;

  list_for_each_safe(pos, q, &free_blocks.list) {
    tmp = list_entry(pos, ddhcp_block_list, list);
    block = tmp->block;
    list_del(pos);
    free(tmp);

    if (r == 0) {
      random_free = block;
    }

    r--;
  }

  DEBUG("block_find_free(...)-> block %i\n", random_free->index);
  return random_free;
}

int block_claim(ddhcp_block* blocks, int num_blocks, ddhcp_config* config) {
  DEBUG("block_claim(blocks, %i, config)\n", num_blocks);

  // Handle blocks already in claiming prozess
  struct list_head* pos, *q;
  time_t now = time(NULL);

  list_for_each_safe(pos, q, &(config->claiming_blocks).list) {
    ddhcp_block_list* tmp = list_entry(pos, ddhcp_block_list, list);
    ddhcp_block* block = tmp->block;

    if (block->claiming_counts == 3) {
      block_own(block);

      // TODO Error Handling

      //Reduce number of blocks we need to claim
      num_blocks--;

      INFO("Block %i claimed after 3 claims.\n", block->index);
      list_del(pos);
      config->claiming_blocks_amount--;
      free(tmp);
    } else if (block->state != DDHCP_CLAIMING) {
      DEBUG("block_claim(...): block %i is no longer marked as claiming\n", block->index);
      list_del(pos);
      config->claiming_blocks_amount--;
      free(tmp);
    }
  }

  // Do we still need more, then lets find some.
  if ((unsigned int) num_blocks > config->claiming_blocks_amount) {
    // find num_blocks - config->claiming_blocks_amount free blocks
    int needed_blocks = num_blocks - config->claiming_blocks_amount;

    for (int i = 0 ; i < needed_blocks ; i++) {
      ddhcp_block* block = block_find_free(blocks, config);

      if (block != NULL) {
        ddhcp_block_list* list = (ddhcp_block_list*) malloc(sizeof(ddhcp_block_list));

        // TODO Error Handling

        block->state = DDHCP_CLAIMING;
        block->claiming_counts = 0;
        block->timeout = now + config->tentative_timeout;
        list->block = block;
        list_add_tail(&(list->list), &(config->claiming_blocks.list));
        config->claiming_blocks_amount++;
      } else {
        // We are short on free blocks in the network.
        WARNING("Warning: Network has no free blocks left!\n");
        // TODO In a future version we could start to forward DHCP requests
        //      to other servers.
      }
    }
  }

  // TODO Sort blocks in claiming process by number of claims already processed.

  // TODO If we have more blocks in claiming process than we need, drop the tail
  //      of blocks for which we had less claim announcements.

  if (config->claiming_blocks_amount < 1) {
    DEBUG("block_claim(...)-> No blocks need claiming.\n");
    return 0;
  }

  // Send claim message for all blocks in claiming process.
  struct ddhcp_mcast_packet* packet = new_ddhcp_packet(DDHCP_MSG_INQUIRE, config);
  packet->count = config->claiming_blocks_amount;

  packet->payload = (struct ddhcp_payload*) calloc(sizeof(struct ddhcp_payload), config->claiming_blocks_amount);
  // TODO Check we actually got the memory

  int index = 0;
  list_for_each(pos, &(config->claiming_blocks).list) {
    ddhcp_block_list*  tmp = list_entry(pos, ddhcp_block_list, list);
    ddhcp_block* block = tmp->block;
    block->claiming_counts++;
    packet->payload[index].block_index = block->index;
    packet->payload[index].timeout = 0;
    packet->payload[index].reserved = 0;
    index++;
  }

  send_packet_mcast(packet, config->mcast_socket, config->mcast_scope_id);

  free(packet->payload);
  free(packet);
  return 0;
}

int block_num_free_leases(ddhcp_block* block, ddhcp_config* config) {
  DEBUG("block_num_free_leases(blocks, config)\n");
  int free_leases = 0;
#if LOG_LEVEL >= LOG_DEBUG
  int num_blocks = 0;
#endif

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_OURS) {
      free_leases += dhcp_num_free(block);
#if LOG_LEVEL >= LOG_DEBUG
      num_blocks++;
#endif
    }

    block++;
  }

  DEBUG("block_num_free_leases(...)-> Found %i free dhcp leases in OUR (%i) blocks\n", free_leases, num_blocks);
  return free_leases;
}

ddhcp_block* block_find_free_leases(ddhcp_block* block, ddhcp_config* config) {
  DEBUG("block_find_free_leases(blocks,config)\n");
  ddhcp_block* selected = NULL;
  uint32_t selected_free_leases = config->block_size + 1;

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_OURS) {
      uint32_t free_leases = dhcp_num_free(block);

      if (free_leases > 0) {
        if (free_leases < selected_free_leases) {
          selected = block;
        }
      }
    }
    block++;
  }
  #if LOG_LEVEL >= LOG_DEBUG
  if ( selected != NULL ) {
    DEBUG("block_find_free_leases(blocks,config) -> Block %i selected\n",selected->index);
  } else {
    DEBUG("block_find_free_leases(blocks,config) -> No block found!\n");
  }
  #endif
  return selected;
}

void block_update_claims(ddhcp_block* blocks, int blocks_needed, ddhcp_config* config) {
  DEBUG("block_update_claims(blocks, %i, config)\n", blocks_needed);
  unsigned int our_blocks = 0;
  ddhcp_block* block = blocks;
  time_t now = time(NULL);
  int timeout_half = floor((double) config->block_timeout / 2);
  int blocks_needed_tmp = blocks_needed;

  // TODO Use a linked list instead of processing the block list twice.
  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_OURS && block->timeout < now + timeout_half) {
      if (blocks_needed_tmp < 0 && dhcp_num_free(block) == config->block_size) {
        DEBUG("block_update_claims(...): block %i no longer needed\n", block->index);
        blocks_needed_tmp--;
        block_free(block);
      } else {
        our_blocks++;
      }
    }

    block++;
  }

  if (our_blocks == 0) {
    DEBUG("block_update_claims(...)-> No blocks need claim update.\n");
    return;
  }

  struct ddhcp_mcast_packet* packet = new_ddhcp_packet(DDHCP_MSG_UPDATECLAIM, config);

  packet->count = our_blocks;

  packet->payload = (struct ddhcp_payload*) calloc(sizeof(struct ddhcp_payload), our_blocks);

  // TODO Check we actually got the memory

  int index = 0;

  block = blocks;

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_OURS && block->timeout < now + timeout_half) {
      packet->payload[index].block_index = block->index;
      packet->payload[index].timeout     = config->block_timeout;
      packet->payload[index].reserved    = 0;
      index++;
      block->timeout = now + config->block_timeout;
      DEBUG("block_update_claims(...): update claim for block %i\n", block->index);
    }

    block++;
  }

  send_packet_mcast(packet, config->mcast_socket, config->mcast_scope_id);

  free(packet->payload);
  free(packet);
}

void block_check_timeouts(ddhcp_block* blocks, ddhcp_config* config) {
  DEBUG("block_check_timeouts(blocks, config)\n");
  ddhcp_block* block = blocks;
  time_t now = time(NULL);

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->timeout < now && block->state != DDHCP_BLOCKED && block->state != DDHCP_FREE) {
      INFO("Block %i FREE throught timeout.\n", block->index);
      block_free(block);
    }

    if (block->state == DDHCP_OURS) {
      dhcp_check_timeouts(block);
    } else if (block->addresses != NULL) {
      int free_leases = dhcp_check_timeouts(block);

      if (free_leases == block->subnet_len) {
        block_free(block);
      }
    }

    block++;
  }
}

void block_free_claims(ddhcp_config* config) {
  if (list_empty(&config->claiming_blocks.list)) {
    return;
  }

  struct list_head* pos, *q;

  list_for_each_safe(pos, q, &(config->claiming_blocks).list) {
    ddhcp_block_list* tmp = list_entry(pos, ddhcp_block_list, list);
    list_del(pos);
    free(tmp);
  }
}

void block_show_status(int fd, ddhcp_block* blocks,  ddhcp_config* config) {
  ddhcp_block* block = blocks;
  dprintf(fd, "index\tstate\towner\t\tclaim_count\tleases\ttimeout\n");

  time_t now = time(NULL);

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    uint32_t free_leases = 0;

    if (block->addresses != NULL) {
      free_leases = dhcp_num_free(block);
    }

    char node_id[17]; 
    for( uint32_t j = 0; j < 8; j++) {
      sprintf(node_id + 2 * j,"%02X",block->node_id[j]);
    }
    node_id[16] = '\0';
    
    char leases[10];
    if ( block->addresses != NULL ) {
      sprintf(leases,"%u",config->block_size - free_leases);
    } else {
      leases[0] = '-';
      leases[1] = '\0';
    }

    time_t timeout = 0;
    if ( block->timeout > now ) {
      timeout = block->timeout - now;
    }

    dprintf(fd, "%i\t%i\t%s\t%u\t%s\t%lu\n", block->index, block->state, node_id, block->claiming_counts, leases, timeout);
    block++;
  }
}
