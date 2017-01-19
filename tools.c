#include "tools.h"

#include <string.h>

void addr_add(struct in_addr* subnet, struct in_addr* result, int add) {
  struct in_addr addr;
  memcpy(&addr, subnet, sizeof(struct in_addr));
  addr.s_addr = ntohl(addr.s_addr);
  addr.s_addr += add;
  addr.s_addr = htonl(addr.s_addr);
  memcpy(result, &addr, sizeof(struct in_addr));
}
