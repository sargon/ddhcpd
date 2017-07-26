#include "control.h"
#include "logger.h"
#include "block.h"
#include "dhcp_options.h"

int handle_command(int socket, uint8_t* buffer, int msglen, ddhcp_block* blocks, ddhcp_config* config) {
  // TODO Rethink command handling and command design
  config->block_size = config->block_size;

  DEBUG("handle_command(socket, %u, %i, blocks, config)\n", buffer[0], msglen);

  if (msglen == 0) {
    WARNING("handle_command(...) -> zero length command received\n");
  }

  // TODO Avoid magic numbers for commands
  switch (buffer[0]) {
  case 1:
    if (msglen != 1) {
      DEBUG("handle_command(...) -> message length mismatch\n");
      return -2;
    }

    DEBUG("handle_command(...) -> show block status\n");
    block_show_status(socket, blocks, config);
    return 0;

  case 2:
    if (msglen != 1) {
      DEBUG("handle_command(...) -> message length mismatch\n");
      return -2;
    }

    DEBUG("handle_command(...) -> show dhcp options\n");
    dhcp_options_show(socket, &config->options);
    return 0;

  case 3:
    DEBUG("handle_command(...) -> set dhcp option\n");

    if (msglen < 3) {
      DEBUG("handle_command(...) -> message not long enought\n");
      return -2;
    }

    if (buffer[2] > msglen - 3) {
      DEBUG("handle_command(...) -> message not long enought\n");
      return -2;
    }

    dhcp_option* option = (dhcp_option*) calloc(sizeof(dhcp_option), 1);
    option->code = buffer[1];
    option->len = buffer[2];
    printf("%i:%i\n", buffer[1], buffer[2]);
    option->payload = (uint8_t*) calloc(sizeof(uint8_t), option->len);

    memcpy(option->payload, buffer + 3, option->len);

    set_option_in_store(&config->options, option);
    return 0;

  default:
    WARNING("handle_command(...) -> unknown command\n");
  }

  return -1;
}

