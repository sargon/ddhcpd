#include "control.h"
#include "logger.h"
#include "block.h"
#include "dhcp_options.h"
#include "statistics.h"

extern int log_level;

int handle_command(int socket, uint8_t* buffer, ssize_t msglen, ddhcp_config* config) {
  if (msglen == 0) {
    DEBUG("handle_command(...): zero length command received\n");
    return -2;
  }

  // TODO Rethink command handling and command design
  DEBUG("handle_command(socket, cmd:%u, len:%i, blocks, config)\n", buffer[0], msglen);

  switch (buffer[0]) {
  case DDHCPCTL_BLOCK_SHOW:
    if (msglen != 1) {
      DEBUG("handle_command(...): message length mismatch\n");
      return -2;
    }

    DEBUG("handle_command(...): show block status\n");
    block_show_status(socket, config);
    return 0;

  case DDHCPCTL_DHCP_OPTIONS_SHOW:
    if (msglen != 1) {
      DEBUG("handle_command(...): message length mismatch\n");
      return -2;
    }

    DEBUG("handle_command(...): show dhcp options\n");
    dhcp_options_show(socket, config);
    return 0;

  case DDHCPCTL_STATISTICS:
    if (msglen != 1) {
      DEBUG("handle_command(...): message length mismatch\n");
    }

    DEBUG("handle_command(...): show statistics\n");
    statistics_show(socket, 0, config);
    return 0;

  case DDHCPCTL_STATISTICS_RESET:
    if (msglen != 1) {
      DEBUG("handle_command(...): message length mismatch\n");
    }

    DEBUG("handle_command(...): show statistics reset\n");
    statistics_show(socket, 1, config);
    return 0;

  case DDHCPCTL_DHCP_OPTION_SET:
    DEBUG("handle_command(...): set dhcp option\n");

    if (msglen < 3) {
      DEBUG("handle_command(...): message not long enough\n");
      return -2;
    }

    if (buffer[2] + 3ul > (size_t) msglen) {
      DEBUG("handle_command(...): message not long enough\n");
      return -2;
    }

    dhcp_option* option = (dhcp_option*) calloc(sizeof(dhcp_option), 1);

    if (!option) {
      WARNING("handle_command(...): Failed to allocate memory for dhcp option\n");
      return -1;
    }

    option->code = buffer[1];
    option->len = buffer[2];
    printf("%i:%i\n", buffer[1], buffer[2]);
    option->payload = (uint8_t*) calloc(sizeof(uint8_t), option->len);

    if (!option->payload) {
      WARNING("handle_command(...): Failed to allocate memory for dhcp option payload\n");
      free(option);
      return -1;
    }

    memcpy(option->payload, buffer + 3, option->len);

    set_option_in_store(&config->options, option);
    return 0;

  case DDHCPCTL_DHCP_OPTION_REMOVE:
    DEBUG("handle_command(...): remove dhcp option\n");

    if (msglen < 2) {
      DEBUG("handle_command(...): message not long enough\n");
      return -2;
    }

    uint8_t code = buffer[1];
    remove_option_in_store(&config->options, code);
    return 0;

  case DDHCPCTL_LOG_LEVEL_SET:
    DEBUG("handle_command(...): set log level\n");

    if (msglen < 2) {
      DEBUG("handle_command(...): message not long enough\n");
      return -2;
    }

    log_level = buffer[1];
    return 0;

  default:
    WARNING("handle_command(...): unknown command\n");
  }

  return -1;
}
