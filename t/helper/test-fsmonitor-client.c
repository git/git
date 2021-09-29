/*
 * test-fsmonitor-client.c: client code to send commands/requests to
 * a `git fsmonitor--daemon` daemon.
 */

#include "test-tool.h"
#include "cache.h"
#include "parse-options.h"
#include "fsmonitor-ipc.h"

#ifndef HAVE_FSMONITOR_DAEMON_BACKEND
int cmd__fsmonitor_client(int argc, const char **argv)
{
	die("fsmonitor--daemon not available on this platform");
}
#else

/*
 * Read the `.git/index` to get the last token written to the
 * FSMonitor Index Extension.
 */
static const char *get_token_from_index(void)
{
	struct index_state *istate = the_repository->index;

	if (do_read_index(istate, the_repository->index_file, 0) < 0)
		die("unable to read index file");
	if (!istate->fsmonitor_last_update)
		die("index file does not have fsmonitor extension");

	return istate->fsmonitor_last_update;
}

/*
 * Send an IPC query to a `git-fsmonitor--daemon` daemon and
 * ask for the changes since the given token or from the last
 * token in the index extension.
 *
 * This will implicitly start a daemon process if necessary.  The
 * daemon process will persist after we exit.
 */
static int do_send_query(const char *token)
{
	struct strbuf answer = STRBUF_INIT;
	int ret;

	if (!token || !*token)
		token = get_token_from_index();

	ret = fsmonitor_ipc__send_query(token, &answer);
	if (ret < 0)
		die(_("could not query fsmonitor--daemon"));

	write_in_full(1, answer.buf, answer.len);
	strbuf_release(&answer);

	return 0;
}

/*
 * Send a "flush" command to the `git-fsmonitor--daemon` (if running)
 * and tell it to flush its cache.
 *
 * This feature is primarily used by the test suite to simulate a loss of
 * sync with the filesystem where we miss kernel events.
 */
static int do_send_flush(void)
{
	struct strbuf answer = STRBUF_INIT;
	int ret;

	ret = fsmonitor_ipc__send_command("flush", &answer);
	if (ret)
		return ret;

	write_in_full(1, answer.buf, answer.len);
	strbuf_release(&answer);

	return 0;
}

int cmd__fsmonitor_client(int argc, const char **argv)
{
	const char *subcmd;
	const char *token = NULL;

	const char * const fsmonitor_client_usage[] = {
		N_("test-helper fsmonitor-client query [<token>]"),
		N_("test-helper fsmonitor-client flush"),
		NULL,
	};

	struct option options[] = {
		OPT_STRING(0, "token", &token, N_("token"),
			   N_("command token to send to the server")),
		OPT_END()
	};

	if (argc < 2)
		usage_with_options(fsmonitor_client_usage, options);

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(fsmonitor_client_usage, options);

	subcmd = argv[1];
	argv--;
	argc++;

	argc = parse_options(argc, argv, NULL, options, fsmonitor_client_usage, 0);

	setup_git_directory();

	if (!strcmp(subcmd, "query"))
		return !!do_send_query(token);

	if (!strcmp(subcmd, "flush"))
		return !!do_send_flush();

	die("Unhandled subcommand: '%s'", subcmd);
}
#endif
