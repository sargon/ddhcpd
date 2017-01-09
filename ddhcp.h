#ifndef _DDHCP_H
#define _DDHCP_H

#include "types.h"
#include "list.h"
#include "block.h"

int ddhcp_block_init(struct ddhcp_block** blocks, ddhcp_config* config);
void ddhcp_block_process_claims(struct ddhcp_block* blocks , struct ddhcp_mcast_packet* packet , ddhcp_config* config) ;
void ddhcp_block_process_inquire(struct ddhcp_block* blocks , struct ddhcp_mcast_packet* packet , ddhcp_config* config);
ddhcp_block* block_find_lease(ddhcp_block* blocks , ddhcp_config* config);
void house_keeping(ddhcp_block* blocks, ddhcp_config* config);

#endif
