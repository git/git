#include "builtin.h"
#include "config.h"
#include "parse-options.h"
#include "fsmonitor.h"
#include "fsmonitor-ipc.h"
#include "simple-ipc.h"
#include "khash.h"

static const char * const builtin_fsmonitor__daemon_usage[] = {
	N_("git fsmonitor--daemon stop"),
	N_("git fsmonitor--daemon status"),
	NULL
};

#ifdef HAVE_FSMONITOR_DAEMON_BACKEND
/*
 * Acting as a CLIENT.
 *
 * Send a "quit" command to the `git-fsmonitor--daemon` (if running)
 * and wait for it to shutdown.
 */
static int do_as_client__send_stop(void)
{
	struct strbuf answer = STRBUF_INIT;
	int ret;

	ret = fsmonitor_ipc__send_command("quit", &answer);

	/* The quit command does not return any response data. */
	strbuf_release(&answer);

	if (ret)
		return ret;

	trace2_region_enter("fsm_client", "polling-for-daemon-exit", NULL);
	while (fsmonitor_ipc__get_state() == IPC_STATE__LISTENING)
		sleep_millisec(50);
	trace2_region_leave("fsm_client", "polling-for-daemon-exit", NULL);

	return 0;
}

static int do_as_client__status(void)
{
	enum ipc_active_state state = fsmonitor_ipc__get_state();

	switch (state) {
	case IPC_STATE__LISTENING:
		printf(_("fsmonitor-daemon is watching '%s'\n"),
		       the_repository->worktree);
		return 0;

	default:
		printf(_("fsmonitor-daemon is not watching '%s'\n"),
		       the_repository->worktree);
		return 1;
	}
}

int cmd_fsmonitor__daemon(int argc, const char **argv, const char *prefix)
{
	const char *subcmd;

	struct option options[] = {
		OPT_END()
	};

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, options,
			     builtin_fsmonitor__daemon_usage, 0);
	if (argc != 1)
		usage_with_options(builtin_fsmonitor__daemon_usage, options);
	subcmd = argv[0];

	if (!strcmp(subcmd, "stop"))
		return !!do_as_client__send_stop();

	if (!strcmp(subcmd, "status"))
		return !!do_as_client__status();

	die(_("Unhandled subcommand '%s'"), subcmd);
}

#else
int cmd_fsmonitor__daemon(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_fsmonitor__daemon_usage, options);

	die(_("fsmonitor--daemon not supported on this platform"));
}
#endif
