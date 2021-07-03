/* SPDX-License-Identifier: GPL-3.0-only */
/*
 *  DDHCP - Event hooks
 *
 *  See AUTHORS file for copyright holders
 */

#include "hook.h"
#include "logger.h"
#include "util.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

ATTR_NONNULL_ALL void hook_address(uint8_t type, struct in_addr *address,
				   uint8_t *chaddr, ddhcp_config_t *config)
{
#if LOG_LEVEL_LIMIT >= LOG_DEBUG
	char *hwaddr = hwaddr2c(chaddr);
	DEBUG("hook_address(type:%i,addr:%s,chaddr:%s,config)\n", type,
	      inet_ntoa(*address), hwaddr);
	free(hwaddr);
#endif

	if (!config->hook_command) {
		DEBUG("hook_address(...): No hook command set\n");
		return;
	}

	int pid;

	char *action = NULL;

	switch (type) {
	case HOOK_LEASE:
		action = (char *)"lease";
		break;
	case HOOK_RELEASE:
		action = (char *)"release";
		break;
	default:
		break;
	}

	if (!action) {
		DEBUG("hook_address(...): unknown hook type: %i\n", type);
		return;
	}

	pid = fork();

	if (pid < 0) {
		/* TODO: Include errno from fork */
		FATAL("hook_address(...): Failed to fork() for hook command execution (errno: %i).\n",
		      pid);
		return;
	}

	if (pid) /* Nothing to do as the parent */
		return;

	int err = execl(
		/* Binary to execute */
		"/bin/sh",

		/* Arguments to pass */
		"/bin/sh", /* Be pedantic about executing /bin/sh */
		"-e", /* Terminate on error return */
		"--", /* Terminate argument parsing */
		config->hook_command, /* Our actual command to run */
		action, /* The action we notify about */
		inet_ntoa(*address), /* The affected IP address */
		hwaddr2c(chaddr), /* The affected MAC address */
		(char *)NULL /* End of command line */
	);

	if (err < 0) {
		/* TODO: Logging from the child should be synchronized */
		FATAL("hook_address(...): Command could not be executed (errno: %i).\n",
		      err);
	}

	exit(1);
}

ATTR_NONNULL_ALL void hook(uint8_t type, ddhcp_config_t *config)
{
	char *action = NULL;
	int pid;

	DEBUG("hook(type:%i,addr:%s,config)\n", type);

	if (!config->hook_command) {
		DEBUG("hook_address(...): No hook command set\n");
		return;
	}

	switch (type) {
	case HOOK_LEARNING_PHASE_END:
		action = (char *)"endlearning";
		break;
	default:
		break;
	}

	if (!action) {
		DEBUG("hook(...): unknown hook type: %i\n", type);
		return;
	}

	pid = fork();

	if (pid < 0) {
		/* TODO: Include errno from fork */
		FATAL("hook(...): Failed to fork() for hook command execution (errno: %i).\n",
		      pid);
		return;
	}

	if (pid != 0) /* Nothing to do as the parent */
		return;

	int err = execl(
		/* Binary to execute */
		"/bin/sh",

		/* Arguments to pass */
		"/bin/sh", /* Be pedantic about executing /bin/sh */
		"-e", /* Terminate on error return */
		"--", /* Terminate argument parsing */
		config->hook_command, /* Our actual command to run */
		action, /* The action we notify about */
		(char *)NULL /* End of command line */
	);

	if (err < 0) {
		/* TODO: Logging from the child should be synchronized */
		FATAL("hook(...): Command could not be executed (errno: %i).\n",
		      err);
	}

	exit(1);
}

void cleanup_process_table(int signum)
{
	UNUSED(signum);
	DEBUG("cleanup_process_table(...): Got signal %i\n", signum);
	wait(NULL);
}

void hook_init()
{
	signal(SIGCHLD, cleanup_process_table);
}
