#include "dhcp_packet.h"

#include <stdio.h>
#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>

struct sockaddr_in broadcast = {
  .sin_family = AF_INET,
  .sin_addr = {INADDR_BROADCAST},
};

void printf_dhcp(dhcp_packet* packet) {
  char* ciaddr_str = (char*) malloc(INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(packet->ciaddr.s_addr), ciaddr_str, INET_ADDRSTRLEN);

  char* yiaddr_str = (char*) malloc(INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(packet->yiaddr.s_addr), yiaddr_str, INET_ADDRSTRLEN);

  char* giaddr_str = (char*) malloc(INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(packet->giaddr.s_addr), giaddr_str, INET_ADDRSTRLEN);

  char* siaddr_str = (char*) malloc(INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(packet->siaddr.s_addr), siaddr_str, INET_ADDRSTRLEN);

  printf(" BOOTP [ op %i, htype %i, hlen %i, hops %i, xid %lu, secs %i, flags %i, ciaddr %s, yiaddr %s, siaddr %s, giaddr %s, sname: %s, file: %s ]\n"
         , packet->op
         , packet->htype
         , packet->hlen
         , packet->hops
         , (unsigned long) packet->xid
         , packet->secs
         , packet->flags
         , ciaddr_str
         , yiaddr_str
         , siaddr_str
         , giaddr_str
         , packet->sname
         , packet->file
        );

  free(ciaddr_str);
  free(yiaddr_str);
  free(giaddr_str);
  free(siaddr_str);

  dhcp_option* option = packet->options;

  for (int i = 0; i < packet->options_len; i++) {
    if (option->len == 1) {
      printf("DHCP OPTION [ code %i, length %i, value %i ]\n", option->code, option->len, option->payload[0]);
    } else if (option->code == DHCP_CODE_PARAMETER_REQUEST_LIST) {
      printf("DHCP OPTION [ code %i, length %i, value ", option->code, option->len);

      for (int k = 0; k < option->len; k++) {
        printf("%i ", option->payload[k]);
      }

      printf("]\n");
    } else {
      printf("DHCP OPTION [ code %i, length %i ]\n", option->code, option->len);
    }

    option++;
  }
}

int _dhcp_packet_len(dhcp_packet* packet) {
  int len = 240 + 1;
  dhcp_option* option = packet->options;

  for (int i = 0; i < packet->options_len; i++) {
    switch (option->code) {
    case DHCP_CODE_PAD:
    case DHCP_CODE_END:
      len++;
      break;

    default:
      len += 2 + option->len;
      break;
    }

    option++;
  }

  return len;
}

int ntoh_dhcp_packet(dhcp_packet* packet, uint8_t* buffer, int len) {

  uint16_t tmp16;
  uint32_t tmp32;

  if (len < 240) {
    return -1;
  }

  printf("LEN:%i\n", len);

  // TODO Use macros to read from the buffer

  packet->op    = buffer[0];
  packet->htype = buffer[1];
  packet->hlen  = buffer[2];
  packet->hops  = buffer[3];
  memcpy(&tmp32, buffer + 4, 4);
  packet->xid   = ntohl(tmp32);
  memcpy(&tmp16, buffer + 8, 2);
  packet->secs  = ntohs(tmp16);
  memcpy(&tmp16, buffer + 10, 2);
  packet->flags = ntohs(tmp16);
  memcpy(&packet->ciaddr, buffer + 12, 4);
  memcpy(&packet->yiaddr, buffer + 16, 4);
  memcpy(&packet->siaddr, buffer + 20, 4);
  memcpy(&packet->giaddr, buffer + 24, 4);
  memcpy(&packet->chaddr, buffer + 28, 16);
  memcpy(&packet->sname, buffer + 44, 64);
  memcpy(&packet->file, buffer + 108, 128);

  // Check for the magic cookie
  if (!((uint8_t) buffer[236] == 99
        && (uint8_t) buffer[237] == 130
        && (uint8_t) buffer[238] == 83
        && (uint8_t) buffer[239] == 99
       )) {
    printf("Warning: Magic cookie not found!\n");
    return -7;
  }

  uint8_t* option = buffer + 236 + 4;

  // Count options
  int options = 0;
  int exit = 0;
  int dhcp_message_type = 0;
  int dhcp_request_list = 0;

  while (option < buffer + len && exit == 0) {
    switch ((uint8_t) option[0]) {
    case DHCP_CODE_PAD:
      // JUMP the padding
      option += 1;
      continue;
      break;

    case DHCP_CODE_END:
      option += 1;
      exit = 1;
      //continue;
      break;

    case DHCP_CODE_MESSAGE_TYPE:
      dhcp_message_type = 1;
      break;

    case DHCP_CODE_PARAMETER_REQUEST_LIST:
      dhcp_request_list = 1;
      break;

    default:
      break;
    }

    if (option + 1 > buffer + len) {
      printf("Warning: DHCP options ended improperly, possible broken client.\n");
      return -4;
    }

    if (option + option[1] + 2 > buffer + len) {
      // Error: Malformed dhcp options
      printf("Warning: DHCP options smaller than len of last option suggest, possible broken client.\n");
      return -5;
    } else {
      option += (uint8_t) option[1] + 2;
      options++;
    }
  }

  if (dhcp_message_type + dhcp_request_list < 2) {
    printf("Warning: Required DHCP options are available, invalid message!\n");
    return -6;
  }

  packet->options_len = options;
  packet->options = (dhcp_option*) malloc(sizeof(dhcp_option) * options);

  option = buffer + 236 + 4;

  exit = 0;
  int i = 0;

  while (option < buffer + len && exit == 0) {
    switch ((uint8_t) option[0]) {
    case DHCP_CODE_END:
      exit = 1;

    case DHCP_CODE_PAD:
      // JUMP padding and end
      packet->options[i].code = option[0];
      packet->options[i].len  = 0;
      packet->options[i].payload = NULL;

      option += 1;
      i++;
      break;

    default:
      packet->options[i].code = option[0];
      packet->options[i].len  = option[1];
      packet->options[i].payload = option + 2;

      option += (uint8_t) option[1] + 2;
      i++;
      break;
    }
  }

  assert(i == options);

  printf_dhcp(packet);

  return 0;
}

int send_dhcp_packet(int socket, dhcp_packet* packet) {

  uint16_t tmp16;
  uint32_t tmp32;

  uint8_t* buffer = calloc(sizeof(char), _dhcp_packet_len(packet));

  // Header
  buffer[0] = packet->op;
  buffer[1] = packet->htype;
  buffer[2] = packet->hlen;
  buffer[3] = packet->hops;
  tmp32 = htonl(packet->xid);
  memcpy(buffer + 4, &tmp32, 4);
  tmp16 = htons(packet->secs);
  memcpy(buffer + 8, &tmp16, 2);
  tmp16 = htons(packet->flags);
  memcpy(buffer + 10, &tmp16, 2);
  memcpy(buffer + 12 , &packet->ciaddr, 4);
  memcpy(buffer + 16 , &packet->yiaddr, 4);
  memcpy(buffer + 20 , &packet->siaddr, 4);
  memcpy(buffer + 24 , &packet->giaddr, 4);
  memcpy(buffer + 28 , &packet->chaddr, 16);
  memcpy(buffer + 44, &packet->sname, 64);
  memcpy(buffer + 108, &packet->file, 128);

  // Magic Cookie
  buffer[236] = 99;
  buffer[237] = 130;
  buffer[238] = 83;
  buffer[239] = 99;

  // Options
  uint8_t* obuf = buffer + 240;
  dhcp_option* option = packet->options;

  for (int i = 0 ; i < packet->options_len; i++) {
    obuf[0] = option->code;

    switch (option->code) {
    case DHCP_CODE_PAD:
    case DHCP_CODE_END:
      obuf++;
      break;

    default:
      obuf[1] = option->len;
      memcpy(obuf + 2, option->payload, option->len);
      obuf = obuf + 2 + option->len;
      break;
    }

    option++;
  }

  buffer[_dhcp_packet_len(packet) - 1] = 255;
  assert(obuf + 1 == buffer + _dhcp_packet_len(packet));
  // Network send
  printf("Message LEN: %i\n", _dhcp_packet_len(packet));

  broadcast.sin_port = htons(68);

  int err = sendto(socket, buffer, _dhcp_packet_len(packet), 0, (struct sockaddr*)&broadcast, sizeof(broadcast));

  if (err) {
    perror("sendto");
    printf("Err: %i\n", errno);
  }

  return 0;
}

uint8_t dhcp_packet_message_type(dhcp_packet* packet) {
  dhcp_option* option = packet->options;

  for (int i = 0; i < packet->options_len; i++) {
    if (option->code == DHCP_CODE_MESSAGE_TYPE) {
      return (uint8_t) option->payload[0];
    }

    option++;
  }

  return 0;
}
