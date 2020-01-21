#include <arpa/inet.h>
#include <assert.h>
#include <getopt.h>
#include <math.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <limits.h>

#include "block.h"
#include "control.h"
#include "ddhcp.h"
#include "dhcp.h"
#include "dhcp_options.h"
#include "dhcp_packet.h"
#include "epoll.h"
#include "hook.h"
#include "logger.h"
#include "netsock.h"
#include "packet.h"
#include "statistics.h"
#include "tools.h"
#include "version.h"

volatile int daemon_running = 0;

extern int log_level;

const int NET = 0;
const int NET_LEN = 10;

uint8_t* buffer = NULL;

ATTR_NONNULL_ALL in_addr_storage get_in_addr(struct sockaddr* sa)
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
ATTR_NONNULL_ALL void house_keeping(ddhcp_config* config) {
  DEBUG("house_keeping(blocks,config)\n");
  block_check_timeouts(config);

  uint32_t spare_leases = block_num_free_leases(config);
  int32_t leases_needed = (int32_t)config->spare_leases_needed - (int32_t)spare_leases;
  int32_t blocks_needed = leases_needed / config->block_size;

  if (leases_needed % config->block_size > 0) {
    blocks_needed++;
  }

  if (blocks_needed < 0 ) {
    block_drop_unused(config);
  } else {
    if (config->needless_marks != 0) {
      block_unmark_needless(config);
    }
    if (blocks_needed > 0) {
      block_claim(blocks_needed, config);
    }
  }

  block_update_claims(config);

  dhcp_packet_list_timeout(&config->dhcp_packet_cache);
  DEBUG("house_keeping(...) finish\n\n");
}


ATTR_NONNULL_ALL uint32_t get_loop_timeout(ddhcp_config* config) {
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

ATTR_NONNULL_ALL int hdl_ddhcp_dhcp(int fd, ddhcp_config* config) {
  ssize_t len;
  struct sockaddr_in6 sender;
  socklen_t sender_len = sizeof sender;

  while ((len = recvfrom(fd, buffer, 1500, 0, (struct sockaddr*) &sender, &sender_len)) > 0) {
#if LOG_LEVEL_LIMIT >= LOG_DEBUG
    in_addr_storage in_addr;
    char ipv6_sender[INET6_ADDRSTRLEN];
    in_addr = get_in_addr((struct sockaddr*)&sender);
    DEBUG("Receive message from %s\n", inet_ntop(AF_INET6, &in_addr, ipv6_sender, INET6_ADDRSTRLEN));
#endif
    statistics_record(config, STAT_DIRECT_RECV_BYTE, (long int)len);
    statistics_record(config, STAT_DIRECT_RECV_PKG, 1);
    ddhcp_dhcp_process(buffer, len, sender, config);
  }
  return 0;
}

ATTR_NONNULL_ALL int hdl_ddhcp_block(int fd, ddhcp_config* config) {
  ssize_t len;
  struct sockaddr_in6 sender;
  socklen_t sender_len = sizeof sender;

  while ((len = recvfrom(fd, buffer, 1500, 0, (struct sockaddr*) &sender, &sender_len)) > 0) {
#if LOG_LEVEL_LIMIT >= LOG_DEBUG
    in_addr_storage in_addr;
    char ipv6_sender[INET6_ADDRSTRLEN];
    in_addr = get_in_addr((struct sockaddr*)&sender);
    DEBUG("Receive message from %s\n", inet_ntop(AF_INET6, &in_addr, ipv6_sender, INET6_ADDRSTRLEN));
#endif
    statistics_record(config, STAT_MCAST_RECV_BYTE, (long int)len);
    statistics_record(config, STAT_MCAST_RECV_PKG, 1);
    ddhcp_block_process(buffer, len, sender, config);
  }
  return 1;
}

ATTR_NONNULL_ALL int hdl_dhcp(int fd, ddhcp_config* config) {
  ssize_t len;
  int need_house_keeping = 0;

  while ((len = read(fd, buffer, 1500)) > 0) {
    statistics_record(config, STAT_DHCP_RECV_BYTE, (long int)len);
    statistics_record(config, STAT_DHCP_RECV_PKG, 1);
    need_house_keeping |= dhcp_process(buffer, len, config);
  }
  return need_house_keeping;
}

ATTR_NONNULL_ALL int hdl_ctrl_new(int fd, ddhcp_config* config) {
  UNUSED(config);
  // Handle new control socket connections
  struct sockaddr_un client_fd;
  unsigned int len = sizeof(client_fd);
  config->client_control_socket = accept(fd, (struct sockaddr*) &client_fd, &len);
  //set_nonblocking(config.client_control_socket);
  add_fd(config->epoll_fd, config->client_control_socket, EPOLLIN | EPOLLET, NULL);
  DEBUG("ControlSocket: new connections\n");
  return 0;
}

ATTR_NONNULL_ALL int hdl_ctrl_cmd(int fd, ddhcp_config* config) {
  ssize_t len;
  // Handle commands comming over a control_socket
  len = read(fd, buffer, 1500);

  if (handle_command(fd, buffer, len, config) < 0) {
    ERROR("Malformed command on control socket.\n");
  }

  del_fd(config->epoll_fd, fd);
  close(fd);
  return 0;
}

int main(int argc, char** argv) {

  srand((unsigned int)time(NULL));

  ddhcp_config config;
  config.block_size = 32;
  config.claiming_blocks_amount = 0;

  inet_aton("10.0.0.0", &config.prefix);
  config.prefix_len = 24;
  config.spare_leases_needed = 2;
  config.block_timeout = 60;
  config.block_refresh_factor = 4;
  config.block_needless_timeout = 300;
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
  int learning_phase = 1;

  while ((c = getopt(argc, argv, "C:c:i:St:dvVDhLb:B:N:o:s:H:n:")) != -1) {
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
      if (config.tentative_timeout < 2) {
        ERROR("Tentative timeout must at least two\n");
        exit(1);
      }
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
      learning_phase = 0;
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
      config.spare_leases_needed = (uint8_t)atoi(optarg);
#if LOG_LEVEL_LIMIT >= LOG_WARNING

      if (config.spare_leases_needed == 0) {
        WARNING("This deamon will only serve roamed clients with a spare limit of zero.");
      }

#endif
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

    case 'n':
      config.block_needless_timeout = (uint16_t)(atoi(optarg));
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
    printf("-B TIMEOUT             Block claim timeout\n");
    printf("-n NEEDLESS_TIMEOUT    Time until we release needless blocks\n");
    printf("-s SPARELEASES         Amount of spare leases (max: 256)\n");
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
    config.spare_leases_needed = 0;
  }

  INFO("CONFIG: network=%s/%i\n", inet_ntoa(config.prefix), config.prefix_len);
  INFO("CONFIG: block_size=%i\n", config.block_size);
  INFO("CONFIG: #blocks=%i\n", config.number_of_blocks);
  INFO("CONFIG: #spare_leases=%i\n", config.spare_leases_needed);
  INFO("CONFIG: timeout=%i\n", config.block_timeout);
  INFO("CONFIG: refresh_factor=%i\n", config.block_refresh_factor);
  INFO("CONFIG: tentative_timeout=%i\n", config.tentative_timeout);
  INFO("CONFIG: client_interface=%s\n", interface_client);
  INFO("CONFIG: group_interface=%s\n", interface);

  //Register signal handlers
  handle_signal(SIGHUP, SIG_IGN);
  handle_signal(SIGPIPE, SIG_IGN);
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
  if (netsock_init(interface, interface_client, &config) == -1) {
    return 1;
  }

  if (control_open(&config) == -1) {
    return 1;
  }

  buffer = (uint8_t*) malloc(sizeof(uint8_t) * 1500);

  if (!buffer) {
    FATAL("Failed to allocate network buffer\n");
    abort();
  }

  size_t maxevents = 64;
  struct epoll_event* events;

  epoll_init(&config);

  add_fd(config.epoll_fd, config.mcast_socket, EPOLLIN | EPOLLET, NULL);
  add_fd(config.epoll_fd, config.server_socket, EPOLLIN | EPOLLET, NULL);
  add_fd(config.epoll_fd, config.control_socket, EPOLLIN | EPOLLET, NULL);

  if (config.disable_dhcp == 0) {
    add_fd(config.epoll_fd, config.client_socket, EPOLLIN | EPOLLET, NULL);
  }

  /* Buffer where events are returned */
  events = calloc(maxevents, sizeof(struct epoll_event));

  if (!events) {
    FATAL("Failed to allocate event buffer\n");
    abort();
  }

  int need_house_keeping = 0;
  uint32_t loop_timeout = config.loop_timeout = get_loop_timeout(&config);
  time_t now = time(NULL);
  // The first time we want to make housekeeping is after the learning phase, 
  // which is block_timeout long. 
  time_t timeout_time = now + config.block_timeout;

  if (!learning_phase) {
    // We want no learning phase, so reset all the timers
    loop_timeout = 0;
    timeout_time = now;
    hook(HOOK_LEARNING_PHASE_END,&config);
  }


  INFO("loop timeout: %i msecs\n", get_loop_timeout(&config));

  do {
    int n = 0;
    do {
      n = epoll_wait(config.epoll_fd, events, (int)maxevents, (int)loop_timeout);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
      ERROR("epoll error (%i) %s",errno,strerror(errno));
    }

#if LOG_LEVEL_LIMIT >= LOG_DEBUG
    if (loop_timeout != config.loop_timeout) {
      DEBUG("Increase loop timeout from %i to %i\n", loop_timeout, config.loop_timeout);
    }
#endif

    loop_timeout = config.loop_timeout;

    now = time(NULL);
    if (timeout_time <= now) {
      // Our time for house keeping has come
      need_house_keeping = 1;
      // The next time for house keeping is in half the tentative timeout.
      timeout_time = now + (config.tentative_timeout >> 1);
      if (learning_phase) {
        learning_phase = 0;
        hook(HOOK_LEARNING_PHASE_END,&config);
      }
    } else { 
      need_house_keeping = 0;
    }

    for (int i = 0; i < n; i++) {
      if ((events[i].events & EPOLLERR)) {
        ERROR("Error in epoll: %i \n", errno);
        exit(1);
      } else if (config.server_socket == events[i].data.fd) {
        hdl_ddhcp_dhcp(events[i].data.fd, &config);
      } else if (config.mcast_socket == events[i].data.fd) {
        need_house_keeping |= hdl_ddhcp_block(events[i].data.fd, &config);
      } else if (config.client_socket == events[i].data.fd) {
        need_house_keeping |= hdl_dhcp(events[i].data.fd, &config);
      } else if (config.control_socket == events[i].data.fd) {
        hdl_ctrl_new(events[i].data.fd, &config);
      } else if (events[i].events & EPOLLIN) {
        hdl_ctrl_cmd(events[i].data.fd, &config);
      } else if (events[i].events & EPOLLHUP) {
        DEBUG("Removing epoll fd %i\n",events[i].data.fd);
        del_fd(config.epoll_fd, events[i].data.fd);
        close(events[i].data.fd);
      } 
    }

    if (need_house_keeping) {
      if (!learning_phase) {
        house_keeping(&config);
      }
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
