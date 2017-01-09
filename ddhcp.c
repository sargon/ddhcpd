#include <arpa/inet.h>
#include <assert.h>
#include <getopt.h>
#include <math.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "block.h"
#include "ddhcp.h"
#include "dhcp.h"
#include "dhcp_packet.h"
#include "logger.h"
#include "netsock.h"
#include "packet.h"
#include "tools.h"
#include "dhcp_options.h"

volatile int daemon_running = 0;

const int NET = 0;
const int NET_LEN = 10;

struct ddhcp_block* blocks;

int ddhcp_block_init(struct ddhcp_block** blocks, ddhcp_config* config) {
  assert(blocks);

  DEBUG("ddhcp_block_init( blocks, config)\n");
  *blocks = (struct ddhcp_block*) calloc(sizeof(struct ddhcp_block), config->number_of_blocks);

  if (! *blocks) {
    FATAL("ddhcp_block_init(...)-> Can't allocate memory for block structure\n");
    return 1;
  }

  time_t now = time(NULL);

  // TODO Maybe we should allocate number_of_blocks dhcp_lease_blocks previous
  //      and assign one here instead of NULL. Performance boost, Memory defrag?
  struct ddhcp_block* block = *blocks;

  for (uint32_t index = 0; index < config->number_of_blocks; index++) {
    block->index = index;
    block->state = DDHCP_FREE;
    addr_add(&config->prefix, &block->subnet, index * config->block_size);
    block->subnet_len = config->block_size;
    block->address = 0;
    block->timeout = now + config->block_timeout;
    block->claiming_counts = 0;
    block->addresses = NULL;
    block++;
  }

  return 0;
}

void ddhcp_block_process_claims(struct ddhcp_block* blocks, struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_block_process_claims( blocks, packet, config )\n");
  assert(packet->command == 1);
  time_t now = time(NULL);

  for (unsigned int i = 0 ; i < packet->count ; i++) {
    struct ddhcp_payload* claim = ((struct ddhcp_payload*) packet->payload) + i;
    uint32_t block_index = claim->block_index;

    if (block_index >= config->number_of_blocks) {
      WARNING("ddhcp_block_process_claims(...): Malformed block number\n");
    }

    if (blocks[block_index].state == DDHCP_OURS) {
      INFO("ddhcp_block_process_claims(...): node 0x%02x%02x%02x%02x%02x%02x%02x%02x claims our block %i\n", HEX_NODE_ID(packet->node_id), block_index);
      // TODO Decide when and if we reclaim this block
      //      Which node has more leases in this block, ..., how has the better node_id.
    } else {
      blocks[block_index].state = DDHCP_CLAIMED;
      blocks[block_index].timeout = now + claim->timeout;
      INFO("ddhcp_block_process_claims(...): node 0x%02x%02x%02x%02x%02x%02x%02x%02x claims block %i with ttl: %i\n", HEX_NODE_ID(packet->node_id), block_index, claim->timeout);
    }
  }
}

void ddhcp_block_process_inquire(struct ddhcp_block* blocks, struct ddhcp_mcast_packet* packet, ddhcp_config* config) {
  DEBUG("ddhcp_block_process_inquire( blocks, packet, config )\n");
  assert(packet->command == 2);
  time_t now = time(NULL);

  for (unsigned int i = 0 ; i < packet->count ; i++) {
    struct ddhcp_payload* tmp = ((struct ddhcp_payload*) packet->payload) + i;

    if (tmp->block_index >= config->number_of_blocks) {
      WARNING("ddhcp_block_process_inquire(...): Malformed block number\n");
      continue;
    }

    INFO("ddhcp_block_process_inquire(...): node 0x%02x%02x%02x%02x%02x%02x%02x%02x inquires block %i\n", HEX_NODE_ID(packet->node_id), tmp->block_index);

    if (blocks[tmp->block_index].state == DDHCP_OURS) {
      // Update Claims
      INFO("ddhcp_block_process_inquire(...): block %i is ours notify network", tmp->block_index);
      blocks[tmp->block_index].timeout = 0;
      block_update_claims(blocks, 0, config);
    } else if (blocks[tmp->block_index].state == DDHCP_CLAIMING) {
      INFO("ddhcp_block_process_inquire(...): we are interested in block %i also\n", tmp->block_index);

      // QUESTION Why do we need multiple states for the same process?
      if (NODE_ID_CMP(packet->node_id, config->node_id)) {
        INFO("ddhcp_block_process_inquire(...): .. but other node wins.\n");
        blocks[tmp->block_index].state = DDHCP_TENTATIVE;
        blocks[tmp->block_index].timeout = now + config->tentative_timeout;
      }

      // otherwise keep inquiring, the other node should see our inquires and step back.
    } else {
      INFO("ddhcp_block_process_inquire(...): set block %i to tentative \n", tmp->block_index);
      blocks[tmp->block_index].state = DDHCP_TENTATIVE;
      blocks[tmp->block_index].timeout = now + config->tentative_timeout;
    }
  }
}

/**
 * House Keeping
 *
 * - Free timed-out DHCP leases.
 * - Refresh timed-out blocks.
 * + Claim new blocks if we are low on spare leases.
 * + Update our claims.
 */
void house_keeping(ddhcp_block* blocks, ddhcp_config* config) {
  DEBUG("house_keeping( blocks, config )\n");
  block_check_timeouts(blocks, config);
  int spares = block_num_free_leases(blocks, config);
  int spare_blocks = ceil((double) spares / (double) config->block_size);
  int blocks_needed = config->spare_blocks_needed - spare_blocks;
  block_claim(blocks, blocks_needed, config);
  block_update_claims(blocks, blocks_needed, config);
}

/**
 * Initialize DHCP options
 */
void init_dhcp_options(ddhcp_config* config) {
  dhcp_option* option;
  uint8_t pl = config->prefix_len;

  if (! has_in_option_store(&config->options, DHCP_CODE_SUBNET_MASK)) {
    // subnet mask
    option = (dhcp_option*) calloc(sizeof(dhcp_option), 1);
    option->code = DHCP_CODE_SUBNET_MASK;
    option->len = 4;
    option->payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4);
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
    option->payload = (uint8_t*) malloc(sizeof(uint8_t) * 4);
    option->payload[0] = 0;
    option->payload[1] = 0;
    option->payload[2] = 0;
    option->payload[3] = 0;

    set_option_in_store(&config->options, option);
  }

  /** Deactivate uneducated default router value
  option = (dhcp_option*) malloc(sizeof(dhcp_option));
  option->code = DHCP_CODE_ROUTER;
  option->len = 4;
  option->payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4 );
  // TODO Configure this throught socket
  option->payload[0] = 10;
  option->payload[1] = 0;
  option->payload[2] = 0;
  option->payload[3] = 1;

  set_option_in_store( &config->options, option );
  */

  if (! has_in_option_store(&config->options, DHCP_CODE_BROADCAST_ADDRESS)) {
    option = (dhcp_option*) malloc(sizeof(dhcp_option));
    option->code = DHCP_CODE_BROADCAST_ADDRESS;
    option->len = 4;
    option->payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4);
    option->payload[0] = (uint8_t) config->prefix.s_addr | ((1 << min(max(8 - pl, 0), 8)) - 1);
    option->payload[1] = (((uint8_t*) &config->prefix.s_addr)[1]) | ((1 << min(max(16 - pl, 0), 8)) - 1);
    option->payload[2] = (((uint8_t*) &config->prefix.s_addr)[2]) | ((1 << min(max(24 - pl, 0), 8)) - 1);
    option->payload[3] = (((uint8_t*) &config->prefix.s_addr)[3]) | ((1 << min(max(32 - pl, 0), 8)) - 1);

    set_option_in_store(&config->options, option);
  }

  if (! has_in_option_store(&config->options, DHCP_CODE_SERVER_IDENTIFIER)) {
    option = (dhcp_option*) malloc(sizeof(dhcp_option));
    option->code = DHCP_CODE_SERVER_IDENTIFIER;
    option->len = 4;
    option->payload = (uint8_t*)  malloc(sizeof(uint8_t) * 4);
    // TODO Check interface for address
    memcpy(option->payload, &config->prefix.s_addr, 4);
    //option->payload[0] = 10;
    //option->payload[1] = 0;
    //option->payload[2] = 0;
    option->payload[3] = 1;

    set_option_in_store(&config->options, option);
  }

}

void add_fd(int efd, int fd, uint32_t events) {
  struct epoll_event event = { 0 };
  event.data.fd = fd;
  event.events = events;

  int s = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event);

  if (s == -1) {
    exit(1);   //("epoll_ctl");
  }
}

void del_fd(int efd, int fd, uint32_t events) {
  struct epoll_event event = { 0 };
  event.data.fd = fd;
  event.events = events;

  int s = epoll_ctl(efd, EPOLL_CTL_DEL, fd, &event);
  perror("epoll_ctl");

  if (s < 0) {
    int errsv = errno;
    printf("%i", errsv);
    perror("epoll_ctl");
    exit(1);   //("epoll_ctl");
  }
}

uint32_t get_loop_timeout(ddhcp_config* config) {
  //Multiply by 500 to convert the timeout value given in seconds
  //into milliseconds AND dividing the value by two at the same time.
  //The integer overflow occuring for timeouts greater than 99.4 days is ignored here.
  return floor(config->tentative_timeout * 500);
}

typedef void (*sighandler_t)(int);

static sighandler_t
handle_signal(int sig_nr, sighandler_t signalhandler) {
  struct sigaction new_sig, old_sig;
  new_sig.sa_handler = signalhandler;
  sigemptyset(&new_sig.sa_mask);
  new_sig.sa_flags = SA_RESTART;

  if (sigaction(sig_nr, &new_sig, &old_sig) < 0) {
    return SIG_ERR;
  }

  return old_sig.sa_handler;
}

void handle_signal_terminate(int sig_nr) {
  if (SIGINT == sig_nr) {
    daemon_running = 0;
  } else if (SIGTERM == sig_nr) {
    daemon_running = 0;
  }
}


int handle_command(int socket, uint8_t* buffer, int msglen, ddhcp_block* blocks, ddhcp_config* config) {
  // TODO Rethink command handling and command design
  config->block_size = config->block_size;

  DEBUG("handle_command(socket, %u, %i, blocks, config)\n", buffer[0], msglen);

  if (msglen == 0) {
    WARNING("handle_command(...) -> zero length command received\n");
  }

  switch (buffer[0]) {
  case 1:
    DEBUG("handle_command(...) -> show block status\n");
    block_show_status(socket, blocks, config);
    return 0;

  case 2:
    DEBUG("handle_command(...) -> show dhcp options\n");
    dhcp_options_show(socket, &config->options);
    return 0;

  case 3:
    DEBUG("handle_command(...) -> set dhcp option\n");

    if (msglen > 3) {
      dhcp_option* option = (dhcp_option*) calloc(sizeof(dhcp_option), 1);
      option->code = buffer[1];
      option->len = buffer[2];
      printf("%i:%i\n", buffer[1], buffer[2]);
      option->payload = (uint8_t*)  calloc(sizeof(uint8_t), option->len);
      memcpy(option->payload, buffer + 3, option->len);

      set_option_in_store(&config->options, option);
      return 0;
    } else {
      DEBUG("handle_command(...) -> message not long enought\n");
      return -2;
    }

  default:
    WARNING("handle_command(...) -> unknown command\n");
  }

  return -1;
}

int main(int argc, char** argv) {

  srand(time(NULL));

  ddhcp_config* config = (ddhcp_config*) calloc(sizeof(ddhcp_config), 1);
  config->block_size = 32;
  config->claiming_blocks_amount = 0;

  inet_aton("10.0.0.0", &config->prefix);
  config->prefix_len = 24;
  config->spare_blocks_needed = 1;
  config->block_timeout = 30;
  config->tentative_timeout = 15;
  config->control_path = "/tmp/ddhcpd_ctl";

  // DHCP
  config->dhcp_port = 67;
  INIT_LIST_HEAD(&(config->options).list);

  INIT_LIST_HEAD(&(config->claiming_blocks).list);

  char* interface = "server0";
  char* interface_client = "client0";

  daemon_running = 2;

  int c;
  int show_usage = 0;
  int early_housekeeping = 0;

  while ((c = getopt(argc, argv, "c:i:t:dDhLb:N:o:s:")) != -1) {
    switch (c) {
    case 'i':
      interface = optarg;
      break;

    case 'c':
      interface_client = optarg;
      break;

    case 'b':
      config->block_size = 1 << atoi(optarg);
      break;

    case 't':
      config->tentative_timeout = atoi(optarg);
      break;

    case 'd':
      daemon_running = 1;
      break;

    case 'D':
      //We pretend we are normally running, just in another mode
      daemon_running = 2;
      break;

    case 'h':
      show_usage = 1;
      break;

    case 'L':
      early_housekeeping = 1;
      break;

    case 'N':
      do {
        // TODO Split prefix and cidr
        size_t optlen = strlen(optarg);
        char* cidr = strchr(optarg, '/');

        if (cidr == NULL) {
          ERROR("Malformed network '%s'\n", optarg);
          exit(1);
        }

        if (cidr == optarg + optlen - 1) {
          ERROR("Malformed network '%s'\n", optarg);
          exit(1);
        }

        cidr[0] = '\0';
        cidr++;
        inet_aton(optarg, &config->prefix);
        config->prefix_len = atoi(cidr);

        if (config->prefix_len < 8) {
          ERROR("Are you the internet, cidr less than 8?!\n");
          exit(1);
        }
      } while (0);

      break;

    case 'o':
      do {
        //size_t optlen = strlen(optarg);

        char* len_s = strchr(optarg, ';');

        if (len_s == NULL) {
          ERROR("Malformed dhcp option '%s'\n", optarg);
          exit(1);
        }

        len_s++[0] = '\0';

        char* payload_s = strchr(len_s, ';');

        if (payload_s == NULL) {
          ERROR("Malformed dhcp option '%s'\n", optarg);
          exit(1);
        }

        payload_s++[0] = '\0';

        uint8_t len = atoi(len_s);
        uint8_t code = atoi(optarg);

        dhcp_option* option = (dhcp_option*) malloc(sizeof(dhcp_option));
        option->code = code;
        option->len = len;
        option->payload = (uint8_t*)  malloc(sizeof(uint8_t) * len);

        for (int i = 0 ; i < len; i++) {
          char* next_payload_s = strchr(payload_s, ',');

          if (next_payload_s == NULL && i < len - 1) {
            ERROR("Malformed dhcp option '%s' to few payload\n", optarg);
            exit(1);
          }

          if (i < len - 1) {
            next_payload_s++[0] = '\0';
          }

          uint8_t payload = atoi(payload_s);
          option->payload[i] = payload;
          payload_s = next_payload_s;
        }

        set_option_in_store(&config->options, option);
      } while (0);

      break;

    case 's':
      config->spare_blocks_needed = atoi(optarg);
      break;

    default:
      printf("ARGC: %i\n", argc);
      show_usage = 1;
      break;
    }
  }

  if (show_usage) {
    printf("Usage: ddhcp [-h] [-d|-D] [-L] [-c CLT-IFACE] [-i SRV-IFACE] [-t TENTATIVE-TIMEOUT]\n");
    printf("\n");
    printf("-h                   This usage information.\n");
    printf("-c CLT-IFACE         Interface on which requests from clients are handled\n");
    printf("-i SRV-IFACE         Interface on which different servers communicate\n");
    printf("-t TENTATIVE         Time required for a block to be claimed\n");
    printf("-N NETWORK/CIDR      Network to announce and manage blocks in\n");
    printf("-o CODE;LEN;P1,..,Pn DHCP Option with code,len and #len chars in decimal\n");
    printf("-b BLKSIZEPOW        Power over two of block size\n");
    printf("-s SPAREBLKS         Amount of spare blocks\n");
    printf("-L                   Deactivate learning phase\n");
    printf("-d                   Run in background and daemonize\n");
    printf("-D                   Run in foreground and log to console (default)\n");
    exit(0);
  }

  config->number_of_blocks = pow(2, (32 - config->prefix_len - ceil(log2(config->block_size))));

  INFO("CONFIG: network=%s/%i\n", inet_ntoa(config->prefix), config->prefix_len);
  INFO("CONFIG: block_size=%i\n", config->block_size);
  INFO("CONFIG: #blocks=%i\n", config->number_of_blocks);
  INFO("CONFIG: #spare_blocks=%i\n", config->spare_blocks_needed);
  INFO("CONFIG: timeout=%i\n", config->block_timeout);
  INFO("CONFIG: tentative_timeout=%i\n", config->tentative_timeout);
  INFO("CONFIG: client_interface=%s\n", interface_client);
  INFO("CONFIG: group_interface=%s\n", interface);

  //Register signal handlers
  handle_signal(SIGHUP, SIG_IGN);
  handle_signal(SIGINT, handle_signal_terminate);
  handle_signal(SIGTERM, handle_signal_terminate);

  //Daemonize if requested
  if (1 == daemon_running) {
    if (daemon(0, 0)) {
      perror("ddhcp");
      exit(1);
    }

    //Conflicts with existing logging
    //openlog("ddhcp", LOG_PID | LOG_CONS | LOG_NDELAY, LOG_DAEMON);
  }

  // init block stucture
  ddhcp_block_init(&blocks, config);
  init_dhcp_options(config);

  // init network and event loops
  // TODO
  if (netsock_open(interface, interface_client, config) == -1) {
    return 1;
  }

  if (control_open(config) == -1) {
    return 1;
  }

  uint8_t* buffer = (uint8_t*) malloc(sizeof(uint8_t) * 1500);
  struct ddhcp_mcast_packet packet;
  struct dhcp_packet dhcp_packet;
  int ret = 0, bytes = 0;

  int efd;
  int maxevents = 64;
  struct epoll_event* events;

  efd = epoll_create1(0);

  if (efd == -1) {
    perror("epoll_create");
    abort();
  }

  add_fd(efd, config->mcast_socket, EPOLLIN | EPOLLET);
  add_fd(efd, config->client_socket, EPOLLIN | EPOLLET);
  add_fd(efd, config->control_socket, EPOLLIN | EPOLLET);

  /* Buffer where events are returned */
  events = calloc(maxevents, sizeof(struct epoll_event));

  uint8_t need_house_keeping;
  uint32_t loop_timeout = config->loop_timeout = get_loop_timeout(config);

  if (early_housekeeping) {
    loop_timeout = 0;
  }

  INFO("loop timeout: %i msecs\n", get_loop_timeout(config));

  // TODO wait loop_timeout before first time housekeeping
  do {
    int n = epoll_wait(efd, events, maxevents, loop_timeout);

    if (n < 0) {
      perror("epoll error:");
    }

#if LOG_LEVEL >= LOG_DEBUG

    if (loop_timeout != config->loop_timeout) {
      DEBUG("Increase loop timeout from %i to %i\n", loop_timeout, config->loop_timeout);
    }

#endif

    loop_timeout = config->loop_timeout;
    need_house_keeping = 1;

    for (int i = 0; i < n; i++) {
      if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
        fprintf(stderr, "epoll error:%i \n", errno);
        close(events[i].data.fd);
      } else if (config->mcast_socket == events[i].data.fd) {
        bytes = read(config->mcast_socket, buffer, 1500);
        // TODO Error Handling
        ret = ntoh_mcast_packet(buffer, bytes, &packet);

        if (ret == 0) {
          switch (packet.command) {
          case DHCPDISCOVER:
            ddhcp_block_process_claims(blocks, &packet, config);
            break;

          case 2:
            ddhcp_block_process_inquire(blocks, &packet, config);

          default:
            break;
          }

          free(packet.payload);
        } else {
          printf("%i\n", ret);
        }

        house_keeping(blocks, config);
        need_house_keeping = 0;
      } else if (config->client_socket == events[i].data.fd) {
        bytes = read(config->client_socket, buffer, 1500);

        // TODO Error Handling
        ret = ntoh_dhcp_packet(&dhcp_packet, buffer, bytes);

        if (ret == 0) {
          int message_type = dhcp_packet_message_type(&dhcp_packet);

          switch (message_type) {
          case DHCPDISCOVER:
            ret = dhcp_discover(config->client_socket, &dhcp_packet, blocks, config);

            if (ret == 1) {
              INFO("we need to inquire new blocks\n");
              need_house_keeping = 1;
            }

            break;

          case DHCPREQUEST:
            dhcp_request(config->client_socket, &dhcp_packet, blocks, config);
            break;

          default:
            WARNING("Unknown DHCP message of type: %i\n", message_type);
            break;
          }

          if (dhcp_packet.options_len > 0) {
            free(dhcp_packet.options);
          }
        }
      } else if (config->control_socket == events[i].data.fd) {
        struct sockaddr_un client_fd;
        unsigned int len = sizeof(client_fd);
        config->client_control_socket = accept(config->control_socket, (struct sockaddr*) &client_fd, &len);
        //set_nonblocking(config->client_control_socket);
        add_fd(efd, config->client_control_socket, EPOLLIN | EPOLLET);
        printf("new connections\n");
      } else if (events[i].events & EPOLLIN) {
        bytes = read(events[i].data.fd, buffer, 1500);

        if (handle_command(events[i].data.fd, buffer, bytes, blocks, config) < 0) {
          ERROR("Malformed command\n");
        }

        close(events[i].data.fd);
      } else if (events[i].events & EPOLLHUP) {
        del_fd(efd, events[i].data.fd, EPOLLIN);
        close(events[i].data.fd);
      }
    }

    if (need_house_keeping) {
      house_keeping(blocks, config);
    }
  } while (daemon_running);

  // TODO free dhcp_leases
  free(events);

  ddhcp_block* block = blocks;

  for (uint32_t i = 0; i < config->number_of_blocks; i++) {
    block_free(block++);
  }

  block_free_claims(config);

  free(blocks);
  free(buffer);

  free_option_store(&config->options);

  close(config->mcast_socket);
  close(config->client_socket);
  close(config->control_socket);

  free(config);
  return 0;
}
