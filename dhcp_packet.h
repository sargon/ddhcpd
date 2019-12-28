#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include <netinet/in.h>

#include <sys/types.h>

#include "list.h"

// List of dhcp_packet
typedef struct list_head dhcp_packet_list;

struct dhcp_packet {
  uint8_t op;
  uint8_t htype;
  uint8_t hlen;
  uint8_t hops;
  uint32_t xid;
  uint16_t secs;
  uint16_t flags;
  int8_t chaddr[16];
  char sname[64];
  char file[128];
  uint8_t options_len;
  time_t timeout;
  struct in_addr ciaddr;
  struct in_addr yiaddr;
  struct in_addr siaddr;
  struct in_addr giaddr;
  struct dhcp_option* options;

  dhcp_packet_list packet_list;
};
typedef struct dhcp_packet dhcp_packet;

enum dhcp_message_type {
  DHCPDISCOVER  = 1,
  DHCPOFFER     = 2,
  DHCPREQUEST   = 3,
  DHCPDECLINE   = 4,
  DHCPACK       = 5,
  DHCPNAK       = 6,
  DHCPRELEASE   = 7,
  DHCPINFORM    = 8,
};

#define DHCP_BROADCAST_MASK 0x8000u

/**
 * Store a packet in the packet_list, create a copy of the packet.
 */
ATTR_NONNULL_ALL int dhcp_packet_list_add(dhcp_packet_list* list, dhcp_packet* packet);

/**
 * Search for a packet in the dhcp_packet_list checking chaddr and xid.
 */
ATTR_NONNULL_ALL dhcp_packet* dhcp_packet_list_find(dhcp_packet_list* list, uint32_t xid, uint8_t* chaddr);

/**
 * Cleanup the packet list.
 */
ATTR_NONNULL_ALL void dhcp_packet_list_timeout(dhcp_packet_list* list);

/**
 * Free DHCP packet list
 */
ATTR_NONNULL_ALL void dhcp_packet_list_free(dhcp_packet_list* list);

/**
 * Print an representation of a dhcp_packet to stdout.
 */
ATTR_NONNULL_ALL void printf_dhcp(dhcp_packet* packet);

/**
 * Memcpy a packet into another packet.
 */
ATTR_NONNULL_ALL int dhcp_packet_copy(dhcp_packet* dest, dhcp_packet* src);


/**
 * Free a packet.
 */
#define dhcp_packet_free(packet,free_payload) do {\
    if ( free_payload > 0 ) {\
      dhcp_option* _option = packet->options;\
      for (; _option < packet->options + packet->options_len; _option++) {\
        free(_option->payload);\
      }\
    }\
    free(packet->options);\
  } while(0)

/**
 * Reads and checks a dhcp_packet from buffer. Will return zero on success.
 * To reduce memory consumption and prevent further memcpy operations this will
 * make pointer to the buffer inside of the dhcp_packet structure. Do not free
 * the buffer before the last operation on that struture!
 */
ATTR_NONNULL_ALL ssize_t ntoh_dhcp_packet(dhcp_packet* packet, uint8_t* buffer, ssize_t len);
ATTR_NONNULL_ALL ssize_t dhcp_packet_send(int socket, dhcp_packet* packet);

ATTR_NONNULL_ALL uint8_t dhcp_packet_message_type(dhcp_packet* packet);
