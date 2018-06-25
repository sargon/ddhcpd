#include <stdlib.h>
#include <string.h>

#include "dhcp_options.h"
#include "list.h"
#include "logger.h"
#include "tools.h"

dhcp_option* find_option(dhcp_option* options, uint8_t len, uint8_t code) {
  dhcp_option* option = options;

  for (; option < options + len; option++) {
    if (option->code == code) {
      return option;
    }
  }

  return NULL;
}

int set_option(dhcp_option* options, uint8_t len, uint8_t code, uint8_t payload_len, uint8_t* payload) {
  DEBUG("set_option( options, len:%i, code:%i, payload_len:%i, payload)\n", len, code, payload_len);

  for (int i = len - 1; i >= 0; i--) {
    DEBUG("set_option(...) %i\n", i);
    dhcp_option* option = options + i;

    if (option->code == code || option->code == 0) {
      option->code = code;
      option->len = payload_len;
      option->payload = payload;

      DEBUG("set_option(...) -> set option at %i \n", i);

      return 0;
    }
  }

  DEBUG("set_option(...) -> failed\n");
  return 1;
}

int set_option_from_store(dhcp_option_list* store, dhcp_option* options, uint8_t len, uint8_t code) {
  dhcp_option* option = find_in_option_store(store, code);
  if (option == NULL) {
    DEBUG("set_option_from_store( ... ) -> Option %u not found in store\n", code);
    return 1;
  }
  return set_option(options,len,code,option->len,option->payload);
}

int find_option_parameter_request_list(dhcp_option* options, uint8_t len, uint8_t** requested) {
  dhcp_option* option = find_option(options, len, DHCP_CODE_PARAMETER_REQUEST_LIST);

  if (requested) {
    *requested = option ? (uint8_t*) option->payload : NULL;
  }

  int optlen = option ? option->len : 0;

  DEBUG("find_option_parameter_request_list(...) -> %i\n", optlen);

  return optlen;
}


uint8_t* find_option_requested_address(dhcp_option* options, uint8_t len) {
  dhcp_option* option = find_option(options, len, DHCP_CODE_REQUESTED_ADDRESS);

  DEBUG("find_option_requested_address(...) -> address %s\n", option ? "found" : "not found");

  return option ? option->payload : NULL;
}

dhcp_option* find_in_option_store(dhcp_option_list* options, uint8_t code) {
  DEBUG("find_in_option_store( store, code: %i)\n", code);

  dhcp_option* option = NULL;
  dhcp_option_list* tmp;

  list_for_each_entry(tmp, &options->list, list) {
    option = tmp->option;

    if (option->code == code) {
      DEBUG("find_in_option_store(...) -> %i\n", code);
      return option;
    }
  }

  return NULL;
}

uint32_t find_in_option_store_address_lease_time(dhcp_option_list* options) {
  dhcp_option* lease_time_opt = find_in_option_store(options,DHCP_CODE_ADDRESS_LEASE_TIME);
  if ( lease_time_opt != NULL) {
    uint32_t buf = 0; 
    memcpy(&buf,lease_time_opt->payload,4);
    return ntohl(buf);
  } 
  return 0;
}

dhcp_option* set_option_in_store(dhcp_option_list* store, dhcp_option* option) {
  DEBUG("set_in_option_store( store, code/len: %i/%i)\n", option->code, option->len);

  dhcp_option* current = find_in_option_store(store, option->code);

  if (current != NULL) {
    DEBUG("set_in_option_store(...) -> replace option\n");

    // Replacing current with new option

    if (current->payload) {
      free(current->payload);
    }

    current->len = option->len;
    current->payload = option->payload;

    return current;
  } else {
    DEBUG("set_in_option_store(...) -> append option\n");

    dhcp_option_list* tmp;
    tmp = (dhcp_option_list*) malloc(sizeof(dhcp_option_list));
    tmp->option = option;

    list_add_tail((&tmp->list), &(store->list));

    return option;
  }
}

void remove_option_in_store(dhcp_option_list* store, uint8_t code) {
  struct list_head* pos, *q;
  dhcp_option_list* tmp;

  list_for_each_safe(pos, q, &store->list) {
    tmp = list_entry(pos, dhcp_option_list, list);
    dhcp_option* option = tmp->option;
    if ( option->code == code ) {
      list_del(pos);

      if (option->payload) {
        free(option->payload);
      }

      free(option);
      free(tmp);
    }
  }
}

void free_option_store(dhcp_option_list* store) {
  struct list_head* pos, *q;
  dhcp_option_list* tmp;

  list_for_each_safe(pos, q, &store->list) {
    tmp = list_entry(pos, dhcp_option_list, list);
    dhcp_option* option = tmp->option;
    list_del(pos);

    if (option->payload) {
      free(option->payload);
    }

    free(option);
    free(tmp);
  }
}

dhcp_option* remove_option_from_store(dhcp_option_list* store, uint8_t code);

int fill_options(dhcp_option* options, uint8_t len, dhcp_option_list* option_store, uint8_t additional, dhcp_option** fullfil) {
  int num_found_options = 0;

  uint8_t* requested = NULL;
  int8_t max_options = find_option_parameter_request_list(options, len, &requested);

  *fullfil = (dhcp_option*) calloc(sizeof(dhcp_option), max_options + additional);

  if (! max_options) {
    return additional;
  }

  for (uint8_t i = 0; i < max_options; i++) {
    uint8_t code = requested[i];
    // LOOP thought option_store
    dhcp_option* option = find_in_option_store(option_store, code);

    if (option != NULL) {
      memcpy(*fullfil + num_found_options, option, sizeof(dhcp_option));
      num_found_options++;
    }
  }

  return num_found_options + additional;
}

void dhcp_options_show(int fd, ddhcp_config* config) {
  struct list_head* pos, *q;
  dhcp_option_list* tmp;
  dhcp_option_list* store = &config->options;

  dprintf(fd,"DHCP Lease Time: %u\n\n",find_in_option_store_address_lease_time(&config->options));
  dprintf(fd,"DHCP Disabled: %u\n",config->disable_dhcp);
  dprintf(fd,"DHCP Option Store\ncode\tlen\tpayload\n");
  list_for_each_safe(pos, q, &store->list) {
    tmp = list_entry(pos, dhcp_option_list, list);
    dhcp_option* option = tmp->option;
    dprintf(fd, "%i\t%i\t", option->code, option->len);

    for (int i = 0; i < option->len; i++) {
      dprintf(fd, "%u\t", option->payload[i]);
    }

    dprintf(fd, "\n");
  }
}

void dhcp_options_init(ddhcp_config* config) {
  dhcp_option* option;
  uint8_t pl = config->prefix_len;

  if (! has_in_option_store(&config->options, DHCP_CODE_SUBNET_MASK)) {
    // subnet mask
    option = (dhcp_option*) calloc(sizeof(dhcp_option), 1);
    option->code = DHCP_CODE_SUBNET_MASK;
    option->len = 4;
    option->payload = (uint8_t*) calloc(sizeof(uint8_t), 4);
    option->payload[0] = 256 - (256 >> min(max(pl -  0, 0), 8));
    option->payload[1] = 256 - (256 >> min(max(pl -  8, 0), 8));
    option->payload[2] = 256 - (256 >> min(max(pl - 16, 0), 8));
    option->payload[3] = 256 - (256 >> min(max(pl - 24, 0), 8));

    set_option_in_store(&config->options, option);
  }

  if (! has_in_option_store(&config->options, DHCP_CODE_TIME_OFFSET)) {
    option = (dhcp_option*) malloc(sizeof(dhcp_option));
    option->code = DHCP_CODE_TIME_OFFSET;
    option->len = 4;
    option->payload = (uint8_t*) calloc(sizeof(uint8_t), 4);
    option->payload[0] = 0;
    option->payload[1] = 0;
    option->payload[2] = 0;
    option->payload[3] = 0;

    set_option_in_store(&config->options, option);
  }

  if (! has_in_option_store(&config->options, DHCP_CODE_BROADCAST_ADDRESS)) {
    option = (dhcp_option*) malloc(sizeof(dhcp_option));
    option->code = DHCP_CODE_BROADCAST_ADDRESS;
    option->len = 4;
    option->payload = (uint8_t*) calloc(sizeof(uint8_t), 4);
    option->payload[0] = (((uint8_t*) &config->prefix.s_addr)[0]) | ((1 << min(max( 8 - pl, 0), 8)) - 1);
    option->payload[1] = (((uint8_t*) &config->prefix.s_addr)[1]) | ((1 << min(max(16 - pl, 0), 8)) - 1);
    option->payload[2] = (((uint8_t*) &config->prefix.s_addr)[2]) | ((1 << min(max(24 - pl, 0), 8)) - 1);
    option->payload[3] = (((uint8_t*) &config->prefix.s_addr)[3]) | ((1 << min(max(32 - pl, 0), 8)) - 1);

    set_option_in_store(&config->options, option);
  }

  if (! has_in_option_store(&config->options, DHCP_CODE_ADDRESS_LEASE_TIME)) {
    option = (dhcp_option*) calloc(sizeof(dhcp_option), 1);
    option->code = DHCP_CODE_ADDRESS_LEASE_TIME;
    option->len = 4;
    option->payload = (uint8_t*) calloc(sizeof(uint8_t), 4);
    // 300 ms ~ 5min
    option->payload[0] = 0x00;
    option->payload[1] = 0x00;
    option->payload[2] = 0x01;
    option->payload[3] = 0x2c; 

    set_option_in_store(&config->options, option);
  }

  if (! has_in_option_store(&config->options, DHCP_CODE_SERVER_IDENTIFIER)) {
    option = (dhcp_option*) malloc(sizeof(dhcp_option));
    option->code = DHCP_CODE_SERVER_IDENTIFIER;
    option->len = 4;
    option->payload = (uint8_t*) calloc(sizeof(uint8_t), 4);
    // TODO Check interface for address
    memcpy(option->payload, &config->prefix.s_addr, 4);
    //option->payload[0] = 10;
    //option->payload[1] = 0;
    //option->payload[2] = 0;
    option->payload[3] = 1;

    set_option_in_store(&config->options, option);
  }

}
