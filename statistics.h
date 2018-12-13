#ifndef _STATISTICS_H
#define _STATISTICS_H

#include "types.h"

#ifdef DDHCPD_STATISTICS
#define statistics_record(config,type,count) do{config->statistics[type]+=count;}while(0)
void statistics_show(int socket, ddhcp_config* config);
#else
#define statistics_record(...)
#define statistics_show(...)
#endif

#endif
