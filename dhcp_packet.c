#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "types.h"
#include "logger.h"

struct sockaddr_in broadcast = {
  .sin_family = AF_INET,
  .sin_addr = {INADDR_BROADCAST},
};


#if LOG_LEVEL >= LOG_DEBUG
void printf_dhcp(dhcp_packet* packet) {
  char* ciaddr_str = (char*) malloc(INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(packet->ciaddr.s_addr), ciaddr_str, INET_ADDRSTRLEN);

  char* yiaddr_str = (char*) malloc(INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(packet->yiaddr.s_addr), yiaddr_str, INET_ADDRSTRLEN);

  char* giaddr_str = (char*) malloc(INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(packet->giaddr.s_addr), giaddr_str, INET_ADDRSTRLEN);

  char* siaddr_str = (char*) malloc(INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(packet->siaddr.s_addr), siaddr_str, INET_ADDRSTRLEN);

  DEBUG("BOOTP [ op %i, htype %i, hlen %i, hops %i, xid %lu, secs %i, flags %i, ciaddr %s, yiaddr %s, siaddr %s, giaddr %s, sname: %s, file: %s ]\n"
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
      DEBUG("DHCP OPTION [ code %i, length %i, value %i ]\n", option->code, option->len, option->payload[0]);
    } else if (option->code == DHCP_CODE_PARAMETER_REQUEST_LIST) {
      DEBUG("DHCP OPTION [ code %i, length %i, value ", option->code, option->len);

      for (int k = 0; k < option->len; k++) {
        LOG("%i ", option->payload[k]);
      }

      LOG("]\n");
    } else {
      DEBUG("DHCP OPTION [ code %i, length %i ]\n", option->code, option->len);
    }

    option++;
  }
}
# else 
#define printf_dhcp(packet) {}
# endif


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
      WARNING("DHCP options ended improperly, possible broken client.\n");
      return -4;
    }

    if (option + option[1] + 2 > buffer + len) {
      // Error: Malformed dhcp options
      WARNING("DHCP options smaller than len of last option suggest, possible broken client.\n");
      return -5;
    } else {
      option += (uint8_t) option[1] + 2;
      options++;
    }
  }

  if (dhcp_message_type != 1) {
    INFO("Message contains no message type - invalid!\n");
    return -6;
  }

  if (dhcp_request_list != 1) {
    DEBUG("Message contains no dhcp request list - broken client?\n");
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

#if LOG_LEVEL >= LOG_INFO
  printf_dhcp(packet);
#endif

  return 0;
}

int dhcp_packet_send(int socket, dhcp_packet* packet) {
  DEBUG("dhcp_packet_send(%i, dhcp_packet)\n", socket);
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

  int ret = sendto(socket, buffer, _dhcp_packet_len(packet), 0, (struct sockaddr*)&broadcast, sizeof(broadcast));

  if (ret < 0) {
    perror("sendto");
    printf("Err: %i\n", errno);
  }

  free(buffer);

  return 0;
}

int dhcp_packet_copy(dhcp_packet* dest, dhcp_packet* src) {
  memcpy(dest, src, sizeof(struct dhcp_packet));
  dest->options = (struct dhcp_option*) calloc(src->options_len, sizeof(struct dhcp_option));

  if (dest->options == NULL) {
    return 1;
  }

  dhcp_option* src_option = src->options;
  dhcp_option* dest_option = dest->options;

  for (; src_option < src->options + src->options_len; src_option++) {
    uint8_t* dest_payload = (uint8_t*) calloc(src_option->len, sizeof(uint8_t));
    memcpy(dest_payload, src_option->payload, src_option->len);
    dest_option->code = src_option->code;
    dest_option->len = src_option->len;
    dest_option->payload = dest_payload;
    dest_option++;
  }

  return 0;
}

int dhcp_packet_list_add(dhcp_packet_list* list, dhcp_packet* packet) {
  time_t now = time(NULL);
  // Save dhcp packet, for further actions, later.
  dhcp_packet_list* tmp = calloc(1, sizeof(dhcp_packet_list));
  dhcp_packet* copy = calloc(1,sizeof(dhcp_packet));
  if ( !tmp ) {
    ERROR("dhcp_packet_list_add( ... ) -> Unable to allocate memory");
    return 1;
  }
  dhcp_packet_copy(copy, packet);
  tmp->packet = copy;
  tmp->packet->timeout = now + 120;
  list_add_tail((&tmp->list), &(list->list));
  return 0;
}

dhcp_packet* dhcp_packet_list_find(dhcp_packet_list* list, uint32_t xid, uint8_t* chaddr) {
  DEBUG("dhcp_packet_list_find(list,xid:%u,chaddr)\n", xid);
  struct list_head* pos, *q;
  dhcp_packet_list* tmp;

  list_for_each_safe(pos, q, &list->list) {
    tmp = list_entry(pos, dhcp_packet_list, list);
    if (tmp->packet->xid == xid) {
      if (memcmp(tmp->packet->chaddr, chaddr, 16) == 0) {
        DEBUG("dhcp_packet_list_find( ... ) -> packet found\n");
        list_del(pos);
        dhcp_packet* packet = tmp->packet;
        free(tmp);
        return packet;
      }
    } else {
      DEBUG("dhcp_packet_list_find( ... ): Packet (%u)\n", tmp->packet->xid);
    }
  }
  DEBUG("dhcp_packet_list_find( ... ) -> No matching packet found\n");
  return NULL;
}

void dhcp_packet_list_free(dhcp_packet_list* list) {
  DEBUG("dhcp_packet_list_free(list)\n");
  struct list_head* pos, *q;
  dhcp_packet_list* tmp;

  list_for_each_safe(pos, q, &list->list) {
    tmp = list_entry(pos, dhcp_packet_list, list);
    dhcp_packet* packet = tmp->packet;
    list_del(pos);
    dhcp_packet_free(packet, 1);
    free(packet);
    free(tmp);
  }
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

void dhcp_packet_list_timeout(dhcp_packet_list* list) {
  DEBUG("dhcp_packet_list_timeout(list)\n");
  struct list_head* pos, *q;
  dhcp_packet_list* tmp;
  time_t now = time(NULL);

  list_for_each_safe(pos, q, &list->list) {
    tmp = list_entry(pos, dhcp_packet_list, list);

    if (tmp->packet->timeout < now) {
      dhcp_packet* packet = tmp->packet;
      list_del(pos);
      dhcp_packet_free(packet, 1);
      free(tmp);
      DEBUG("dhcp_packet_list_timeout( ... ): drop packet from cache\n");
    }
  }
}
