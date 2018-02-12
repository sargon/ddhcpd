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

  if (*blocks == NULL) {
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

void ddhcp_block_process(uint8_t* buffer, int len, struct sockaddr_in6 sender, ddhcp_block* blocks, ddhcp_config* config) {
  struct ddhcp_mcast_packet packet;
  int ret = ntoh_mcast_packet(buffer, len, &packet);
  packet.sender = &sender;

  if (ret == 0) {
    switch (packet.command) {
    case DDHCP_MSG_UPDATECLAIM:
      ddhcp_block_process_claims(blocks, &packet, config);
      break;

    case DDHCP_MSG_INQUIRE:
      ddhcp_block_process_inquire(blocks, &packet, config);
      break;

    default:
      break;
    }

    free(packet.payload);
  } else {
    DEBUG("epoll_ret: %i\n", ret);
  }

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
      // Notice the ownership
      blocks[block_index].state = DDHCP_CLAIMED;
      blocks[block_index].timeout = now + claim->timeout;
      // Save the connection details for the claiming node
      // We need to contact him, for dhcp forwarding actions.
      memcpy(&blocks[block_index].owner_address, &packet->sender->sin6_addr, sizeof(struct in6_addr));
      memcpy(&blocks[block_index].node_id, &packet->node_id, sizeof(ddhcp_node_id));
      #if LOG_LEVEL >= LOG_DEBUG
      char ipv6_sender[INET6_ADDRSTRLEN];
      DEBUG("Register block to %s\n",
            inet_ntop(AF_INET6, &blocks[block_index].owner_address, ipv6_sender, INET6_ADDRSTRLEN));
      #endif
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

void ddhcp_dhcp_process(uint8_t* buffer, int len, struct sockaddr_in6 sender, ddhcp_block* blocks, ddhcp_config* config) {
  struct ddhcp_mcast_packet packet;
  int ret = ntoh_mcast_packet(buffer, len, &packet);
  packet.sender = &sender;

  if (ret == 0) {
    switch (packet.command) {
    case DDHCP_MSG_RENEWLEASE:
      ddhcp_dhcp_renewlease(blocks, &packet, config);
      break;

    case DDHCP_MSG_LEASEACK:
      ddhcp_dhcp_leaseack(blocks, &packet, config);
      break;

    case DDHCP_MSG_LEASENAK:
      ddhcp_dhcp_leasenak(&packet, config);
      break;

    case DDHCP_MSG_RELEASE:
      ddhcp_dhcp_release(blocks, &packet, config);
      break;

    default:
      break;
    }
  }
}

void ddhcp_dhcp_renewlease(struct ddhcp_block* blocks, struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_dhcp_renewlease(blocks, request, config)\n");

  #if LOG_LEVEL >= LOG_DEBUG
  char* hwaddr = hwaddr2c(packet->renew_payload->chaddr);
  DEBUG("ddhcp_dhcp_renewlease( ... ): Request for xid: %u chaddr: %s\n",packet->renew_payload->xid,hwaddr);
  free(hwaddr);
  #endif

  int ret = dhcp_rhdl_request(&(packet->renew_payload->address), blocks, config);

  ddhcp_mcast_packet* answer = NULL;

  if (ret == 0) {
    DEBUG("ddhcp_dhcp_renewlease( ... ): %i ACK\n", ret);
    answer = new_ddhcp_packet(DDHCP_MSG_LEASEACK, config);
  } else if (ret == 1) {
    DEBUG("ddhcp_dhcp_renewlease( ... ): %i NAK\n", ret);
    answer = new_ddhcp_packet(DDHCP_MSG_LEASENAK, config);
    // TODO Can we hand over the block?
  } else {
    // Unexpected behaviour
    WARNING("ddhcp_dhcp_renewlease( ... ) -> Unexpected return value from dhcp_rhdl_request.");
    return;
  }

  answer->renew_payload = packet->renew_payload;

  send_packet_direct(answer, &packet->sender->sin6_addr, config->server_socket, config->mcast_scope_id);
  free(answer->renew_payload);
  free(answer);
}

void ddhcp_dhcp_leaseack(struct ddhcp_block* blocks, struct ddhcp_mcast_packet* request, ddhcp_config* config) {
  // Stub functions
  DEBUG("ddhcp_dhcp_leaseack(blocks,request,config)\n");
  #if LOG_LEVEL >= LOG_DEBUG
  char* hwaddr = hwaddr2c(request->renew_payload->chaddr);
  DEBUG("ddhcp_dhcp_leaseack( ... ): ACK for xid: %u chaddr: %s\n",request->renew_payload->xid,hwaddr);
  free(hwaddr);
  #endif
  dhcp_packet* packet = dhcp_packet_list_find(&config->dhcp_packet_cache, request->renew_payload->xid, request->renew_payload->chaddr);

  if (packet == NULL) {
    // Ignore packet
    DEBUG("ddhcp_dhcp_leaseack( ... ) -> No matching packet found, ignore message\n");
  } else {
    // Process packet
    dhcp_rhdl_ack(config->client_socket, packet, blocks, config);
  }
  dhcp_packet_free(packet,1);
  free(packet);
  free(request->renew_payload);
}

void ddhcp_dhcp_leasenak(struct ddhcp_mcast_packet* request, ddhcp_config* config) {
  // Stub functions
  DEBUG("ddhcp_dhcp_leasenak(blocks,request,config)\n");
  #if LOG_LEVEL >= LOG_DEBUG
  char* hwaddr = hwaddr2c(request->renew_payload->chaddr);
  DEBUG("ddhcp_dhcp_leaseack( ... ): NAK for xid: %u chaddr: %s\n",request->renew_payload->xid,hwaddr);
  free(hwaddr);
  #endif
  dhcp_packet* packet = dhcp_packet_list_find(&config->dhcp_packet_cache, request->renew_payload->xid, request->renew_payload->chaddr);

  if (packet == NULL) {
    // Ignore packet
    DEBUG("ddhcp_dhcp_leaseack( ... ) -> No matching packet found, ignore message\n");
  } else {
    // Process packet
    dhcp_nack(config->client_socket, packet);
  }
  dhcp_packet_free(packet,1);
  free(packet);
  free(request->renew_payload);
}

void ddhcp_dhcp_release(struct ddhcp_block* blocks, struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_dhcp_release(blocks,packet,config)\n");
  dhcp_release_lease(packet->renew_payload->address, blocks, config);
  free(packet->renew_payload);
}
