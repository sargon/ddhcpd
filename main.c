#include <arpa/inet.h>
#include <assert.h>
#include <getopt.h>
#include <math.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>

#include "block.h"
#include "ddhcp.h"
#include "dhcp.h"
#include "dhcp_packet.h"
#include "logger.h"
#include "netsock.h"
#include "packet.h"
#include "tools.h"
#include "dhcp_options.h"
#include "control.h"
#include "version.h"

volatile int daemon_running = 0;

const int NET = 0;
const int NET_LEN = 10;

struct ddhcp_block* blocks;

void* get_in_addr(struct sockaddr* sa)
{
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
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

  dhcp_packet_list_timeout(&config->dhcp_packet_cache);
  DEBUG("house_keeping( ... ) finish\n\n");
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
    ERROR("%i", errsv);
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


int main(int argc, char** argv) {

  srand(time(NULL));

  ddhcp_config* config = (ddhcp_config*) calloc(sizeof(ddhcp_config), 1);
  config->block_size = 32;
  config->claiming_blocks_amount = 0;

  inet_aton("10.0.0.0", &config->prefix);
  config->prefix_len = 24;
  config->spare_blocks_needed = 1;
  config->block_timeout = 60;
  config->block_refresh_factor = 4;
  config->tentative_timeout = 15;
  config->control_path = "/tmp/ddhcpd_ctl";
  config->disable_dhcp = 0;

  // DHCP
  config->dhcp_port = 67;
  INIT_LIST_HEAD(&(config->options).list);

  INIT_LIST_HEAD(&(config->claiming_blocks).list);

  INIT_LIST_HEAD(&(config->dhcp_packet_cache).list);

  char* interface = "server0";
  char* interface_client = "client0";

  daemon_running = 2;

  int c;
  int show_usage = 0;
  int early_housekeeping = 0;

  while ((c = getopt(argc, argv, "C:c:i:St:dvDhLb:N:o:s:")) != -1) {
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

		case 'v':
			printf("Revision: %s\n",REVISION);
      return 0;
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

    case 'S':
      config->disable_dhcp = 1;
      break;

    case 'o':
      do {
        dhcp_option* option = parse_option();
        set_option_in_store(&config->options, option);
      } while (0);

      break;

    case 's':
      config->spare_blocks_needed = atoi(optarg);
      break;

    case 'C':
      config->control_path = optarg;
      break;

    default:
      printf("ARGC: %i\n", argc);
      show_usage = 1;
      break;
    }
  }

  if (show_usage) {
    printf("Usage: ddhcp [-h] [-d|-D] [-L] [-c CLT-IFACE|-S] [-i SRV-IFACE] [-t TENTATIVE-TIMEOUT]\n");
    printf("\n");
    printf("-h                     This usage information.\n");
    printf("-c CLT-IFACE           Interface on which requests from clients are handled\n");
    printf("-i SRV-IFACE           Interface on which different servers communicate\n");
    printf("-S                     no Client interface\n");
    printf("-t TENTATIVE           Time required for a block to be claimed\n");
    printf("-N NETWORK/CIDR        Network to announce and manage blocks in\n");
    printf("-o CODE:LEN:P1. .. .Pn DHCP Option with code,len and #len chars in decimal\n");
    printf("-b BLKSIZEPOW          Power over two of block size\n");
    printf("-s SPAREBLKS           Amount of spare blocks\n");
    printf("-L                     Deactivate learning phase\n");
    printf("-d                     Run in background and daemonize\n");
    printf("-D                     Run in foreground and log to console (default)\n");
    printf("-C CTRL_PATH           Path to control socket\n");
    printf("-v                     Print build revision\n");
    exit(0);
  }

  config->number_of_blocks = pow(2, (32 - config->prefix_len - ceil(log2(config->block_size))));

  if ( config->disable_dhcp ) {
    config->spare_blocks_needed = 0;
  }

  INFO("CONFIG: network=%s/%i\n", inet_ntoa(config->prefix), config->prefix_len);
  INFO("CONFIG: block_size=%i\n", config->block_size);
  INFO("CONFIG: #blocks=%i\n", config->number_of_blocks);
  INFO("CONFIG: #spare_blocks=%i\n", config->spare_blocks_needed);
  INFO("CONFIG: timeout=%i\n", config->block_timeout);
  INFO("CONFIG: refresh_factor=%i\n", config->block_refresh_factor);
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
  dhcp_options_init(config);

  // init network and event loops
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
  add_fd(efd, config->server_socket, EPOLLIN | EPOLLET);
  add_fd(efd, config->control_socket, EPOLLIN | EPOLLET);
  if ( config->disable_dhcp == 0 ) {
    add_fd(efd, config->client_socket, EPOLLIN | EPOLLET);
  }

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
      if ((events[i].events & EPOLLERR) ) {
        ERROR("Error in epoll: %i \n", errno);
        exit(1);
      } else if (config->server_socket == events[i].data.fd) {
        // DDHCP Roamed DHCP Requests
        struct sockaddr_in6 sender;
        socklen_t sender_len = sizeof sender;
        bytes = recvfrom(events[i].data.fd, buffer, 1500, 0, (struct sockaddr*) &sender, &sender_len);
        // TODO Error Handling
        #if LOG_LEVEL >= LOG_DEBUG
        char ipv6_sender[INET6_ADDRSTRLEN];
        DEBUG("Receive message from %s\n",
              inet_ntop(AF_INET6, get_in_addr((struct sockaddr*)&sender), ipv6_sender, INET6_ADDRSTRLEN));
        #endif
        ret = ntoh_mcast_packet(buffer, bytes, &packet);
        packet.sender = &sender;

        if (ret == 0) {
          switch (packet.command) {
          case DDHCP_MSG_RENEWLEASE:
            ddhcp_dhcp_renewlease(blocks, &packet, config);
            break;

          case DDHCP_MSG_LEASEACK:
            ddhcp_dhcp_leaseack(blocks, &packet, config);
            break;

          case DDHCP_MSG_LEASENAK:
            ddhcp_dhcp_leasenak(&packet, config);
            break;

          case DDHCP_MSG_RELEASE:
            ddhcp_dhcp_release(blocks, &packet, config);
            break;

          default:
            break;
          }
        } else {
          DEBUG("epoll_ret: %i\n", ret);
        }
      } else if (config->mcast_socket == events[i].data.fd) {
        // DDHCP Block Handling
        struct sockaddr_in6 sender;
        socklen_t sender_len = sizeof sender;
        while(1) {
        bytes = recvfrom(events[i].data.fd, buffer, 1500, 0, (struct sockaddr*) &sender, &sender_len);
        if ( bytes < 0 ) break;
        // TODO Error Handling
        #if LOG_LEVEL >= LOG_DEBUG
        char ipv6_sender[INET6_ADDRSTRLEN];
        DEBUG("Receive message from %s\n",
              inet_ntop(AF_INET6, get_in_addr((struct sockaddr*)&sender), ipv6_sender, INET6_ADDRSTRLEN));
        #endif
        ret = ntoh_mcast_packet(buffer, bytes, &packet);
        packet.sender = &sender;

        if (ret == 0) {
          switch (packet.command) {
          case DDHCP_MSG_UPDATECLAIM:
            ddhcp_block_process_claims(blocks, &packet, config);
            break;

          case DDHCP_MSG_INQUIRE:
            ddhcp_block_process_inquire(blocks, &packet, config);
            break;

          default:
            break;
          }

          free(packet.payload);
        } else {
          DEBUG("epoll_ret: %i\n", ret);
        }
        }
        house_keeping(blocks, config);
        need_house_keeping = 0;
      } else if (config->client_socket == events[i].data.fd) {
        // DHCP
        bytes = read(config->client_socket, buffer, 1500);

        // TODO Error Handling
        ret = ntoh_dhcp_packet(&dhcp_packet, buffer, bytes);

        if (ret == 0) {
          int message_type = dhcp_packet_message_type(&dhcp_packet);

          switch (message_type) {
          case DHCPDISCOVER:
            ret = dhcp_hdl_discover(config->client_socket, &dhcp_packet, blocks, config);

            if (ret == 1) {
              INFO("we need to inquire new blocks\n");
              need_house_keeping = 1;
            }

            break;

          case DHCPREQUEST:
            dhcp_hdl_request(config->client_socket, &dhcp_packet, blocks, config);
            break;

          case DHCPRELEASE:
            dhcp_hdl_release(&dhcp_packet, blocks, config);
            break;

          default:
            WARNING("Unknown DHCP message of type: %i\n", message_type);
            break;
          }

          if (dhcp_packet.options_len > 0) {
            free(dhcp_packet.options);
          }
        } else {
          WARNING("Malformed packet!? errcode: %i\n",ret);
        }
      } else if (config->control_socket == events[i].data.fd) {
        // Handle new control socket connections
        struct sockaddr_un client_fd;
        unsigned int len = sizeof(client_fd);
        config->client_control_socket = accept(config->control_socket, (struct sockaddr*) &client_fd, &len);
        //set_nonblocking(config->client_control_socket);
        add_fd(efd, config->client_control_socket, EPOLLIN | EPOLLET);
        DEBUG("ControlSocket: new connections\n");
      } else if (events[i].events & EPOLLIN) {
        // Handle commands comming over a control_socket
        bytes = read(events[i].data.fd, buffer, 1500);

        if (handle_command(events[i].data.fd, buffer, bytes, blocks, config) < 0) {
          ERROR("Malformed command\n");
        }

        del_fd(efd, events[i].data.fd, 0);
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
  dhcp_packet_list_free(&config->dhcp_packet_cache);

  close(config->mcast_socket);
  close(config->client_socket);
  close(config->control_socket);

  remove(config->control_path);

  free(config);
  return 0;
}
