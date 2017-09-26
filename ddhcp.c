#include <assert.h>

#include "ddhcp.h"
#include "dhcp.h"
#include "logger.h"
#include "tools.h"

int ddhcp_block_init(struct ddhcp_block** blocks, ddhcp_config* config) {
  assert(blocks);

  if (config->number_of_blocks < 1) {
    FATAL("ddhcp_block_init(...)-> Need at least 1 blocks to be configured\n");
    return 1;
  }

  DEBUG("ddhcp_block_init( blocks, config)\n");
  *blocks = (struct ddhcp_block*) calloc(sizeof(struct ddhcp_block), config->number_of_blocks);

  if (! *blocks) {
    FATAL("ddhcp_block_init(...)-> Can't allocate memory for block structure\n");
    return 1;
  }

  time_t now = time(NULL);

  // TODO Maybe we should allocate number_of_blocks dhcp_lease_blocks previous
  //      and assign one here instead of NULL. Performance boost, Memory defrag?
  struct ddhcp_block* block = *blocks;

  for (uint32_t index = 0; index < config->number_of_blocks; index++) {
    block->index = index;
    block->state = DDHCP_FREE;
    addr_add(&config->prefix, &block->subnet, index * config->block_size);
    block->subnet_len = config->block_size;
    memset(&block->owner_address, 0, sizeof(struct in6_addr));
    block->timeout = now + config->block_timeout;
    block->claiming_counts = 0;
    block->addresses = NULL;
    block++;
  }

  return 0;
}

void ddhcp_block_process_claims(struct ddhcp_block* blocks, struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_block_process_claims( blocks, packet, config )\n");
  assert(packet->command == 1);
  time_t now = time(NULL);

  for (unsigned int i = 0; i < packet->count; i++) {
    struct ddhcp_payload* claim = &packet->payload[i];
    uint32_t block_index = claim->block_index;

    if (block_index >= config->number_of_blocks) {
      WARNING("ddhcp_block_process_claims(...): Malformed block number\n");
      continue;
    }

    if (blocks[block_index].state == DDHCP_OURS) {
      INFO("ddhcp_block_process_claims(...): node 0x%02x%02x%02x%02x%02x%02x%02x%02x claims our block %i\n", HEX_NODE_ID(packet->node_id), block_index);
      // TODO Decide when and if we reclaim this block
      //      Which node has more leases in this block, ..., who has the better node_id.
    } else {
      // TODO Save the connection details for the claiming node, so we can contact him, for dhcp actions.
      blocks[block_index].state = DDHCP_CLAIMED;
      blocks[block_index].timeout = now + claim->timeout;
      char ipv6_sender[INET6_ADDRSTRLEN];
      memcpy(&blocks[block_index].owner_address, &packet->sender->sin6_addr, sizeof(struct in6_addr));
      DEBUG("Register block to %s\n",
            inet_ntop(AF_INET6, &blocks[block_index].owner_address, ipv6_sender, INET6_ADDRSTRLEN));
      INFO("ddhcp_block_process_claims(...): node 0x%02x%02x%02x%02x%02x%02x%02x%02x claims block %i with ttl: %i\n", HEX_NODE_ID(packet->node_id), block_index, claim->timeout);
    }
  }
}

void ddhcp_block_process_inquire(struct ddhcp_block* blocks, struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_block_process_inquire( blocks, packet, config )\n");
  assert(packet->command == 2);
  time_t now = time(NULL);

  for (unsigned int i = 0; i < packet->count; i++) {
    struct ddhcp_payload* tmp = &packet->payload[i];

    if (tmp->block_index >= config->number_of_blocks) {
      WARNING("ddhcp_block_process_inquire(...): Malformed block number\n");
      continue;
    }

    INFO("ddhcp_block_process_inquire(...): node 0x%02x%02x%02x%02x%02x%02x%02x%02x inquires block %i\n", HEX_NODE_ID(packet->node_id), tmp->block_index);

    if (blocks[tmp->block_index].state == DDHCP_OURS) {
      // Update Claims
      INFO("ddhcp_block_process_inquire(...): block %i is ours notify network", tmp->block_index);
      blocks[tmp->block_index].timeout = 0;
      block_update_claims(blocks, 0, config);
    } else if (blocks[tmp->block_index].state == DDHCP_CLAIMING) {
      INFO("ddhcp_block_process_inquire(...): we are interested in block %i also\n", tmp->block_index);

      // QUESTION Why do we need multiple states for the same process?
      if (NODE_ID_CMP(packet->node_id, config->node_id) > 0) {
        INFO("ddhcp_block_process_inquire(...): .. but other node wins.\n");
        blocks[tmp->block_index].state = DDHCP_TENTATIVE;
        blocks[tmp->block_index].timeout = now + config->tentative_timeout;
      }

      // otherwise keep inquiring, the other node should see our inquires and step back.
    } else {
      INFO("ddhcp_block_process_inquire(...): set block %i to tentative \n", tmp->block_index);
      blocks[tmp->block_index].state = DDHCP_TENTATIVE;
      blocks[tmp->block_index].timeout = now + config->tentative_timeout;
    }
  }
}

void ddhcp_dhcp_renewlease(struct ddhcp_block* blocks, struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_dhcp_renewlease(%li,%li,%li)\n", (long int) &blocks, (long int) &packet, (long int) &config);
  int ret = dhcp_rhdl_request(&(packet->address), blocks, config);

  // We are reusing the packet here
  if (! ret) {
    DEBUG("ddhcp_dhcp_renewlease( ... ): %i ACK\n", ret);
    packet->command = DDHCP_MSG_LEASEACK;
  } else {
    DEBUG("ddhcp_dhcp_renewlease( ... ): %i NAK\n", ret);
    packet->command = DDHCP_MSG_LEASENAK;

    // TODO Can we hand over the block?
  }

  send_packet_direct(packet, &packet->sender->sin6_addr, config->server_socket, config->mcast_scope_id);
}

void ddhcp_dhcp_leaseack(struct ddhcp_block* blocks, struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  // Stub functions
  DEBUG("ddhcp_dhcp_leaseack(%li,%li,%li)\n", (long int) &blocks, (long int) &packet, (long int) &config);
}

void ddhcp_dhcp_leasenak(struct ddhcp_block* blocks, struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  // Stub functions
  DEBUG("ddhcp_dhcp_leasenak(%li,%li,%li)\n", (long int) &blocks, (long int) &packet, (long int) &config);
}

void ddhcp_dhcp_release(struct ddhcp_block* blocks, struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_dhcp_release(blocks,packet,config)\n");
  dhcp_release_lease(packet->address, blocks, config);
}
