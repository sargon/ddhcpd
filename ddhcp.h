#ifndef _DDHCP_H
#define _DDHCP_H

#include "types.h"
#include "list.h"
#include "block.h"

int ddhcp_block_init(ddhcp_config* config);
void ddhcp_block_free(ddhcp_config* config);

void ddhcp_block_process(uint8_t* buffer, int len, struct sockaddr_in6 sender, ddhcp_config* config);

void ddhcp_block_process_claims(struct ddhcp_mcast_packet* packet, ddhcp_config* config);
void ddhcp_block_process_inquire(struct ddhcp_mcast_packet* packet, ddhcp_config* config);

void ddhcp_dhcp_process(uint8_t* buffer, int len, struct sockaddr_in6 sender, ddhcp_config* config);
void ddhcp_dhcp_renewlease(struct ddhcp_mcast_packet* packet, ddhcp_config* config);
void ddhcp_dhcp_leaseack(struct ddhcp_mcast_packet* packet, ddhcp_config* config);
void ddhcp_dhcp_leasenak(struct ddhcp_mcast_packet* packet, ddhcp_config* config);
void ddhcp_dhcp_release(struct ddhcp_mcast_packet* packet, ddhcp_config* config);

ddhcp_block* block_find_lease(ddhcp_config* config);

void house_keeping(ddhcp_config* config);

#endif
