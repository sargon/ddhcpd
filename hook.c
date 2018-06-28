#include "hook.h"
#include "logger.h"
#include "tools.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

void hook(uint8_t type, struct in_addr* address, uint8_t* chaddr, ddhcp_config* config) {
  DEBUG("hook(%i,%s,%s,config)\n", type, inet_ntoa(*address), hwaddr2c(chaddr));

  if (!config->hook_command) {
    DEBUG("hook( ... ): No hook command set");
    return;
  }

  int pid;

  char* action = NULL;

  switch (type) {
  case HOOK_LEASE:
    action = "lease";
    break;

  case HOOK_RELEASE:
    action = "release";
    break;

  default:
    break;
  }

  if (!action) {
    DEBUG("hook: unknown hook type: %i\n", type);
    return;
  }

  pid = fork();

  if (pid < 0) {
    // TODO: Include errno from fork
    FATAL("hook( ... ): Failed to fork() for hook command execution (errno: %i).\n", pid);
    return;
  }

  if (pid != 0) {
    //Nothing to do as the parent
    return;
  }

  int err = execl(
    // Binary to execute
    "/bin/sh",

    // Arguments to pass
    "/bin/sh", //Be pedantic about executing /bin/sh
    "-e", // Terminate on error return
    "--", // Terminate argument parsing
    config->hook_command, // Our actual command to run
    action, // The action we notify about
    inet_ntoa(*address), // The affected IP address
    hwaddr2c(chaddr), // The affected MAC address
    (char*) NULL // End of command line
  );

  if (err < 0) {
    // TODO: Logging from the child should be synchronized
    FATAL("hook( ... ): Command could not be executed (errno: %i).\n", err);
  }
  exit(1);
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
