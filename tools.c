#include "tools.h"

#include <string.h>

void addr_add(struct in_addr* subnet, struct in_addr* result, int add) {
  uint32_t addr;
  memcpy(&addr, subnet, sizeof(struct in_addr));
  addr = ntohl(addr);
  addr += add;
  addr = htonl(addr);
  memcpy(result, &addr, sizeof(struct in_addr));
}
