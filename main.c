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
#include <limits.h>

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
#include "hook.h"
#include "statistics.h"

volatile int daemon_running = 0;

extern int log_level;

const int NET = 0;
const int NET_LEN = 10;

in_addr_storage get_in_addr(struct sockaddr* sa)
{
  struct sockaddr_in in;
  struct sockaddr_in6 in6;
  in_addr_storage in_store = { 0 };
  if (sa->sa_family == AF_INET) {
    memcpy(&in, sa, sizeof(in));
    in_store.in_addr = in.sin_addr;
  }

  if (sa->sa_family == AF_INET6) {
    memcpy(&in6, sa, sizeof(in6));
    in_store.in6_addr = in6.sin6_addr;
  }
  return in_store;
}

/**
 * House Keeping
 *
 * - Free timed-out DHCP leases.
 * - Refresh timed-out blocks.
 * + Claim new blocks if we are low on spare leases.
 * + Update our claims.
 */
void house_keeping(ddhcp_config* config) {
  DEBUG("house_keeping(blocks,config)\n");
  block_check_timeouts(config);

  uint32_t spares = block_num_free_leases(config);
  uint32_t spare_blocks = spares / config->block_size;
  int32_t blocks_needed = (int32_t)config->spare_blocks_needed - (int32_t)spare_blocks;

  block_claim(blocks_needed, config);
  block_update_claims(blocks_needed, config);

  dhcp_packet_list_timeout(&config->dhcp_packet_cache);
  DEBUG("house_keeping(...) finish\n\n");
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

  if (s < 0) {
    int errsv = errno;
    FATAL("%i", errsv);
    perror("epoll_ctl");
    exit(1);   //("epoll_ctl");
  }
}

uint32_t get_loop_timeout(ddhcp_config* config) {
  //Multiply by 500 to convert the timeout value given in seconds
  //into milliseconds AND dividing the value by two at the same time.
  //The integer overflow occuring for timeouts greater than 99.4 days is ignored here.
  return config->tentative_timeout * 500u;
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

  srand((unsigned int)time(NULL));

  ddhcp_config config;
  config.block_size = 32;
  config.claiming_blocks_amount = 0;

  inet_aton("10.0.0.0", &config.prefix);
  config.prefix_len = 24;
  config.spare_blocks_needed = 1;
  config.block_timeout = 60;
  config.block_refresh_factor = 4;
  config.tentative_timeout = 15;
  config.control_path = (char*)"/tmp/ddhcpd_ctl";
  config.disable_dhcp = 0;

  config.hook_command = NULL;

#ifdef DDHCPD_STATISTICS
  memset(config.statistics, 0, sizeof(long int) * STAT_NUM_OF_FIELDS);
#endif

  // DHCP
  config.dhcp_port = 67;
  INIT_LIST_HEAD(&config.options);

  INIT_LIST_HEAD(&config.claiming_blocks);

  INIT_LIST_HEAD(&config.dhcp_packet_cache);

  char* interface = (char*)"server0";
  char* interface_client = (char*)"client0";

  daemon_running = 2;

  int c;
  int show_usage = 0;
  int early_housekeeping = 0;

  while ((c = getopt(argc, argv, "C:c:i:St:dvVDhLb:B:N:o:s:H:")) != -1) {
    switch (c) {
    case 'i':
      interface = optarg;
      break;

    case 'c':
      interface_client = optarg;
      break;

    case 'b':
      config.block_size = (uint8_t)(1 << atoi(optarg));
      break;

    case 'B':
      {
        unsigned long block_timeout = strtoul(optarg, NULL, 0);
        if(!block_timeout) {
          ERROR("Block timeout must be > 0\n");
          exit(1);
        }
        if(block_timeout == ULONG_MAX && errno) {
          ERROR("Failed to parse block timeout: %s(%d)\n", strerror(errno), errno);
          exit(1);
        }
        config.block_timeout = (uint16_t)block_timeout;
      }
      break;

    case 't':
      config.tentative_timeout = (uint16_t)atoi(optarg);
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

    case 'V':
      printf("Revision: %s\n", REVISION);
      return 0;

    case 'N':
      do {
        // TODO Split prefix and cidr
        size_t optlen = strlen(optarg);
        char* cidr = strchr(optarg, '/');

        if (!cidr) {
          ERROR("Malformed network '%s'\n", optarg);
          exit(1);
        }

        if (cidr == optarg + optlen - 1) {
          ERROR("Malformed network '%s'\n", optarg);
          exit(1);
        }

        cidr[0] = '\0';
        cidr++;
        inet_aton(optarg, &config.prefix);
        config.prefix_len = (uint8_t)atoi(cidr);

        if (config.prefix_len < 8) {
          ERROR("Are you the internet? CIDR less than 8 seems strange.\n");
          exit(1);
        }
      } while (0);
      break;

    case 'S':
      config.disable_dhcp = 1;
      break;

    case 'o':
      do {
        dhcp_option* option = parse_option();
        set_option_in_store(&config.options, option);
      } while (0);
      break;

    case 's':
      config.spare_blocks_needed = (uint8_t)atoi(optarg);
      break;

    case 'C':
      config.control_path = optarg;
      break;

    case 'H':
      config.hook_command = optarg;
      break;

    case 'v':
      if (log_level < LOG_LEVEL_MAX) {
        log_level++;
      }
      break;

    default:
      printf("ARGC: %i\n", argc);
      show_usage = 1;
      break;
    }
  }

  if (show_usage) {
    printf("Usage: %s [-h] [-d|-D] [-L] [-c CLT-IFACE|-S] [-i SRV-IFACE] [-t TENTATIVE-TIMEOUT] [-B BLOCK-TIMEOUT]\n", argv[0]);
    printf("\n");
    printf("-h                     This usage information.\n");
    printf("-c CLT-IFACE           Interface on which requests from clients are handled\n");
    printf("-i SRV-IFACE           Interface on which different servers communicate\n");
    printf("-S                     no Client interface\n");
    printf("-t TENTATIVE           Time required for a block to be claimed\n");
    printf("-N NETWORK/CIDR        Network to announce and manage blocks in\n");
    printf("-o CODE:LEN:P1. .. .Pn DHCP Option with code,len and #len chars in decimal\n");
    printf("-b BLKSIZEPOW          Power over two of block size\n");
    printf("-B TIMEOUT             Block timeout\n");
    printf("-s SPAREBLKS           Amount of spare blocks\n");
    printf("-L                     Deactivate learning phase\n");
    printf("-d                     Run in background and daemonize\n");
    printf("-D                     Run in foreground and log to console (default)\n");
    printf("-C CTRL_PATH           Path to control socket\n");
    printf("-H COMMAND             Hook to call on events\n");
    printf("-V                     Print build revision\n");
    printf("-v                     Increase verbosity, can be specified multiple times\n");
    exit(0);
  }

  if (log_level > LOG_LEVEL_LIMIT) {
    LOG("WARNING: Requested verbosity is higher than maximum supported by this build\n");
  }

  config.number_of_blocks = (uint32_t)pow(2u, (32u - config.prefix_len - ceil(log2(config.block_size))));

  if (config.disable_dhcp) {
    config.spare_blocks_needed = 0;
  }

  INFO("CONFIG: network=%s/%i\n", inet_ntoa(config.prefix), config.prefix_len);
  INFO("CONFIG: block_size=%i\n", config.block_size);
  INFO("CONFIG: #blocks=%i\n", config.number_of_blocks);
  INFO("CONFIG: #spare_blocks=%i\n", config.spare_blocks_needed);
  INFO("CONFIG: timeout=%i\n", config.block_timeout);
  INFO("CONFIG: refresh_factor=%i\n", config.block_refresh_factor);
  INFO("CONFIG: tentative_timeout=%i\n", config.tentative_timeout);
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
  ddhcp_block_init(&config);

  if (dhcp_options_init(&config)) {
    FATAL("Failed to allocate memory for option store\n");
    abort();
  }

  hook_init();

  // init network and event loops
  if (netsock_open(interface, interface_client, &config) == -1) {
    return 1;
  }

  if (control_open(&config) == -1) {
    return 1;
  }

  uint8_t* buffer = (uint8_t*) malloc(sizeof(uint8_t) * 1500);

  if (!buffer) {
    FATAL("Failed to allocate network buffer\n");
    abort();
  }

  ssize_t bytes = 0;

  int efd;
  size_t maxevents = 64;
  struct epoll_event* events;

  efd = epoll_create1(0);

  if (efd == -1) {
    perror("epoll_create");
    abort();
  }

  add_fd(efd, config.mcast_socket, EPOLLIN | EPOLLET);
  add_fd(efd, config.server_socket, EPOLLIN | EPOLLET);
  add_fd(efd, config.control_socket, EPOLLIN | EPOLLET);

  if (config.disable_dhcp == 0) {
    add_fd(efd, config.client_socket, EPOLLIN | EPOLLET);
  }

  /* Buffer where events are returned */
  events = calloc(maxevents, sizeof(struct epoll_event));

  if (!events) {
    FATAL("Failed to allocate event buffer\n");
    abort();
  }

  int need_house_keeping;
  uint32_t loop_timeout = config.loop_timeout = get_loop_timeout(&config);

  if (early_housekeeping) {
    loop_timeout = 0;
  }

  INFO("loop timeout: %i msecs\n", get_loop_timeout(&config));

  // TODO wait loop_timeout before first time housekeeping
  struct sockaddr_in6 sender;
  socklen_t sender_len = sizeof sender;

  do {
    int n = epoll_wait(efd, events, (int)maxevents, (int)loop_timeout);

    if (n < 0) {
      perror("epoll error:");
    }

#if LOG_LEVEL_LIMIT >= LOG_DEBUG

    if (loop_timeout != config.loop_timeout) {
      DEBUG("Increase loop timeout from %i to %i\n", loop_timeout, config.loop_timeout);
    }

#endif

    loop_timeout = config.loop_timeout;
    need_house_keeping = 1;

    for (int i = 0; i < n; i++) {
      if ((events[i].events & EPOLLERR)) {
        ERROR("Error in epoll: %i \n", errno);
        exit(1);
      } else if (config.server_socket == events[i].data.fd) {
        // DDHCP Roamed DHCP Requests
        ssize_t len;

        while ((len = recvfrom(events[i].data.fd, buffer, 1500, 0, (struct sockaddr*) &sender, &sender_len)) > 0) {
#if LOG_LEVEL_LIMIT >= LOG_DEBUG
          in_addr_storage in_addr;
          char ipv6_sender[INET6_ADDRSTRLEN];
          in_addr = get_in_addr((struct sockaddr*)&sender);
          DEBUG("Receive message from %s\n", inet_ntop(AF_INET6, &in_addr, ipv6_sender, INET6_ADDRSTRLEN));
#endif
          statistics_record((&config), STAT_DIRECT_RECV_BYTE, (long int)len);
          statistics_record((&config), STAT_DIRECT_RECV_PKG, 1);
          ddhcp_dhcp_process(buffer, len, sender, &config);
        }
      } else if (config.mcast_socket == events[i].data.fd) {
        // DDHCP Block Handling
        ssize_t len;

        while ((len = recvfrom(events[i].data.fd, buffer, 1500, 0, (struct sockaddr*) &sender, &sender_len)) > 0) {
#if LOG_LEVEL_LIMIT >= LOG_DEBUG
          in_addr_storage in_addr;
          char ipv6_sender[INET6_ADDRSTRLEN];
          in_addr = get_in_addr((struct sockaddr*)&sender);
          DEBUG("Receive message from %s\n", inet_ntop(AF_INET6, &in_addr, ipv6_sender, INET6_ADDRSTRLEN));
#endif
          statistics_record((&config), STAT_MCAST_RECV_BYTE, (long int)len);
          statistics_record((&config), STAT_MCAST_RECV_PKG, 1);
          ddhcp_block_process(buffer, len, sender, &config);
        }

        house_keeping(&config);
        need_house_keeping = 0;
      } else if (config.client_socket == events[i].data.fd) {
        // DHCP
        ssize_t len;

        while ((len = read(config.client_socket, buffer, 1500)) > 0) {
          statistics_record((&config), STAT_DHCP_RECV_BYTE, (long int)len);
          statistics_record((&config), STAT_DHCP_RECV_PKG, 1);
          need_house_keeping |= dhcp_process(buffer, len, &config);
        }
      } else if (config.control_socket == events[i].data.fd) {
        // Handle new control socket connections
        struct sockaddr_un client_fd;
        unsigned int len = sizeof(client_fd);
        config.client_control_socket = accept(config.control_socket, (struct sockaddr*) &client_fd, &len);
        //set_nonblocking(config.client_control_socket);
        add_fd(efd, config.client_control_socket, EPOLLIN | EPOLLET);
        DEBUG("ControlSocket: new connections\n");
      } else if (events[i].events & EPOLLIN) {
        // Handle commands comming over a control_socket
        bytes = read(events[i].data.fd, buffer, 1500);

        if (handle_command(events[i].data.fd, buffer, bytes, &config) < 0) {
          ERROR("Malformed command on control socket.\n");
        }

        del_fd(efd, events[i].data.fd, 0);
        close(events[i].data.fd);
      } else if (events[i].events & EPOLLHUP) {
        del_fd(efd, events[i].data.fd, EPOLLIN);
        close(events[i].data.fd);
      }
    }

    if (need_house_keeping) {
      house_keeping(&config);
    }
  } while (daemon_running);

  // TODO free dhcp_leases
  free(events);
  free(buffer);

  ddhcp_block_free(&config);

  free_option_store(&config.options);
  dhcp_packet_list_free(&config.dhcp_packet_cache);

  close(config.mcast_socket);
  close(config.client_socket);
  close(config.control_socket);

  remove(config.control_path);

  return 0;
}
