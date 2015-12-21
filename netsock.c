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

#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/select.h>
#include "netsock.h"
#include "packet.h"

const struct in6_addr in6addr_localmcast = {{{ 0xff, 0x02, 0x00, 0x00,
					       0x00, 0x00, 0x00, 0x00,
					       0x00, 0x00, 0x00, 0x00,
					       0x00, 0x00, 0x12, 0x34 } } };

int mac_to_ipv6(const struct ether_addr *mac, struct in6_addr *addr)               
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
               

int netsock_open(char* interface,int* interface_mcast)
{
	int sock;
	int sock_mc;
	struct sockaddr_in6 sin6, sin6_mc;
	struct ipv6_mreq mreq;
	struct ifreq ifr;
	int ret;

	sock = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (sock  < 0) {
		perror("can't open socket");
		return -1;
	}

	sock_mc = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (sock_mc  < 0) {
		close(sock);
		perror("can't open socket");
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, interface, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';
	if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
		perror("can't get interface");
		goto err;
	}

	uint32_t scope_id = ifr.ifr_ifindex;

	if (ioctl(sock, SIOCGIFHWADDR, &ifr) == -1) {
		perror("can't get MAC address");
		goto err;
	}

  struct ether_addr hwaddr;
  struct in6_addr address;

	memcpy(&hwaddr, &ifr.ifr_hwaddr.sa_data, 6);
	mac_to_ipv6(&hwaddr, &address);

	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_port = htons(DDHCP_MULTICAST_PORT);
	sin6.sin6_family = AF_INET6;
	memcpy(&sin6.sin6_addr, &address, sizeof(sin6.sin6_addr));
	sin6.sin6_scope_id = scope_id;

	memset(&sin6_mc, 0, sizeof(sin6_mc));
	sin6_mc.sin6_port = htons(DDHCP_MULTICAST_PORT);
	sin6_mc.sin6_family = AF_INET6;
	memcpy(&sin6_mc.sin6_addr, &in6addr_localmcast,
	       sizeof(sin6_mc.sin6_addr));
	sin6_mc.sin6_scope_id = scope_id;

	if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, interface,
		       strlen(interface) + 1)) {
		perror("can't bind to device");
		goto err;
	}

	if (setsockopt(sock_mc, SOL_SOCKET, SO_BINDTODEVICE,
		       interface,
		       strlen(interface) + 1)) {
		perror("can't bind to device");
		goto err;
	}

	if (bind(sock, (struct sockaddr *)&sin6, sizeof(sin6)) < 0) {
		perror("can't bind");
		goto err;
	}

	if (bind(sock_mc, (struct sockaddr *)&sin6_mc, sizeof(sin6_mc)) < 0) {
		perror("can't bind");
		goto err;
	}

	memcpy(&mreq.ipv6mr_multiaddr, &in6addr_localmcast,
	       sizeof(mreq.ipv6mr_multiaddr));
	mreq.ipv6mr_interface = scope_id;

	if (setsockopt(sock_mc, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
		       &mreq, sizeof(mreq))) {
		perror("can't add multicast membership");
		goto err;
	}

	ret = fcntl(sock, F_GETFL, 0);
	if (ret < 0) {
		perror("failed to get file status flags");
		goto err;
	}

	ret = fcntl(sock, F_SETFL, ret | O_NONBLOCK);
	if (ret < 0) {
		perror("failed to set file status flags");
		goto err;
	}

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

  //interface->netsock = sock;
  interface_mcast = sock_mc;

	return 0;
err:
	close(sock);
	close(sock_mc);
	return -1;
}
