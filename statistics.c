#include "types.h"
#include <stdio.h>

#ifdef DDHCPD_STATISTICS
void statistics_show(int fd, ddhcp_config* config) {
  dprintf(fd, "mcast.recv_pkg %li\n", config->statistics[STAT_MCAST_RECV_PKG]);
  dprintf(fd, "mcast.send_pkg %li\n", config->statistics[STAT_MCAST_SEND_PKG]);
  dprintf(fd, "mcast.recv_byte %li\n", config->statistics[STAT_MCAST_RECV_BYTE]);
  dprintf(fd, "mcast.send_byte %li\n", config->statistics[STAT_MCAST_SEND_BYTE]);
  dprintf(fd, "mcast.recv_updateclaim %li\n", config->statistics[STAT_MCAST_RECV_UPDATECLAIM]);
  dprintf(fd, "mcast.recv_inquire %li\n", config->statistics[STAT_MCAST_RECV_INQUIRE]);
  dprintf(fd, "direct.recv_pkg %li\n", config->statistics[STAT_DIRECT_RECV_PKG]);
  dprintf(fd, "direct.send_pkg %li\n", config->statistics[STAT_DIRECT_SEND_PKG]);
  dprintf(fd, "direct.recv_byte %li\n", config->statistics[STAT_DIRECT_RECV_BYTE]);
  dprintf(fd, "direct.send_byte %li\n", config->statistics[STAT_DIRECT_SEND_BYTE]);
  dprintf(fd, "direct.recv_renewlease %li\n", config->statistics[STAT_DIRECT_RECV_RENEWLEASE]);
  dprintf(fd, "direct.send_renewlease %li\n", config->statistics[STAT_DIRECT_SEND_RENEWLEASE]);
  dprintf(fd, "direct.recv_leaseack %li\n", config->statistics[STAT_DIRECT_RECV_LEASEACK]);
  dprintf(fd, "direct.send_leaseack %li\n", config->statistics[STAT_DIRECT_SEND_LEASEACK]);
  dprintf(fd, "direct.recv_leasenak %li\n", config->statistics[STAT_DIRECT_RECV_LEASENAK]);
  dprintf(fd, "direct.send_leasenak %li\n", config->statistics[STAT_DIRECT_SEND_LEASENAK]);
  dprintf(fd, "direct.recv_release %li\n", config->statistics[STAT_DIRECT_RECV_RELEASE]);
  dprintf(fd, "direct.send_release %li\n", config->statistics[STAT_DIRECT_SEND_RELEASE]);
  dprintf(fd, "dhcp.recv_pkg %li\n", config->statistics[STAT_DHCP_RECV_PKG]);
  dprintf(fd, "dhcp.send_pkg %li\n", config->statistics[STAT_DHCP_SEND_PKG]);
  dprintf(fd, "dhcp.recv_byte %li\n", config->statistics[STAT_DHCP_RECV_BYTE]);
  dprintf(fd, "dhcp.send_byte %li\n", config->statistics[STAT_DHCP_SEND_BYTE]);
  dprintf(fd, "dhcp.recv_discover %li\n", config->statistics[STAT_DHCP_RECV_DISCOVER]);
  dprintf(fd, "dhcp.send_offer %li\n", config->statistics[STAT_DHCP_SEND_OFFER]);
  dprintf(fd, "dhcp.recv_request %li\n", config->statistics[STAT_DHCP_RECV_REQUEST]);
  dprintf(fd, "dhcp.send_ack %li\n", config->statistics[STAT_DHCP_SEND_ACK]);
  dprintf(fd, "dhcp.send_nak %li\n", config->statistics[STAT_DHCP_SEND_NAK]);
  dprintf(fd, "dhcp.recv_release %li\n", config->statistics[STAT_DHCP_RECV_RELEASE]);
}
#endif
