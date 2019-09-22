#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "block.h"
#include "dhcp.h"
#include "dhcp_options.h"
#include "hook.h"
#include "logger.h"
#include "packet.h"
#include "statistics.h"
#include "tools.h"

// Free an offered lease after 12 seconds.
uint16_t DHCP_OFFER_TIMEOUT = 12;
uint16_t DHCP_LEASE_SERVER_DELTA = 10;

#if LOG_LEVEL_LIMIT >= LOG_DEBUG
#define DEBUG_DHCP_LEASE(...) do { \
  DEBUG("DHCP LEASE [ state %i, xid %u, end %i ]\n",lease->state,lease->xid,lease->lease_end);\
} while (0);
#else
#define DEBUG_LEASE(...)
#endif

/**
 * Search for block and lease for given address, returns a status code and found
 * results.
 * A status code of 0 is returned, iff the result is in one of our blocks.
 * Of 1, iff result is non in our blocks.
 * And 2 on failure.
 */
uint8_t find_lease_from_address(struct in_addr* addr, ddhcp_config* config, ddhcp_block** lease_block, uint32_t* lease_index) {
#if LOG_LEVEL_LIMIT >= LOG_DEBUG
  DEBUG("find_lease_from_address(%s, ...)\n", inet_ntoa(*addr));
#endif

  ddhcp_block* blocks = config->blocks;
  uint32_t address = (uint32_t) addr->s_addr;

  uint32_t block_number = (ntohl(address) - ntohl((uint32_t) config->prefix.s_addr)) / config->block_size;
  uint32_t lease_number = (ntohl(address) - ntohl((uint32_t) config->prefix.s_addr)) % config->block_size;

  if (block_number < config->number_of_blocks) {
    DEBUG("find_lease_from_address(...): found block %i and lease %i with state %i\n", block_number, lease_number, blocks[block_number].state);

    if (lease_block) {
      *lease_block = blocks + block_number;
    }

    if (lease_index) {
      *lease_index = lease_number;
    }

    DEBUG("find_lease_from_address(...): state: %i\n", DDHCP_OURS);

    if (blocks[block_number].state == DDHCP_OURS) {
      return 0;
    } else {
      // TODO Try to aquire address for client
      return 1;
    }
  }

  DEBUG("find_lease_from_address(...): block index %i outside of configured network structure\n", block_number);

  return 2;
}

void _dhcp_release_lease(ddhcp_block* block, uint32_t lease_index) {
  INFO("dhcp_release_lease(...): Releasing lease %i in block %i\n", lease_index, block->index);
  dhcp_lease* lease = block->addresses + lease_index;

  // TODO Should we really reset the chaddr or xid, RFC says we
  // ''SHOULD retain a record of the client's initialization parameters for possible reuse''
  memset(lease->chaddr, 0, 16);

  lease->xid   = 0;
  lease->state = FREE;
}

dhcp_packet* build_initial_packet(dhcp_packet* from_client) {
  DEBUG("build_initial_packet(from_client)\n");

  dhcp_packet* packet = (dhcp_packet*) calloc(sizeof(dhcp_packet), 1);

  if (!packet) {
    WARNING("build_initial_packet(...): packet memory allocation failed\n");
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

uint8_t _ddo[1] = { 0 };

int16_t _dhcp_default_options(uint8_t msg_type, dhcp_packet* packet, dhcp_packet* request, ddhcp_config* config, bool include_lease_time) {
  int16_t num_options;
  // TODO We need a more extendable way to build up options
  // TODO Proper error handling

  // Fill options list with requested options, allocate memory and reserve for additonal
  // dhcp options.
  if ((num_options = fill_options(request->options, request->options_len, &config->options, 3, &packet->options)) < 0) {
    return num_options;
  }

  packet->options_len = (uint8_t)num_options;

  // DHCP Message Type
  _ddo[0] = msg_type;
  set_option(packet->options, packet->options_len, DHCP_CODE_MESSAGE_TYPE, 1, _ddo);

  // To support DHCPINFORM (as of RFC 2132) we need to be able to omit the lease time
  if (include_lease_time) {
    // DHCP Lease Time
    set_option_from_store(&config->options, packet->options, packet->options_len, DHCP_CODE_ADDRESS_LEASE_TIME);
  }

  // DHCP Server identifier
  set_option_from_store(&config->options, packet->options, packet->options_len, DHCP_CODE_SERVER_IDENTIFIER);

  return 0;
}

int dhcp_process(uint8_t* buffer, ssize_t len, ddhcp_config* config) {
  // TODO Error Handling
  struct dhcp_packet dhcp_packet_buf;
  ssize_t ret = ntoh_dhcp_packet(&dhcp_packet_buf, buffer, len);

  if (ret == 0) {
    int message_type = dhcp_packet_message_type(&dhcp_packet_buf);

    switch (message_type) {
    case DHCPDISCOVER:
      statistics_record(config, STAT_DHCP_RECV_DISCOVER, 1);
      ret = dhcp_hdl_discover(config->client_socket, &dhcp_packet_buf, config);

      if (ret == 1) {
        INFO("dhcp_process(...): we need to inquire new blocks\n");
        return 1;
      }

      break;

    case DHCPREQUEST:
      statistics_record(config, STAT_DHCP_RECV_REQUEST, 1);
      dhcp_hdl_request(config->client_socket, &dhcp_packet_buf, config);
      break;

    case DHCPRELEASE:
      statistics_record(config, STAT_DHCP_RECV_RELEASE, 1);
      dhcp_hdl_release(&dhcp_packet_buf, config);
      break;

    case DHCPINFORM:
      statistics_record(config, STAT_DHCP_RECV_INFORM, 1);
      dhcp_hdl_inform(config->client_socket, &dhcp_packet_buf, config);
      break;

    default:
      WARNING("dhcp_process(...): Unknown DHCP message of type %i\n", message_type);
      break;
    }

    if (dhcp_packet_buf.options_len > 0) {
      free(dhcp_packet_buf.options);
    }
  } else {
    WARNING("dhcp_process(...): Malformed packet!? errcode: %li\n", ret);
  }

  return 0;
}

int dhcp_hdl_discover(int socket, dhcp_packet* discover, ddhcp_config* config) {
  DEBUG("dhcp_hdl_discover(socket:%i, packet, config)\n", socket);

  time_t now = time(NULL);
  ddhcp_block* lease_block = block_find_free_leases(config);

  if (!lease_block) {
    DEBUG("dhcp_hdl_discover(...): no block with free leases found\n");
    return 3;
  }

  uint32_t lease_index = dhcp_get_free_lease(lease_block);
  dhcp_lease* lease = lease_block->addresses + lease_index;

  if (!lease) {
    DEBUG("dhcp_hdl_discover(...): no free leases found, this should not happen!\n");
    return 2;
  }

  dhcp_packet* packet = build_initial_packet(discover);

  if (!packet) {
    WARNING("dhcp_hdl_discover(...): packet memory allocation failed\n");
    return 1;
  }

  // Mark lease as offered and register client
  memcpy(&lease->chaddr, &discover->chaddr, 16);
  lease->xid = discover->xid;
  lease->state = OFFERED;
  lease->lease_end = now + DHCP_OFFER_TIMEOUT;

  addr_add(&lease_block->subnet, &packet->yiaddr, (int)lease_index);

  DEBUG("dhcp_hdl_discover(...): offering address %i %s\n", lease_index, inet_ntoa(lease_block->subnet));

  if (_dhcp_default_options(DHCPOFFER, packet, discover, config, true)) {
    WARNING("dhcp_hdl_discover(...): option memory allocation failed\n");
    free(packet);
    return 1;
  }

  statistics_record(config, STAT_DHCP_SEND_PKG, 1);
  statistics_record(config, STAT_DHCP_SEND_OFFER, 1);
  ssize_t bytes_send = dhcp_packet_send(socket, packet);
  statistics_record(config, STAT_DHCP_SEND_BYTE, (long int) bytes_send);
  UNUSED(bytes_send);

  free(packet->options);
  free(packet);

  return 0;
}

int dhcp_rhdl_request(uint32_t* address, ddhcp_config* config) {
  DEBUG("dhcp_rhdl_request(address,config)\n");

  time_t now = time(NULL);
  ddhcp_block* lease_block = NULL;
  uint32_t lease_index = 0;
  struct in_addr requested_address;

  memcpy(&requested_address, address, sizeof(struct in_addr));

  uint8_t found = find_lease_from_address(&requested_address, config, &lease_block, &lease_index);

  if (found == 0) {
    // Update lease information
    // TODO Check for validity of request (chaddr)
    dhcp_lease* lease = lease_block->addresses + lease_index;
    lease->lease_end = now + find_in_option_store_address_lease_time(&config->options)  + DHCP_LEASE_SERVER_DELTA;
    // Report ack
    return 0;
  } else if (found == 1) {
    // We got a request for a block we don't own (anymore?)
    // Reply with a nack
    return 1;
  } else {
    return 2;
  }
}

int dhcp_rhdl_ack(int socket, struct dhcp_packet* request, ddhcp_config* config) {

  ddhcp_block* lease_block = NULL;
  uint32_t lease_index = 0;
  struct in_addr requested_address = {0};

  uint8_t* address = find_option_requested_address(request->options, request->options_len);

  if (address) {
    memcpy(&requested_address, address, sizeof(struct in_addr));
  } else if (request->ciaddr.s_addr != INADDR_ANY) {
    memcpy(&requested_address, &request->ciaddr.s_addr, sizeof(struct in_addr));
  }

  if (find_lease_from_address(&requested_address, config, &lease_block, &lease_index) != 1) {
    DEBUG("dhcp_rhdl_ack(...): lease not found\n");
    return 1;
  }

  return dhcp_ack(socket, request, lease_block, lease_index, config);
}

int dhcp_hdl_request(int socket, struct dhcp_packet* request, ddhcp_config* config) {
  DEBUG("dhcp_hdl_request(socket:%i, dhcp_packet, config)\n", socket);

  // search the lease we may have offered

  time_t now = time(NULL);
  dhcp_lease* lease = NULL ;
  ddhcp_block* lease_block = NULL;
  uint32_t lease_index = 0;

  uint8_t* address = find_option_requested_address(request->options, request->options_len);

  struct in_addr requested_address;
  uint8_t found_address = 0;

  if (address) {
    memcpy(&requested_address, address, sizeof(struct in_addr));
    found_address = 1;
  } else if (request->ciaddr.s_addr != INADDR_ANY) {
    memcpy(&requested_address, &request->ciaddr.s_addr, sizeof(struct in_addr));
    found_address = 1;
  }

  if (found_address) {
    // Calculate block and dhcp_lease from address
    uint8_t found = find_lease_from_address(&requested_address, config, &lease_block, &lease_index);

    if (found != 2) {
      lease = lease_block->addresses + lease_index;
      DEBUG("dhcp_hdl_request(...): Lease found.\n");

      if (lease_block->state == DDHCP_CLAIMED) {
        if (block_alloc(lease_block)) {
          ERROR("dhcp_hdl_request(...): can't allocate requested block\n");
          dhcp_nack(socket, request, config);
        }

        lease = lease_block->addresses + lease_index;
        // This lease block is not ours so we have to forward the request
        DEBUG("dhcp_hdl_request(...): Requested lease is owned by another node. Sent request.\n");
        // Register client information in lease
        // TODO This isn't a good idea, because of multi request on the same address from various clients, register it elsewhere and append xid.
        lease->xid = request->xid;
        lease->state = OFFERED;
        lease->lease_end = now + find_in_option_store_address_lease_time(&config->options)  + DHCP_LEASE_SERVER_DELTA;
        memcpy(&lease->chaddr, &request->chaddr, 16);

        // Build packet and send it
        ddhcp_renew_payload payload;
        memcpy(&payload.chaddr, &request->chaddr, 16);
        memcpy(&payload.address, &requested_address, sizeof(struct in_addr));
        payload.xid = request->xid;
        payload.lease_seconds = 0;

#if LOG_LEVEL_LIMIT >= LOG_DEBUG
        char* hwaddr = hwaddr2c(payload.chaddr);
        DEBUG("dhcp_hdl_request(...): Save request for xid: %u chaddr: %s\n", payload.xid, hwaddr);
        free(hwaddr);
#endif

        // Send packet
        ddhcp_mcast_packet* packet = new_ddhcp_packet(DDHCP_MSG_RENEWLEASE, config);

        if (!packet) {
          WARNING("dhcp_hdl_request(...): Failed to allocate memory for ddhcpd mcast packet\n");
          return -ENOMEM;
        }

        packet->renew_payload = &payload;

        // Store packet for later usage.
        // TODO Error handling
        dhcp_packet_list_add(&config->dhcp_packet_cache, request);

        statistics_record(config, STAT_DIRECT_SEND_PKG, 1);
        statistics_record(config, STAT_DIRECT_SEND_RENEWLEASE, 1);
        ssize_t bytes_send = send_packet_direct(packet, &lease_block->owner_address, config->server_socket, config->mcast_scope_id);
        statistics_record(config, STAT_DIRECT_SEND_BYTE, (long int) bytes_send);
        UNUSED(bytes_send);
        free(packet);
        return 2;

      } else if (lease_block->state == DDHCP_OURS) {
        if (lease->state != OFFERED || lease->xid != request->xid) {
          if (memcmp(request->chaddr, lease->chaddr, 16) != 0) {
            // Check if lease is free
            if (lease->state != FREE) {
              DEBUG("dhcp_hdl_request(...): Requested lease offered to other client\n");
              // Send DHCP_NACK
              dhcp_nack(socket, request, config);
              return 2;
            }
          }
        }
      } else {
        // Block is neither blocked nor ours, so probably say nak here
        // TODO but first we should check if we are still in warmup.
        return 2;
      }
    }
  } else {
    ddhcp_block* block = config->blocks;

    // Find lease from xid
    for (uint32_t i = 0; i < config->number_of_blocks; i++) {
      if (block->state == DDHCP_OURS) {
        dhcp_lease* lease_iter = block->addresses;

        for (unsigned int j = 0 ; j < block->subnet_len ; j++) {
          if (lease_iter->state == OFFERED && lease_iter->xid == request->xid) {
            if (memcmp(request->chaddr, lease_iter->chaddr, 16) == 0) {
              DEBUG("dhcp_hdl_request(...): Found requested lease\n");

              lease = lease_iter;
              lease_block = block;
              lease_index = j;

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
    DEBUG("dhcp_hdl_request(...): Requested lease not found\n");
    // Send DHCP_NACK
    dhcp_nack(socket, request, config);
    return 2;
  }

  return dhcp_ack(socket, request, lease_block, lease_index, config);
}

void dhcp_hdl_release(dhcp_packet* packet, ddhcp_config* config) {
  DEBUG("dhcp_hdl_release(dhcp_packet,config)\n");

  ddhcp_block* lease_block = NULL;
  uint32_t lease_index = 0;

  struct in_addr addr;
  memcpy(&addr, &packet->ciaddr, sizeof(struct in_addr));
  uint8_t found = find_lease_from_address(&addr, config, &lease_block, &lease_index);

  dhcp_lease* lease;

  switch (found) {
  case 0:
    lease = lease_block->addresses + lease_index;

    // Check Hardware Address of client
    if (memcmp(packet->chaddr, lease->chaddr, 16) == 0) {
      _dhcp_release_lease(lease_block, lease_index);
      hook(HOOK_RELEASE, &packet->yiaddr, (uint8_t*) &packet->chaddr, config);
    } else {
      ERROR("dhcp_hdl_release(...): Hardware address transmitted by client did not match with our record, doing nothing.\n");
    }

  case 1:
    // TODO Handle remote block
    // Send Message to neighbor
    break;

  default:
    // Since there is no reply to this message, we could `silently` drop this case.
    break;
  }
}

void dhcp_hdl_inform(int socket, dhcp_packet* request, ddhcp_config* config) {
  DEBUG("dhcp_hdl_inform(socket:%i, dhcp_packet,config)\n", socket);

  ddhcp_block* lease_block = NULL;
  uint32_t lease_index = 0;

  struct in_addr addr;
  memcpy(&addr, &request->ciaddr, sizeof(struct in_addr));
  find_lease_from_address(&addr, config, &lease_block, &lease_index);

  dhcp_packet* packet = build_initial_packet(request);

  if (!packet) {
    WARNING("dhcp_hdl_inform(...): packed memory allocation failed\n");
    return;
  }

  if (_dhcp_default_options(DHCPACK, packet, request, config, false)) {
    WARNING("dhcp_hdl_inform(...): option memory allocation failed\n");
    free(packet);
    return;
  }

  DEBUG("dhcp_hdl_inform(...): informing address %i %s\n", lease_index, inet_ntoa(packet->ciaddr));
  statistics_record(config, STAT_DHCP_SEND_PKG, 1);
  statistics_record(config, STAT_DHCP_SEND_ACK, 1);
  ssize_t bytes_send = dhcp_packet_send(socket, packet);
  statistics_record(config, STAT_DHCP_SEND_BYTE, (long int) bytes_send);
  UNUSED(bytes_send);

  hook(HOOK_INFORM, &packet->ciaddr, (uint8_t*) &packet->chaddr, config);

  free(packet->options);
  free(packet);
}

int dhcp_nack(int socket, dhcp_packet* from_client, ddhcp_config* config) {
  dhcp_packet* packet = build_initial_packet(from_client);

  if (!packet) {
    WARNING("dhcp_nack(...): packet memory allocation failed\n");
    return 1;
  }

  packet->options_len = 1;
  packet->options = (dhcp_option*) calloc(sizeof(dhcp_option), 1);

  if (!packet->options) {
    WARNING("dhcp_nack(...): option memory allocation failed\n");
    free(packet);
    return 1;
  }

  set_option(packet->options, packet->options_len, DHCP_CODE_MESSAGE_TYPE, 1, (uint8_t[]) {
    DHCPNAK
  });

  statistics_record(config, STAT_DHCP_SEND_PKG, 1);
  statistics_record(config, STAT_DHCP_SEND_NAK, 1);
  ssize_t bytes_send = dhcp_packet_send(socket, packet);
  statistics_record(config, STAT_DHCP_SEND_BYTE, (long int) bytes_send);
  UNUSED(bytes_send);
  UNUSED(config);

  free(packet->options);
  free(packet);

  return 0;
}

int dhcp_ack(int socket, dhcp_packet* request, ddhcp_block* lease_block, uint32_t lease_index, ddhcp_config* config) {
  time_t now = time(NULL);
  dhcp_lease* lease = lease_block->addresses + lease_index;

  dhcp_packet* packet = build_initial_packet(request);

  if (!packet) {
    WARNING("dhcp_ack(...): packed memory allocation failed\n");
    return 1;
  }

  // Mark lease as leased and register client
  memcpy(&lease->chaddr, &request->chaddr, 16);
  lease->xid = request->xid;
  lease->state = LEASED;
  lease->lease_end = now + find_in_option_store_address_lease_time(&config->options)  + DHCP_LEASE_SERVER_DELTA;

  addr_add(&lease_block->subnet, &packet->yiaddr, (int)lease_index);

  if (_dhcp_default_options(DHCPACK, packet, request, config, true)) {
    WARNING("dhcp_ack(...): option memory allocation failed\n");
    free(packet);
    return 1;
  }

  DEBUG("dhcp_ack(...): offering address %i %s\n", lease_index, inet_ntoa(packet->yiaddr));
  statistics_record(config, STAT_DHCP_SEND_PKG, 1);
  statistics_record(config, STAT_DHCP_SEND_ACK, 1);
  ssize_t bytes_send = dhcp_packet_send(socket, packet);
  statistics_record(config, STAT_DHCP_SEND_BYTE, (long int) bytes_send);
  UNUSED(bytes_send);

  hook(HOOK_LEASE, &packet->yiaddr, (uint8_t*) &packet->chaddr, config);

  free(packet->options);
  free(packet);
  return 0;
}

int dhcp_has_free(struct ddhcp_block* block) {
  dhcp_lease* lease = block->addresses;

  for (unsigned int i = 0; i < block->subnet_len; i++) {
    if (lease->state == FREE) {
      return 1;
    }

    lease++;
  }

  return 0;
}

uint32_t dhcp_num_free(struct ddhcp_block* block) {
  uint32_t num = 0;
  dhcp_lease* lease = block->addresses;

  for (unsigned int i = 0 ; i < block->subnet_len ; i++) {
    if (lease->state == FREE) {
      num++;
    }

    lease++;
  }

  return num;
}

uint32_t dhcp_num_offered(struct ddhcp_block* block) {
  uint32_t num = 0;
  dhcp_lease* lease = block->addresses;

  for (unsigned int i = 0 ; i < block->subnet_len ; i++) {
    if (lease->state == OFFERED) {
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

  ERROR("dhcp_get_free_lease(...): no free leases found");

  return block->subnet_len;
}

void dhcp_release_lease(uint32_t address, ddhcp_config* config) {

  ddhcp_block* lease_block = NULL;
  uint32_t lease_index = 0;
  struct in_addr addr;
  memcpy(&addr, &address, sizeof(struct in_addr));
  uint8_t found = find_lease_from_address(&addr, config, &lease_block, &lease_index);

  if (found == 0) {
    _dhcp_release_lease(lease_block, lease_index);
  } else {
    DEBUG("dhcp_release_lease(...): No lease for address %s found.\n", inet_ntoa(addr));
  }
}

int dhcp_check_timeouts(ddhcp_block* block) {
  DEBUG("dhcp_check_timeouts(block)\n");
  dhcp_lease* lease = block->addresses;
  time_t now = time(NULL);

  int free_leases = 0;

  for (unsigned int i = 0 ; i < block->subnet_len ; i++) {
    if (lease->state != FREE && lease->lease_end < now) {
      _dhcp_release_lease(block, i);
    }

    if (lease->state == FREE) {
      free_leases++;
    }

    lease++;
  }

  return free_leases;
}
