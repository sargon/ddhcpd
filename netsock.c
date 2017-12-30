/*
 * Copyright (C) 2012-2015  B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "netsock.h"
#include "packet.h"

// ff02::1234.1234
const struct in6_addr in6addr_localmcast =
{
  {
    {
      0xff, 0x02, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x12, 0x34
    }
  }
};

int mac_to_ipv6(const struct ether_addr* mac, struct in6_addr* addr)
{
  memset(addr, 0, sizeof(*addr));
  addr->s6_addr[0] = 0xfe;
  addr->s6_addr[1] = 0x80;

  addr->s6_addr[8] = mac->ether_addr_octet[0] ^ 0x02;
  addr->s6_addr[9] = mac->ether_addr_octet[1];
  addr->s6_addr[10] = mac->ether_addr_octet[2];

  addr->s6_addr[11] = 0xff;
  addr->s6_addr[12] = 0xfe;

  addr->s6_addr[13] = mac->ether_addr_octet[3];
  addr->s6_addr[14] = mac->ether_addr_octet[4];
  addr->s6_addr[15] = mac->ether_addr_octet[5];

  return 0;
}

int control_open(ddhcp_config* state) {
  int ctl_sock;
  struct sockaddr_un s_un;
  int ret;

  ctl_sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

  memset(&s_un, 0, sizeof(s_un));
  s_un.sun_family = AF_UNIX;

  strncpy(s_un.sun_path, state->control_path, sizeof(s_un.sun_path));

  if (bind(ctl_sock, (struct sockaddr*)&s_un, sizeof(s_un)) < 0) {
    perror("can't bind control socket");
    goto err;
  }

  ret = fcntl(ctl_sock, F_GETFL, 0);
  ret = fcntl(ctl_sock, F_SETFL, ret | O_NONBLOCK);

  if (ret < 0) {
    perror("failed to set file status flags");
    goto err;
  }

  if (listen(ctl_sock, 2) < 0) {
    perror("failed ot listen");
    goto err;
  }

  state->control_socket = ctl_sock;

  return 0;
err:
  close(ctl_sock);
  return -1;
}

int netsock_openv4(char* interface_client, ddhcp_config* config);

int netsock_open(char* interface, char* interface_client, ddhcp_config* state)
{
  int sock_mc;
  int sock_srv;
  struct sockaddr_in6 sin6_mc;
  struct sockaddr_in6 sin6_srv;
  struct ipv6_mreq mreq;
  unsigned int mloop = 0;
  struct ifreq ifr;
  int ret;

  if ( state->disable_dhcp == 0 ) {
    if ( netsock_openv4(interface_client, state) < 0 ) {
      return -1;
    }
  }

  sock_mc = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);

  if (sock_mc  < 0) {
    perror("can't open multicast socket");
    return -1;
  }

  sock_srv = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);

  if (sock_srv  < 0) {
    perror("can't open server socket");
    return -1;
  }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, interface, IFNAMSIZ);
  ifr.ifr_name[IFNAMSIZ - 1] = '\0';

  if (ioctl(sock_srv, SIOCGIFINDEX, &ifr) == -1) {
    perror("can't get interface");
    goto err;
  }

  uint32_t scope_id = ifr.ifr_ifindex;
  state->mcast_scope_id = ifr.ifr_ifindex;

  if (ioctl(sock_srv, SIOCGIFHWADDR, &ifr) == -1) {
    perror("can't get MAC address");
    goto err;
  }

  struct ether_addr hwaddr;

  struct in6_addr address;

  memcpy(&hwaddr, &ifr.ifr_hwaddr.sa_data, 6);

  mac_to_ipv6(&hwaddr, &address);

  // DDHCPD Multicast Port

  memset(&sin6_mc, 0, sizeof(sin6_mc));

  sin6_mc.sin6_port = htons(DDHCP_MULTICAST_PORT);

  sin6_mc.sin6_family = AF_INET6;

  memcpy(&sin6_mc.sin6_addr, &in6addr_localmcast,
         sizeof(sin6_mc.sin6_addr));

  sin6_mc.sin6_scope_id = scope_id;

  // DDHCPD Unicast Port

  memset(&sin6_srv, 0, sizeof(sin6_srv));

  sin6_srv.sin6_port = htons(DDHCP_UNICAST_PORT);

  sin6_srv.sin6_family = AF_INET6;

  memcpy(&sin6_srv.sin6_addr, &in6addr_any,
         sizeof(sin6_srv.sin6_addr));

  sin6_srv.sin6_scope_id = scope_id;

  // Socket Options

  if (setsockopt(sock_mc, SOL_SOCKET, SO_BINDTODEVICE,
                 interface,
                 strlen(interface) + 1)) {
    perror("can't bind to multicast device");
    goto err;
  }

  if (setsockopt(sock_srv, SOL_SOCKET, SO_BINDTODEVICE,
                 interface,
                 strlen(interface) + 1)) {
    perror("can't bind to server device");
    goto err;
  }

  // Bind

  if (bind(sock_mc, (struct sockaddr*)&sin6_mc, sizeof(sin6_mc)) < 0) {
    perror("can't bind multicast");
    goto err;
  }

  if (bind(sock_srv, (struct sockaddr*)&sin6_srv, sizeof(sin6_srv)) < 0) {
    perror("can't bind server");
    goto err;
  }

  // Multicast Group Registration
  
  memcpy(&mreq.ipv6mr_multiaddr, &in6addr_localmcast,
         sizeof(mreq.ipv6mr_multiaddr));
  mreq.ipv6mr_interface = scope_id;

  if (setsockopt(sock_mc, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                 &mreq, sizeof(mreq))) {
    perror("can't add multicast membership");
    goto err;
  }

  if (setsockopt(sock_mc, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
                 &mloop, sizeof(mloop))) {
    perror("can't unset multicast loop");
    goto err;
  }

  // Broadcast Options for DHCP 

  ret = fcntl(sock_mc, F_GETFL, 0);

  if (ret < 0) {
    perror("failed to get file status flags");
    goto err;
  }

  ret = fcntl(sock_mc, F_SETFL, ret | O_NONBLOCK);

  if (ret < 0) {
    perror("failed to set file status flags");
    goto err;
  }

  ret = fcntl(sock_srv, F_GETFL, 0);

  if (ret < 0) {
    perror("failed to get file status flags");
    goto err;
  }

  ret = fcntl(sock_srv, F_SETFL, ret | O_NONBLOCK);

  if (ret < 0) {
    perror("failed to set file status flags");
    goto err;
  }

  state->mcast_socket = sock_mc;
  state->server_socket = sock_srv;

  memcpy(&state->node_id,&hwaddr,sizeof(hwaddr));

  return 0;
err:
  close(sock_srv);
  close(sock_mc);
  return -1;
}

int netsock_openv4(char* interface_client, ddhcp_config* config) {
  int sock;
  struct sockaddr_in sin;
  struct in_addr address_client;
  unsigned int broadcast = 1;
  int ret;

  sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (sock  < 0) {
    perror("can't open broadcast socket");
    return -1;
  }

  memset(&sin, 0, sizeof(sin));
  sin.sin_port = htons(config->dhcp_port);
  sin.sin_family = AF_INET;

  inet_aton("0.0.0.0", &address_client);
  memcpy(&sin.sin_addr, &address_client, sizeof(sin.sin_addr));

  if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, interface_client,
                 strlen(interface_client) + 1)) {
    perror("can't bind to boardcast device");
    close(sock);
    return -1;
  }

  if (bind(sock, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
    perror("can't bind broadcast socket");
    close(sock);
    return -1;
  }

  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(unsigned int))) {
    perror("can't set boardcast on client socket");
    close(sock);
    return -1;
  }

  ret = fcntl(sock, F_GETFL, 0);

  if (ret < 0) {
    perror("failed to get file status flags");
    close(sock);
    return -1;
  }

  ret = fcntl(sock, F_SETFL, ret | O_NONBLOCK);

  if (ret < 0) {
    perror("failed to set file status flags");
    close(sock);
    return -1;
  }
  config->client_socket = sock;

  return 0;
}
