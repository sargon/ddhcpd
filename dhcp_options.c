
#include <stdlib.h>
#include <string.h>

#include "dhcp_options.h"
#include "list.h"
#include "logger.h"

dhcp_option* find_option(dhcp_option* options, uint8_t len, uint8_t code) {
  dhcp_option* option = options;

  for (; option < options + len ; option++) {
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

int find_option_parameter_request_list(dhcp_option* options, uint8_t len, uint8_t** requested) {
  dhcp_option* option = find_option(options, len, DHCP_CODE_PARAMETER_REQUEST_LIST);

  if (option) {
    *requested = (uint8_t*) option->payload;
    DEBUG("find_option_parameter_request_list(...) -> %i\n", option->len);
    return option->len;
  } else {
    *requested = NULL;
    DEBUG("find_option_parameter_request_list(...) -> %i\n", 0);
    return 0;
  }
}

uint8_t* find_option_requested_address(dhcp_option* options, uint8_t len) {
  dhcp_option* option = find_option(options, len, DHCP_CODE_REQUESTED_ADDRESS);

  if (option) {
    DEBUG("find_option_requested_address(...) -> found\n");
    return option->payload;
  } else {
    DEBUG("find_option_parameter_request_list(...) -> not found\n");
    return NULL;
  }
}

dhcp_option* find_in_option_store(dhcp_option_list* options, uint8_t code) {
  DEBUG("find_in_option_store( store, code: %i)\n", code);
  dhcp_option* option = NULL;
  struct list_head* pos, *q;
  dhcp_option_list* tmp;
  list_for_each_safe(pos, q, &options->list) {
    tmp = list_entry(pos, dhcp_option_list, list);
    option = tmp->option;

    if (option->code == code) {
      DEBUG("find_in_option_store(...) -> %i\n", code);
      return option;
    }
  }

  return NULL;
}

dhcp_option* set_option_in_store(dhcp_option_list* store, dhcp_option* option) {
  DEBUG("set_in_option_store( store, code/len: %i/%i)\n", option->code, option->len);
  dhcp_option* current = find_in_option_store(store, option->code);

  if (current != NULL) {
    // Replacing current with new option
    current->len = option->len;

    if (current->payload) {
      free(current->payload);
    }

    DEBUG("set_in_option_store(...) -> replace option\n");
    current->payload = option->payload;
    return current;
  } else {
    dhcp_option_list* tmp;
    tmp = (dhcp_option_list*) malloc(sizeof(dhcp_option_list));
    tmp->option = option;
    list_add_tail((&tmp->list), &(store->list));
    DEBUG("set_in_option_store(...) -> append option\n");
    return option;
  }
}

void free_option_store(dhcp_option_list* store) {
  struct list_head* pos, *q;
  dhcp_option_list* tmp;
  list_for_each_safe(pos, q, &store->list) {
    tmp = list_entry(pos, dhcp_option_list, list);
    dhcp_option* option = tmp->option;
    list_del(pos);

    if (option->len > 1) {
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

  if (! max_options) {
    *fullfil = NULL;
    return 0;
  }

  *fullfil = (dhcp_option*) calloc(sizeof(dhcp_option) , max_options + additional);

  for (uint8_t i = additional; i < max_options; i++) {
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

void dhcp_options_show(int fd, dhcp_option_list* store) {
  struct list_head* pos, *q;
  dhcp_option_list* tmp;
  list_for_each_safe(pos, q, &store->list) {
    tmp = list_entry(pos, dhcp_option_list, list);
    dhcp_option* option = tmp->option;
    dprintf(fd, "%i,%i:", option->code, option->len);

    for (int i = 0; i < option->len; i++) {
      dprintf(fd, " %u", option->payload[i]);
    }

    dprintf(fd, "\n");
  }
}
