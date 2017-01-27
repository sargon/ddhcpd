#include <string.h>

#include "dhcp.h"
#include "tools.h"
#include "logger.h"
#include "dhcp_options.h"

// Free an offered lease after 12 seconds.
uint16_t DHCP_OFFER_TIMEOUT = 12;
uint16_t DHCP_LEASE_TIME    = 3600;
uint16_t DHCP_LEASE_SERVER_DELTA = 100;

#if LOG_LEVEL >= LOG_DEBUG
#define DEBUG_DHCP_LEASE(...) do { \
  DEBUG("DHCP LEASE [ state %i, xid %u, end %i ]\n",lease->state,lease->xid,lease->lease_end);\
} while (0);
#else
#define DEBUG_LEASE(...)
#endif

uint8_t find_lease_from_address(struct in_addr* addr, ddhcp_block* blocks, ddhcp_config* config, ddhcp_block** lease_block, uint32_t* lease_index) {
#if LOG_LEVEL >= LOG_DEBUG
  DEBUG("find_lease_from_address( %s, ...)\n", inet_ntoa(*addr));
#endif
  uint32_t address = (uint32_t) addr->s_addr;

  uint32_t block_number = (ntohl(address) - ntohl((uint32_t) config->prefix.s_addr)) / config->block_size;
  uint32_t lease_number = (ntohl(address) - ntohl((uint32_t) config->prefix.s_addr)) % config->block_size;

  if (block_number < config->number_of_blocks) {
    if (blocks[block_number].state == DDHCP_OURS) {
      DEBUG("find_lease_from_address(...) -> found block %i and lease %i\n", block_number, lease_number);
      if(lease_block) {
        *lease_block = blocks + block_number;
      }
      if(lease_index) {
        *lease_index = lease_number;
      }
      return 0;
    } else {
      // TODO Try to aquire address for client
      return 1;
    }
  }

  DEBUG("find_lease_from_address(...) -> block index %i outside of configured of network structure\n", block_number);

  return 1;
}

dhcp_packet* build_initial_packet(dhcp_packet* from_client) {
  DEBUG("build_initial_packet( from_client, packet )\n");

  dhcp_packet* packet = (dhcp_packet*) calloc(sizeof(dhcp_packet), 1);

  if (packet == NULL) {
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
  memcpy(&packet->ciaddr, &from_client->ciaddr, 4);
  // yiaddr
  // siaddr
  memcpy(&packet->giaddr, &from_client->giaddr, 4);
  memcpy(&packet->chaddr, &from_client->chaddr, 16);
  // sname
  // file
  // options

  return packet;
}

int dhcp_discover(int socket, dhcp_packet* discover, ddhcp_block* blocks, ddhcp_config* config) {
  DEBUG("dhcp_discover( %i, packet, blocks, config)\n", socket);
  time_t now = time(NULL);
  ddhcp_block* block = blocks;
  dhcp_lease* lease = NULL;
  ddhcp_block* lease_block = NULL;
  int lease_index = 0;
  int lease_ratio = config->block_size + 1;

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    if (block->state == DDHCP_OURS) {
      int free_leases = dhcp_num_free(block);

      if (free_leases > 0) {
        DEBUG("dhcp_discover(...) -> block %i has %i free leases\n", block->index, free_leases);

        if (free_leases < lease_ratio) {
          DEBUG("dhcp_discover(...) -> block %i has best lease ratio until now\n", block->index);
          uint32_t index = dhcp_get_free_lease(block);
          lease_block = block;
          lease_index = index;
          lease_ratio = free_leases;

          lease = block->addresses + index;
        }
      }
    }

    block++;
  }

  if (! lease) {
    DEBUG("dhcp_discover(...) -> no free leases found");
    return 2;
  }

  dhcp_packet* packet = build_initial_packet(discover);

  if (! packet) {
    DEBUG("dhcp_discover(...) -> memory allocation failure");
    return 1;
  }

  // Mark lease as offered and register client
  memcpy(&lease->chaddr, &discover->chaddr, 16);
  lease->xid = discover->xid;
  lease->state = OFFERED;
  lease->lease_end = now + DHCP_OFFER_TIMEOUT;

  addr_add(&lease_block->subnet, &packet->yiaddr, lease_index);
  DEBUG("dhcp_discover(...) offering address %i %s\n", lease_index, inet_ntoa(lease_block->subnet));

  // TODO We need a more extendable way to build up options
  packet->options_len = fill_options(discover->options, discover->options_len, &config->options, 2, &packet->options) ;

  // TODO Error handling
  set_option(packet->options, packet->options_len, DHCP_CODE_MESSAGE_TYPE, 1, (uint8_t[]) {
    DHCPOFFER
  });
  set_option(packet->options, packet->options_len, DHCP_CODE_ADDRESS_LEASE_TIME, 1, (uint8_t[]) {
    DHCP_LEASE_TIME
  });

  send_dhcp_packet(socket, packet);
  free(packet);
  return 0;
}

int dhcp_request(int socket, struct dhcp_packet* request, ddhcp_block* blocks, ddhcp_config* config) {
  DEBUG("dhcp_request( %i, dhcp_packet, blocks, config)\n", socket);
  // search the lease we may have offered
  time_t now = time(NULL);
  dhcp_lease* lease = NULL ;
  ddhcp_block* lease_block = NULL;
  uint32_t lease_index = 0;

  uint8_t* address = find_option_requested_address(request->options, request->options_len);
  struct in_addr requested_address;
  uint8_t found_address = 0;

  if (address) {
    memcpy(&requested_address, address, 4);
    found_address = 1;
  }


  if (!address) {
    if (request->ciaddr.s_addr != INADDR_ANY) {
      memcpy(&requested_address, &request->ciaddr.s_addr, 4);
      found_address = 1;
    }
  }

  if (found_address) {
    // Calculate block and dhcp_lease from address
    uint8_t found = find_lease_from_address(&requested_address, blocks, config, &lease_block, &lease_index);

    if (found == 0) {
      lease = lease_block->addresses + lease_index;

      if (lease->state != OFFERED || lease->xid != request->xid) {
        if (memcmp(request->chaddr, lease->chaddr, 16) != 0) {
          // Check if lease is free
          if (lease->state != FREE) {
            DEBUG("dhcp_request(...): Requested lease offered to other client\n");
            // Send DHCP_NACK
            dhcp_nack(socket, request);
            return 2;
          }
        }
      }
    }
  } else {
    ddhcp_block* block = blocks;

    // Find lease from xid
    for (uint32_t i = 0; i < config->number_of_blocks; i++) {
      if (block->state == DDHCP_OURS) {
        dhcp_lease* lease_iter = block->addresses;

        for (unsigned int j = 0 ; j < block->subnet_len ; j++) {
          if (lease_iter->state == OFFERED && lease_iter->xid == request->xid) {
            if (memcmp(request->chaddr, lease_iter->chaddr, 16) == 0) {
              lease = lease_iter;
              lease_block = block;
              lease_index = j;
              DEBUG("dhcp_request(...): Found requested lease\n");
              break;
            }
          }

          lease_iter++;
        }

        if (lease) {
          break;
        }
      }

      block++;
    }
  }

  if (!lease) {
    DEBUG("dhcp_request(...): Requested lease not found\n");
    // Send DHCP_NACK
    dhcp_nack(socket, request);
    return 2;
  }

  dhcp_packet* packet = build_initial_packet(request);

  if (! packet) {
    DEBUG("dhcp_request(...) -> memory allocation failure\n");
    return 1;
  }

  // Mark lease as leased and register client
  memcpy(&lease->chaddr, &request->chaddr, 16);
  lease->xid = request->xid;
  lease->state = LEASED;
  lease->lease_end = now + DHCP_LEASE_TIME + DHCP_LEASE_SERVER_DELTA;

  addr_add(&lease_block->subnet, &packet->yiaddr, lease_index);
  DEBUG("dhcp_request(...) offering address %i %s\n", lease_index, inet_ntoa(packet->yiaddr));

  // TODO We need a more extendable way to build up options
  packet->options_len = fill_options(request->options, request->options_len, &(config->options), 2, &packet->options) ;

  // TODO Error handling
  set_option(packet->options, packet->options_len, DHCP_CODE_MESSAGE_TYPE, 1, (uint8_t[]) {
    DHCPACK
  });
  // TODO correct type conversion, currently solution is simply wrong
  set_option(packet->options, packet->options_len, DHCP_CODE_ADDRESS_LEASE_TIME, 4, (uint8_t[]) {
    0, 0, 0, DHCP_LEASE_TIME
  });

  send_dhcp_packet(socket, packet);
  free(packet);

  return 0;
}

int dhcp_nack(int socket, dhcp_packet* from_client) {
  dhcp_packet* packet = build_initial_packet(from_client);

  if (! packet) {
    DEBUG("dhcp_discover(...) -> memory allocation failure\n");
    return 1;
  }

  packet->options_len = 1;
  packet->options = (dhcp_option*) calloc(sizeof(dhcp_option), 1);
  // TODO Error handling

  set_option(packet->options, packet->options_len, DHCP_CODE_MESSAGE_TYPE, 1, (uint8_t[]) {
    DHCPNAK
  });

  send_dhcp_packet(socket, packet);
  free(packet);

  return 0;
}

int dhcp_has_free(struct ddhcp_block* block) {
  dhcp_lease* lease = block->addresses;

  for (unsigned int i = 0 ; i < block->subnet_len ; i++) {
    if (lease->state == FREE) {
      return 1;
    }

    lease++;
  }

  return 0;
}

int dhcp_num_free(struct ddhcp_block* block) {
  int num = 0;
  dhcp_lease* lease = block->addresses;

  for (unsigned int i = 0 ; i < block->subnet_len ; i++) {
    if (lease->state == FREE) {
      num++;
    }

    lease++;
  }

  return num;
}

uint32_t dhcp_get_free_lease(ddhcp_block* block) {
  dhcp_lease* lease = block->addresses;

  for (uint32_t i = 0 ; i < block->subnet_len ; i++) {
    if (lease->state == FREE) {
      return i;
    }

    lease++;
  }

  ERROR("dhcp_get_free_lease(...): no free lease found");
  return block->subnet_len;
}

void dhcp_check_timeouts(ddhcp_block* block) {
  dhcp_lease* lease = block->addresses;
  time_t now = time(NULL);

  for (unsigned int i = 0 ; i < block->subnet_len ; i++) {
    if (lease->state != FREE && lease->lease_end < now) {
      INFO("Releasing Lease %i in block %i\n", i, block->index);

      memset(lease->chaddr, 0, 16);

      lease->xid   = 0;
      lease->state = FREE;
    }

    lease++;
  }
}
