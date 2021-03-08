#include "cache.h"
#include "fsmonitor.h"
#include "fsmonitor-ipc.h"
#include "run-command.h"
#include "strbuf.h"
#include "trace2.h"

#ifdef HAVE_FSMONITOR_DAEMON_BACKEND
#define FSMONITOR_DAEMON_IS_SUPPORTED 1
#else
#define FSMONITOR_DAEMON_IS_SUPPORTED 0
#endif

/*
 * A trivial function so that this source file always defines at least
 * one symbol even when the feature is not supported.  This quiets an
 * annoying compiler error.
 */
int fsmonitor_ipc__is_supported(void)
{
	return FSMONITOR_DAEMON_IS_SUPPORTED;
}

#ifdef HAVE_FSMONITOR_DAEMON_BACKEND

GIT_PATH_FUNC(fsmonitor_ipc__get_path, "fsmonitor")

enum ipc_active_state fsmonitor_ipc__get_state(void)
{
	return ipc_get_active_state(fsmonitor_ipc__get_path());
}

static int spawn_daemon(void)
{
	const char *args[] = { "fsmonitor--daemon", "--start", NULL };

	return run_command_v_opt_tr2(args, RUN_COMMAND_NO_STDIN | RUN_GIT_CMD,
				    "fsmonitor");
}

int fsmonitor_ipc__send_query(const char *since_token,
			      struct strbuf *answer)
{
	int ret = -1;
	int tried_to_spawn = 0;
	enum ipc_active_state state = IPC_STATE__OTHER_ERROR;
	struct ipc_client_connection *connection = NULL;
	struct ipc_client_connect_options options
		= IPC_CLIENT_CONNECT_OPTIONS_INIT;

	options.wait_if_busy = 1;
	options.wait_if_not_found = 0;

	trace2_region_enter("fsm_client", "query", NULL);

	trace2_data_string("fsm_client", NULL, "query/command",
			   since_token);

try_again:
	state = ipc_client_try_connect(fsmonitor_ipc__get_path(), &options,
				       &connection);

	switch (state) {
	case IPC_STATE__LISTENING:
		ret = ipc_client_send_command_to_connection(
			connection, since_token, answer);
		ipc_client_close_connection(connection);

		trace2_data_intmax("fsm_client", NULL,
				   "query/response-length", answer->len);

		if (fsmonitor_is_trivial_response(answer))
			trace2_data_intmax("fsm_client", NULL,
					   "query/trivial-response", 1);

		goto done;

	case IPC_STATE__NOT_LISTENING:
		ret = error(_("fsmonitor_ipc__send_query: daemon not available"));
		goto done;

	case IPC_STATE__PATH_NOT_FOUND:
		if (tried_to_spawn)
			goto done;

		tried_to_spawn++;
		if (spawn_daemon())
			goto done;

		/*
		 * Try again, but this time give the daemon a chance to
		 * actually create the pipe/socket.
		 *
		 * Granted, the daemon just started so it can't possibly have
		 * any FS cached yet, so we'll always get a trivial answer.
		 * BUT the answer should include a new token that can serve
		 * as the basis for subsequent requests.
		 */
		options.wait_if_not_found = 1;
		goto try_again;

	case IPC_STATE__INVALID_PATH:
		ret = error(_("fsmonitor_ipc__send_query: invalid path '%s'"),
			    fsmonitor_ipc__get_path());
		goto done;

	case IPC_STATE__OTHER_ERROR:
	default:
		ret = error(_("fsmonitor_ipc__send_query: unspecified error on '%s'"),
			    fsmonitor_ipc__get_path());
		goto done;
	}

done:
	trace2_region_leave("fsm_client", "query", NULL);

	return ret;
}

int fsmonitor_ipc__send_command(const char *command,
				struct strbuf *answer)
{
	struct ipc_client_connection *connection = NULL;
	struct ipc_client_connect_options options
		= IPC_CLIENT_CONNECT_OPTIONS_INIT;
	int ret;
	enum ipc_active_state state;

	strbuf_reset(answer);

	options.wait_if_busy = 1;
	options.wait_if_not_found = 0;

	state = ipc_client_try_connect(fsmonitor_ipc__get_path(), &options,
				       &connection);
	if (state != IPC_STATE__LISTENING) {
		die("fsmonitor--daemon is not running");
		return -1;
	}

	ret = ipc_client_send_command_to_connection(connection, command, answer);
	ipc_client_close_connection(connection);

	if (ret == -1) {
		die("could not send '%s' command to fsmonitor--daemon",
		    command);
		return -1;
	}

	return 0;
}

#endif
