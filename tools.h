#ifndef _TOOLS_H
#define _TOOLS_H

#include <arpa/inet.h>

#define min(a,b) ((a<b)?a:b)
#define max(a,b) ((a>b)?a:b)

void addr_add(struct in_addr *subnet, struct in_addr *result,int add);

#endif
