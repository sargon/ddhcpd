#include "hook.h"
#include "logger.h"
#include "tools.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

void hook(uint8_t type, struct in_addr* address, uint8_t* chaddr, ddhcp_config* config) {
  DEBUG("hook(%i,%s,%s,config)\n",type, inet_ntoa(*address), hwaddr2c(chaddr));

  if (!config->hook_command) {
    DEBUG("hook( ... ): No hook command set");
    return;
  }

  int err = 0;
  int pid;

// TODO: should we synchronize the hook runs?
  switch (type) {
  case HOOK_LEASE:
    pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", config->hook_command, "lease", inet_ntoa(*address), hwaddr2c(chaddr), (char*) 0);
        err = -1;
  }
    break;

  case HOOK_RELEASE:
    pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", config->hook_command, "release", inet_ntoa(*address), hwaddr2c(chaddr), (char*) 0);
        err = -1;
    }
    break;
  }

  if (err < 0) {
    FATAL("hook( ... ): Command can not be executed.\n");
  }
}

void cleanup_process_table(int signum)
{
    UNUSED(signum);
    DEBUG("signal: %i\n", signum);
    wait(NULL);
}

void hook_init() {
    signal(SIGCHLD, cleanup_process_table);
}
