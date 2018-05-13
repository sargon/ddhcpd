#include "hook.h"
#include "logger.h"
#include "tools.h"

#include <unistd.h>

void hook(uint8_t type, struct in_addr* address, uint8_t* chaddr, ddhcp_config* config) {
  DEBUG("hook(%i,%s,%s,config)\n",type, inet_ntoa(*address), hwaddr2c(chaddr));

  if (config->hook_command) {
    DEBUG("hook( ... ): No hook command set");
    return;
  }

  int err = 0;

  switch (type) {
  case HOOK_LEASE:
    execl(config->hook_command, "lease", inet_ntoa(*address), hwaddr2c(chaddr), (char*) 0);
    break;

  case HOOK_RELEASE:
    execl(config->hook_command, "release", inet_ntoa(*address), hwaddr2c(chaddr), (char*) 0);
    break;
  }

  if (err < 0) {
    FATAL("hook( ... ): Command can not be executed.\n");
  }
}
