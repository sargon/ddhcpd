#include <assert.h>

#include "ddhcp.h"
#include "dhcp.h"
#include "logger.h"
#include "tools.h"
#include "statistics.h"

int ddhcp_block_init(ddhcp_config* config) {
  DEBUG("ddhcp_block_init(config)\n");

  if (config->number_of_blocks < 1) {
    FATAL("ddhcp_block_init(...): Need at least 1 block to be configured\n");
    return 1;
  }

  config->blocks = (struct ddhcp_block*) calloc(sizeof(struct ddhcp_block), config->number_of_blocks);

  if (!config->blocks) {
    FATAL("ddhcp_block_init(...): Can't allocate memory for block structure\n");
    return 1;
  }

  time_t now = time(NULL);

  // TODO Maybe we should allocate number_of_blocks dhcp_lease_blocks previous
  //      and assign one here instead of NULL. Performance boost, Memory defrag?
  struct ddhcp_block* block = config->blocks;

  for (uint32_t index = 0; index < config->number_of_blocks; index++) {
    block->index = index;
    block->state = DDHCP_FREE;
    addr_add(&config->prefix, &block->subnet, (int)(index * config->block_size));
    block->subnet_len = config->block_size;
    memset(&block->owner_address, 0, sizeof(struct in6_addr));
    block->timeout = now + config->block_timeout;
    block->claiming_counts = 0;
    block->addresses = NULL;
    block++;
  }

  return 0;
}

void ddhcp_block_free(ddhcp_config* config) {
  ddhcp_block* block = config->blocks;

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    block_free(block++);
  }

  block_free_claims(config);
  free(config->blocks);
}

int ddhcp_check_packet(struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  if (memcmp(&packet->prefix, &config->prefix, sizeof(struct in_addr)) != 0 ||
      packet->prefix_len != config->prefix_len ||
      packet->blocksize != config->block_size) {
    return 1;
  }

  return 0;
}

void ddhcp_block_process(uint8_t* buffer, ssize_t len, struct sockaddr_in6 sender, ddhcp_config* config) {
  struct ddhcp_mcast_packet packet;
  ssize_t ret = ntoh_mcast_packet(buffer, len, &packet);
  packet.sender = &sender;

  if (ret == 0) {
    // Check if this packet is for our swarm
    if (ddhcp_check_packet(&packet, config)) {
      DEBUG("ddhcp_block_process(...): drop foreign packet before processing");
      free(packet.payload);
      return;
    }

    switch (packet.command) {
    case DDHCP_MSG_UPDATECLAIM:
      statistics_record(config, STAT_MCAST_RECV_UPDATECLAIM, 1);
      ddhcp_block_process_claims(&packet, config);
      break;

    case DDHCP_MSG_INQUIRE:
      statistics_record(config, STAT_MCAST_RECV_INQUIRE, 1);
      ddhcp_block_process_inquire(&packet, config);
      break;

    default:
      break;
    }

    free(packet.payload);
  } else {
    DEBUG("ddhcp_block_process(...): epoll returned status %i\n", ret);
  }
}

void ddhcp_block_process_claims(struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_block_process_claims(packet,config)\n");

  assert(packet->command == 1);
  time_t now = time(NULL);

  ddhcp_block* blocks = config->blocks;

  for (unsigned int i = 0; i < packet->count; i++) {
    struct ddhcp_payload* claim = &packet->payload[i];
    uint32_t block_index = claim->block_index;

    if (block_index >= config->number_of_blocks) {
      WARNING("ddhcp_block_process_claims(...): Malformed block number\n");
      continue;
    }

    if (blocks[block_index].state == DDHCP_OURS && NODE_ID_CMP(packet->node_id, config->node_id) < 0) {
      INFO("ddhcp_block_process_claims(...): node 0x%02x%02x%02x%02x%02x%02x%02x%02x claims our block %i\n", HEX_NODE_ID(packet->node_id), block_index);
      // TODO Decide when and if we reclaim this block
      //      Which node has more leases in this block, ..., who has the better node_id.
      // Unrelated from the above, the original concept is claiming the block now.
      blocks[block_index].timeout = 0;
      block_update_claims(config);
    } else {
      // Notice the ownership
      blocks[block_index].state = DDHCP_CLAIMED;
      blocks[block_index].timeout = now + claim->timeout;
      // Save the connection details for the claiming node
      // We need to contact him, for dhcp forwarding actions.
      memcpy(&blocks[block_index].owner_address, &packet->sender->sin6_addr, sizeof(struct in6_addr));
      memcpy(&blocks[block_index].node_id, &packet->node_id, sizeof(ddhcp_node_id));

#if LOG_LEVEL_LIMIT >= LOG_DEBUG
      char ipv6_sender[INET6_ADDRSTRLEN];
      DEBUG("ddhcp_block_process_claims(...): Register block to %s\n",
            inet_ntop(AF_INET6, &blocks[block_index].owner_address, ipv6_sender, INET6_ADDRSTRLEN));
#endif

      INFO("ddhcp_block_process_claims(...): node 0x%02x%02x%02x%02x%02x%02x%02x%02x claims block %i with TTL %i\n", HEX_NODE_ID(packet->node_id), block_index, claim->timeout);
    }
  }
}

void ddhcp_block_process_inquire(struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_block_process_inquire(packet,config)\n");

  assert(packet->command == 2);
  time_t now = time(NULL);

  ddhcp_block* blocks = config->blocks;

  for (unsigned int i = 0; i < packet->count; i++) {
    struct ddhcp_payload* tmp = &packet->payload[i];

    if (tmp->block_index >= config->number_of_blocks) {
      WARNING("ddhcp_block_process_inquire(...): Malformed block number\n");
      continue;
    }

    INFO("ddhcp_block_process_inquire(...): node 0x%02x%02x%02x%02x%02x%02x%02x%02x inquires block %i\n", HEX_NODE_ID(packet->node_id), tmp->block_index);

    if (blocks[tmp->block_index].state == DDHCP_OURS) {
      // Update Claims
      INFO("ddhcp_block_process_inquire(...): block %i is ours, notify network\n", tmp->block_index);
      blocks[tmp->block_index].timeout = 0;
      block_update_claims(config);
    } else if (blocks[tmp->block_index].state == DDHCP_CLAIMING) {
      INFO("ddhcp_block_process_inquire(...): we are furthermore interested in block %i\n", tmp->block_index);

      // QUESTION Why do we need multiple states for the same process?
      if (NODE_ID_CMP(packet->node_id, config->node_id) > 0) {
        INFO("ddhcp_block_process_inquire(...): ... but other node wins.\n");
        blocks[tmp->block_index].state = DDHCP_TENTATIVE;
        blocks[tmp->block_index].timeout = now + config->tentative_timeout;
      }

      // otherwise keep inquiring, the other node should see our inquires and step back.
    } else {
      INFO("ddhcp_block_process_inquire(...): set block %i to tentative\n", tmp->block_index);
      blocks[tmp->block_index].state = DDHCP_TENTATIVE;
      blocks[tmp->block_index].timeout = now + config->tentative_timeout;
    }
  }
}

void ddhcp_dhcp_process(uint8_t* buffer, ssize_t len, struct sockaddr_in6 sender, ddhcp_config* config) {
  struct ddhcp_mcast_packet packet;
  ssize_t ret = ntoh_mcast_packet(buffer, len, &packet);
  packet.sender = &sender;

  if (ret == 0) {
    // Check if this packet is for our swarm
    if (ddhcp_check_packet(&packet, config)) {
      DEBUG("ddhcp_dhcp_process(...): drop foreign packet before processing");
      free(packet.renew_payload);
      return;
    }

    switch (packet.command) {
    case DDHCP_MSG_RENEWLEASE:
      statistics_record(config, STAT_DIRECT_RECV_RENEWLEASE, 1);
      ddhcp_dhcp_renewlease(&packet, config);
      break;

    case DDHCP_MSG_LEASEACK:
      statistics_record(config, STAT_DIRECT_RECV_LEASEACK, 1);
      ddhcp_dhcp_leaseack(&packet, config);
      break;

    case DDHCP_MSG_LEASENAK:
      statistics_record(config, STAT_DIRECT_RECV_LEASENAK, 1);
      ddhcp_dhcp_leasenak(&packet, config);
      break;

    case DDHCP_MSG_RELEASE:
      statistics_record(config, STAT_DIRECT_RECV_RELEASE, 1);
      ddhcp_dhcp_release(&packet, config);
      break;

    default:
      break;
    }
  }
}

void ddhcp_dhcp_renewlease(struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_dhcp_renewlease(request,config)\n");

  DEBUG("ddhcp_dhcp_renewlease(...): Request for xid: %u chaddr: %s\n", packet->renew_payload->xid, hwaddr2c(packet->renew_payload->chaddr));

  int ret = dhcp_rhdl_request(&(packet->renew_payload->address), config);

  ddhcp_mcast_packet* answer = NULL;

  if (ret == 0) {
    DEBUG("ddhcp_dhcp_renewlease(...): %i ACK\n", ret);
    answer = new_ddhcp_packet(DDHCP_MSG_LEASEACK, config);
    statistics_record(config, STAT_DIRECT_SEND_LEASEACK, 1);
  } else if (ret == 1) {
    DEBUG("ddhcp_dhcp_renewlease(...): %i NAK\n", ret);
    answer = new_ddhcp_packet(DDHCP_MSG_LEASENAK, config);
    statistics_record(config, STAT_DIRECT_SEND_LEASENAK, 1);
    // TODO Can we hand over the block?
  } else {
    // Unexpected behaviour
    WARNING("ddhcp_dhcp_renewlease(...): Unexpected return value from dhcp_rhdl_request.");
    return;
  }

  if (!answer) {
    WARNING("ddhcp_dhcp_renewlease(...): Failed to allocate memory for ddhcpd mcast packet.\n");
    return;
  }

  answer->renew_payload = packet->renew_payload;

  statistics_record(config, STAT_DIRECT_SEND_PKG, 1);
  ssize_t bytes_send = send_packet_direct(answer, &packet->sender->sin6_addr, config->server_socket, config->mcast_scope_id);
  statistics_record(config, STAT_DIRECT_SEND_BYTE, (long int) bytes_send);
  UNUSED(bytes_send);

  free(answer->renew_payload);
  free(answer);
}

void ddhcp_dhcp_leaseack(struct ddhcp_mcast_packet* request, ddhcp_config* config) {
  // Stub functions
  DEBUG("ddhcp_dhcp_leaseack(request,config)\n");

  DEBUG("ddhcp_dhcp_leaseack(...): ACK for xid: %u chaddr: %s\n", request->renew_payload->xid, hwaddr2c(request->renew_payload->chaddr));

  dhcp_packet* packet = dhcp_packet_list_find(&config->dhcp_packet_cache, request->renew_payload->xid, request->renew_payload->chaddr);

  if (!packet) {
    // Ignore packet
    DEBUG("ddhcp_dhcp_leaseack(...): No matching packet found, message ignored\n");
  } else {
    // Process packet
    dhcp_rhdl_ack(config->client_socket, packet, config);
    dhcp_packet_free(packet, 1);
    free(packet);
  }

  free(request->renew_payload);
}

void ddhcp_dhcp_leasenak(struct ddhcp_mcast_packet* request, ddhcp_config* config) {
  // Stub functions
  DEBUG("ddhcp_dhcp_leasenak(request,config)\n");

  DEBUG("ddhcp_dhcp_leaseack(...): NAK for xid: %u chaddr: %s\n", request->renew_payload->xid, hwaddr2c(request->renew_payload->chaddr));

  dhcp_packet* packet = dhcp_packet_list_find(&config->dhcp_packet_cache, request->renew_payload->xid, request->renew_payload->chaddr);

  if (!packet) {
    // Ignore packet
    DEBUG("ddhcp_dhcp_leaseack(...): No matching packet found, message ignored\n");
  } else {
    // Process packet
    dhcp_nack(config->client_socket, packet, config);
    dhcp_packet_free(packet, 1);
    free(packet);
  }

  free(request->renew_payload);
}

void ddhcp_dhcp_release(struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_dhcp_release(packet,config)\n");
  dhcp_release_lease(packet->renew_payload->address, config);
  free(packet->renew_payload);
}
