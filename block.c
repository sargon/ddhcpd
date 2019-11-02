#include "block.h"

#include <errno.h>
#include <math.h>

#include "dhcp.h"
#include "logger.h"
#include "statistics.h"
#include "tools.h"

// TODO define sane value
#define UPDATE_CLAIM_MAX_BLOCKS 32

int block_alloc(ddhcp_block* block) {
  DEBUG("block_alloc(block)\n");

  if (!block) {
    WARNING("block_alloc(...): No block given to initialize\n");
    return 1;
  }

  // Do not allocate memory and initialise, when the block is already allocated
  if (block->addresses) {
    return 0;
  }

  block->addresses = (struct dhcp_lease*) calloc(sizeof(struct dhcp_lease), block->subnet_len);

  if (!block->addresses) {
    WARNING("block_alloc(...): Failed to allocate memory for lease management on block %i\n", block->index);
    return 1;
  }

  for (unsigned int index = 0; index < block->subnet_len; index++) {
    block->addresses[index].state = FREE;
    block->addresses[index].lease_end = 0;
  }

  return 0;
}

int block_own(ddhcp_block* block, ddhcp_config* config) {
  if (!block) {
    WARNING("block_own(...): No block given to own\n");
    return 1;
  }

  if (block_alloc(block)) {
    WARNING("block_own(...): Failed to initialize block %i for owning\n", block->index);
    return 1;
  }

  block->state = DDHCP_OURS;
  block->first_claimed = time(NULL);
  NODE_ID_CP(&block->node_id, &config->node_id);
  return 0;
}

void block_free(ddhcp_block* block) {
  DEBUG("block_free(%i)\n", block->index);

  if (block->state != DDHCP_BLOCKED) {
    NODE_ID_CLEAR(&block->node_id);
    block->state = DDHCP_FREE;
  }

  if (block->addresses) {
    DEBUG("block_free(%i): Freeing DHCP leases\n", block->index);
    free(block->addresses);
    block->addresses = NULL;
  }
}

ddhcp_block* block_find_free(ddhcp_config* config) {
  DEBUG("block_find_free(config)\n");
  ddhcp_block* block = config->blocks;

  ddhcp_block_list free_blocks;
  INIT_LIST_HEAD(&free_blocks);
  uint32_t num_free_blocks = 0;

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_FREE) {
      list_add_tail((&block->tmp_list), &free_blocks);
      num_free_blocks++;
    }

    block++;
  }

  DEBUG("block_find_free(...): found %i free blocks\n", num_free_blocks);

  ddhcp_block* random_free = NULL;
  uint32_t r = ~0u;

  if (num_free_blocks > 0) {
    r = (uint32_t)rand() % num_free_blocks;
  } else {
    DEBUG("block_find_free(...): no free block found\n");
    return NULL;
  }

  struct list_head* pos, *q;

  list_for_each_safe(pos, q, &free_blocks) {
    block = list_entry(pos, ddhcp_block, tmp_list);
    list_del(pos);

    if (r == 0) {
      random_free = block;
      break;
    }

    r--;
  }

  if (random_free) {
    DEBUG("block_find_free(...): found block %i\n", random_free->index);
  } else {
    WARNING("block_find_free(...): no free block found\n");
  }

  return random_free;
}

int block_claim(int32_t num_blocks, ddhcp_config* config) {
  DEBUG("block_claim(count:%i, config)\n", num_blocks);

  // Handle blocks already in claiming prozess
  struct list_head* pos, *q;
  time_t now = time(NULL);

  list_for_each_safe(pos, q, &config->claiming_blocks) {
    ddhcp_block* block = list_entry(pos, ddhcp_block, claim_list);

    if (block->claiming_counts == 3) {
      block_own(block, config);

      // TODO Error Handling

      //Reduce number of blocks we need to claim
      num_blocks--;

      INFO("block_claim(...): block %i claimed after 3 claims.\n", block->index);
      list_del(pos);
      config->claiming_blocks_amount--;
    } else if (block->state != DDHCP_CLAIMING) {
      DEBUG("block_claim(...): block %i is no longer marked for claiming\n", block->index);
      list_del(pos);
      config->claiming_blocks_amount--;
    }
  }

  // Do we still need more, then lets find some.
  if (num_blocks > config->claiming_blocks_amount) {
    // find num_blocks - config->claiming_blocks_amount free blocks
    uint32_t needed_blocks = (uint32_t) num_blocks - config->claiming_blocks_amount;

    for (uint32_t i = 0; i < needed_blocks; i++) {
      ddhcp_block* block = block_find_free(config);

      if (block) {
        block->state = DDHCP_CLAIMING;
        block->claiming_counts = 0;
        block->timeout = now + config->tentative_timeout;
        list_add_tail(&(block->claim_list), &config->claiming_blocks);
        config->claiming_blocks_amount++;
      } else {
        // We are short on free blocks in the network.
        WARNING("block_claim(...): Network has no free blocks left!\n");
        // TODO In a future version we could start to forward DHCP requests
        //      to other servers.
      }
    }
  }

  // TODO Sort blocks in claiming process by number of claims already processed.

  // TODO If we have more blocks in claiming process than we need, drop the tail
  //      of blocks for which we had less claim announcements.

  if (config->claiming_blocks_amount < 1) {
    DEBUG("block_claim(...): No blocks need claiming.\n");
    return 0;
  }

  // Send claim message for all blocks in claiming process.
  struct ddhcp_mcast_packet* packet = new_ddhcp_packet(DDHCP_MSG_INQUIRE, config);

  if (!packet) {
    WARNING("block_claim(...): Failed to allocate ddhcpd mcast packet.\n");
    return -ENOMEM;
  }

  packet->count = config->claiming_blocks_amount;

  packet->payload = (struct ddhcp_payload*) calloc(sizeof(struct ddhcp_payload), config->claiming_blocks_amount);

  if (!packet->payload) {
    free(packet);
    WARNING("block_claim(...): Failed to allocate ddhcpd mcast packet payload.\n");
    return -ENOMEM;
  }

  int index = 0;
  ddhcp_block* block;

  list_for_each_entry(block, &config->claiming_blocks, claim_list) {
    packet->payload[index].block_index = block->index;
    packet->payload[index].timeout = 0;
    packet->payload[index].reserved = 0;
    index++;
  }

  statistics_record(config, STAT_MCAST_SEND_PKG, 1);
  statistics_record(config, STAT_MCAST_SEND_INQUIRE, 1);
  ssize_t bytes_send = send_packet_mcast(packet, config->mcast_socket, config->mcast_scope_id);
  statistics_record(config, STAT_MCAST_SEND_BYTE, (long int) bytes_send);

  if (bytes_send > 0) {
    list_for_each_entry(block, &config->claiming_blocks, claim_list) {
      block->claiming_counts++;
    }
  } else {
    DEBUG("block_claim(...): Send failed, no updates made.\n");
  }

  free(packet->payload);
  free(packet);
  return 0;
}

uint32_t block_num_free_leases(ddhcp_config* config) {
  DEBUG("block_num_free_leases(config)\n");

  ddhcp_block* block = config->blocks;
  uint32_t free_leases = 0;
#if LOG_LEVEL_LIMIT >= LOG_DEBUG
  uint32_t num_blocks = 0;
#endif

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_OURS) {
      free_leases += dhcp_num_free(block);
#if LOG_LEVEL_LIMIT >= LOG_DEBUG
      num_blocks++;
#endif
    }

    block++;
  }

  DEBUG("block_num_free_leases(...): Found %lu free DHCP leases in OUR (%lu) blocks\n", free_leases, num_blocks);
  return free_leases;
}

ddhcp_block* block_find_free_leases(ddhcp_config* config) {
  DEBUG("block_find_free_leases(config)\n");

  ddhcp_block* block = config->blocks;
  ddhcp_block* selected = NULL;

  // TODO Change strategy, select the oldest block with free leases
  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_OURS) {
      if (dhcp_has_free(block)) {
        if (selected) {
          if (selected->first_claimed > block->first_claimed) {
            selected = block;
          }
        } else {
          selected = block;
        }
      }
    }

    block++;
  }

#if LOG_LEVEL_LIMIT >= LOG_DEBUG

  if (selected) {
    DEBUG("block_find_free_leases(...): Block %i selected\n", selected->index);
  } else {
    DEBUG("block_find_free_leases(...): No block found!\n");
  }

#endif

  return selected;
}

void block_drop_unused(ddhcp_config* config) {
  DEBUG("block_drop_unsued(config)\n");
  ddhcp_block* block = config->blocks;
  ddhcp_block* freeable_block = NULL;

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_OURS) {
      if (dhcp_num_free(block) == config->block_size) {
        DEBUG("block_drop unused(...): block %i is unused.\n", block->index);

        if (freeable_block) {
          if (freeable_block->first_claimed < block->first_claimed) {
            freeable_block = block;
          }
        } else {
          freeable_block = block;
        }
      }
    }

    block++;
  }

  if (freeable_block) {
    DEBUG("block_drop_unused(...): free block %i.\n", freeable_block->index);
    block_free(freeable_block);
  }
}

void _block_update_claim_send(struct ddhcp_mcast_packet* packet, time_t new_block_timeout, ddhcp_config* config) {
  DEBUG("block_update_claims_send(packet:%i,%l,config)\n",packet->count,new_block_timeout);
  statistics_record(config, STAT_MCAST_SEND_PKG, 1);
  statistics_record(config, STAT_MCAST_SEND_UPDATECLAIM, 1);
  ssize_t bytes_send = send_packet_mcast(packet, config->mcast_socket, config->mcast_scope_id);
  statistics_record(config, STAT_MCAST_SEND_BYTE, (long int) bytes_send);
  // TODO? Stat the number of blocks reclaimed.

  if (bytes_send > 0) {
    // Update the timeout value of all contained blocks
    // iff the packet has been transmitted
    for (uint8_t i = 0; i < packet->count; i++) {
      uint32_t index = packet->payload[i].block_index;
      DEBUG("block_update_claims_send(...): updated claim for block %i\n", index);
      config->blocks[index].timeout = new_block_timeout;
    }
  } else {
    DEBUG("block_update_claims_send(...): Send failed, no updates made.\n");
  }
}

void block_update_claims(ddhcp_config* config) {
  DEBUG("block_update_claims(config)\n");
  uint32_t our_blocks = 0;
  ddhcp_block* block = config->blocks;
  time_t now = time(NULL);
  time_t timeout_factor = now + config->block_timeout - (time_t)(config->block_timeout / config->block_refresh_factor);

  // Determine if we need to run a full update claim run
  // we run through the list until we see one block which needs update.
  // Running a full update claims (see below) is much more expensive
  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_OURS && block->timeout < timeout_factor) {
      our_blocks++;
      break;
    }

    block++;
  }

  if (our_blocks == 0) {
    DEBUG("block_update_claims(...): No blocks need claim updates.\n");
    return;
  }

  struct ddhcp_mcast_packet* packet = new_ddhcp_packet(DDHCP_MSG_UPDATECLAIM, config);

  if (!packet) {
    WARNING("block_update_claims(...): Failed to allocate ddhcpd mcast packet.\n");
    return;
  }

  // Aggressively group blocks into packets, send packet iff
  // at least one block in a packet is below the baseline.

  packet->payload = (struct ddhcp_payload*) calloc(sizeof(struct ddhcp_payload), UPDATE_CLAIM_MAX_BLOCKS);

  if (!packet->payload) {
    WARNING("block_update_claims(...): Failed to allocate ddhcpd packet payload.\n");
    free(packet);
    return;
  }

  block = config->blocks;
  uint8_t send_packet = 0;
  uint8_t index = 0;
  time_t new_block_timeout = now + config->block_timeout;

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_OURS) {

      if (block->timeout < timeout_factor) {
        DEBUG("block_update_claims(...): update claim for block %i needed\n", block->index);
        send_packet = 1;
      }

      packet->payload[index].block_index = block->index;
      packet->payload[index].timeout     = config->block_timeout;
      packet->payload[index].reserved    = 0;

      index++;

      if (index == UPDATE_CLAIM_MAX_BLOCKS) {
        if (send_packet) {
          packet->count = index;
          send_packet = 0;
          _block_update_claim_send(packet, new_block_timeout, config);
        }

        index = 0;
      }
    }

    block++;
  }

  if (send_packet) {
    packet->count = index;
    _block_update_claim_send(packet, new_block_timeout, config);
  }

  free(packet->payload);
  free(packet);
}

void block_check_timeouts(ddhcp_config* config) {
  DEBUG("block_check_timeouts(config)\n");
  ddhcp_block* block = config->blocks;
  time_t now = time(NULL);

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->timeout < now && block->state != DDHCP_BLOCKED && block->state != DDHCP_FREE) {
      INFO("block_check_timeouts(...): Block %i FREE through timeout.\n", block->index);
      block_free(block);
    }

    if (block->state == DDHCP_OURS) {
      dhcp_check_timeouts(block);
    } else if (block->addresses) {
      int free_leases = dhcp_check_timeouts(block);

      if (free_leases == block->subnet_len) {
        block_free(block);
      }
    }

    block++;
  }
}

void block_show_status(int fd, ddhcp_config* config) {
  ddhcp_block* block = config->blocks;
  dprintf(fd, "block size/number\t%u/%u \n", config->block_size, config->number_of_blocks);
  dprintf(fd, "      tentative timeout\t%u\n", config->tentative_timeout);
  dprintf(fd, "      timeout\t%u\n", config->block_timeout);
  dprintf(fd, "      refresh factor\t%u\n", config->block_refresh_factor);
  dprintf(fd, "      spare leases needed\t%u\n", config->spare_leases_needed);
  dprintf(fd, "      network: %s/%i \n", inet_ntoa(config->prefix), config->prefix_len);

  char node_id[17];

  for (uint32_t j = 0; j < 8; j++) {
    sprintf(node_id + 2 * j, "%02X", config->node_id[j]);
  }

  node_id[16] = '\0';

  dprintf(fd, "node id\t%s\n", node_id);

  dprintf(fd, "ddhcp blocks\n");
  dprintf(fd, "index\tstate\towner\t\t\tclaim\tleases\ttimeout\n");

  time_t now = time(NULL);

  uint32_t num_reserved_blocks = 0;

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    uint32_t free_leases = 0;
    uint32_t offered_leases = 0;

    if (block->addresses) {
      free_leases = dhcp_num_free(block);
      offered_leases = dhcp_num_offered(block);
    }

    for (uint32_t j = 0; j < 8; j++) {
      sprintf(node_id + 2 * j, "%02X", block->node_id[j]);
    }

    node_id[16] = '\0';

    char leases[16];

    if (block->addresses) {
      snprintf(leases, sizeof(leases), "%u/%u", offered_leases, config->block_size - free_leases - offered_leases);
    } else {
      leases[0] = '-';
      leases[1] = '\0';
    }

    time_t timeout = block->timeout - now;

    if (timeout > 0) {
      num_reserved_blocks++;
      dprintf(fd, "%i\t%i\t%s\t%u\t%s\t%lu\n", block->index, block->state, node_id, block->claiming_counts, leases, timeout);
    }

    block++;
  }

  dprintf(fd, "\nblocks in use: %i\n", num_reserved_blocks);
}
