#ifndef _DHCP_PACKET_H
#define _DHCP_PACKET_H

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

struct dhcp_option {
    uint8_t code;
    uint8_t len;
    uint8_t *payload;
};
typedef struct dhcp_option dhcp_option;

struct dhcp_packet {
      uint8_t op;
      uint8_t htype;
      uint8_t hlen;
      uint8_t hops;
      uint32_t xid;
      uint16_t secs;
      uint16_t flags;
      struct in_addr ciaddr;
      struct in_addr yiaddr; 
      struct in_addr siaddr; 
      struct in_addr giaddr; 
      int8_t chaddr[16];
      char sname[64];
      char file[128];
      int options_len;
      dhcp_option *options;
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

enum dhcp_option_code {
  // RFC 2132
  DHCP_CODE_PAD = 0,
  DHCP_CODE_MESSAGE_TYPE = 53,
  DHCP_CODE_PARAMETER_REQUEST_LIST = 55,
  DHCP_CODE_END = 255,
};

/** 
 * Print an representation of a dhcp_packet to stdout.
 */
void printf_dhcp(dhcp_packet *packet);

/**  
 * Reads and checks a dhcp_packet from buffer. Will return zero on success.
 * To reduce memory consumption and prevent further memcpy operations this will
 * make pointer to the buffer inside of the dhcp_packet structure. Do not free
 * the buffer before the last operation on that struture!
 */
int ntoh_dhcp_packet(dhcp_packet *packet,uint8_t* buffer,int len);
int send_dhcp_packet(int socket,dhcp_packet *packet);

uint8_t dhcp_packet_message_type(dhcp_packet *packet);
#endif
