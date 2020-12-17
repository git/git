#include "builtin.h"
#include "config.h"
#include "parse-options.h"
#include "fsmonitor.h"
#include "fsmonitor-ipc.h"
#include "compat/fsmonitor/fsmonitor-fs-listen.h"
#include "fsmonitor--daemon.h"
#include "simple-ipc.h"
#include "khash.h"

static const char * const builtin_fsmonitor__daemon_usage[] = {
	N_("git fsmonitor--daemon --start [<options>]"),
	N_("git fsmonitor--daemon --run [<options>]"),
	N_("git fsmonitor--daemon --stop"),
	N_("git fsmonitor--daemon --is-running"),
	N_("git fsmonitor--daemon --query <token>"),
	N_("git fsmonitor--daemon --query-index"),
	N_("git fsmonitor--daemon --flush"),
	NULL
};

#ifdef HAVE_FSMONITOR_DAEMON_BACKEND
/*
 * Global state loaded from config.
 */
#define FSMONITOR__IPC_THREADS "fsmonitor.ipcthreads"
static int fsmonitor__ipc_threads = 8;

#define FSMONITOR__START_TIMEOUT "fsmonitor.starttimeout"
static int fsmonitor__start_timeout_sec = 60;

static int fsmonitor_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, FSMONITOR__IPC_THREADS)) {
		int i = git_config_int(var, value);
		if (i < 1)
			return error(_("value of '%s' out of range: %d"),
				     FSMONITOR__IPC_THREADS, i);
		fsmonitor__ipc_threads = i;
		return 0;
	}

	if (!strcmp(var, FSMONITOR__START_TIMEOUT)) {
		int i = git_config_int(var, value);
		if (i < 0)
			return error(_("value of '%s' out of range: %d"),
				     FSMONITOR__START_TIMEOUT, i);
		fsmonitor__start_timeout_sec = i;
		return 0;
	}

	return git_default_config(var, value, cb);
}

/*
 * Acting as a CLIENT.
 *
 * Send an IPC query to a `git-fsmonitor--daemon` SERVER process and
 * ask for the changes since the given token.  This will implicitly
 * start a daemon process if necessary.  The daemon process will
 * persist after we exit.
 *
 * This feature is primarily used by the test suite.
 */
static int do_as_client__query_token(const char *token)
{
	struct strbuf answer = STRBUF_INIT;
	int ret;

	ret = fsmonitor_ipc__send_query(token, &answer);
	if (ret < 0)
		die(_("could not query fsmonitor--daemon"));

	write_in_full(1, answer.buf, answer.len);
	strbuf_release(&answer);

	return 0;
}

/*
 * Acting as a CLIENT.
 *
 * Read the `.git/index` to get the last token written to the FSMonitor index
 * extension and use that to make a query.
 *
 * This feature is primarily used by the test suite.
 */
static int do_as_client__query_from_index(void)
{
	struct index_state *istate = the_repository->index;

	setup_git_directory();
	if (do_read_index(istate, the_repository->index_file, 0) < 0)
		die("unable to read index file");
	if (!istate->fsmonitor_last_update)
		die("index file does not have fsmonitor extension");

	return do_as_client__query_token(istate->fsmonitor_last_update);
}

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

/*
 * Acting as a CLIENT.
 *
 * Send a "flush" command to the `git-fsmonitor--daemon` (if running)
 * and tell it to flush its cache.
 *
 * This feature is primarily used by the test suite to simulate a loss of
 * sync with the filesystem where we miss kernel events.
 */
static int do_as_client__send_flush(void)
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

static ipc_server_application_cb handle_client;

static int handle_client(void *data, const char *command,
			 ipc_server_reply_cb *reply,
			 struct ipc_server_reply_data *reply_data)
{
	/* struct fsmonitor_daemon_state *state = data; */
	int result;

	trace2_region_enter("fsmonitor", "handle_client", the_repository);
	trace2_data_string("fsmonitor", the_repository, "request", command);

	result = 0; /* TODO Do something here. */

	trace2_region_leave("fsmonitor", "handle_client", the_repository);

	return result;
}

static void *fsmonitor_fs_listen__thread_proc(void *_state)
{
	struct fsmonitor_daemon_state *state = _state;

	trace2_thread_start("fsm-listen");

	trace_printf_key(&trace_fsmonitor, "Watching: worktree '%s'",
			 state->path_worktree_watch.buf);
	if (state->nr_paths_watching > 1)
		trace_printf_key(&trace_fsmonitor, "Watching: gitdir '%s'",
				 state->path_gitdir_watch.buf);

	fsmonitor_fs_listen__loop(state);

	trace2_thread_exit();
	return NULL;
}

static int fsmonitor_run_daemon_1(struct fsmonitor_daemon_state *state)
{
	struct ipc_server_opts ipc_opts = {
		.nr_threads = fsmonitor__ipc_threads,

		/*
		 * We know that there are no other active threads yet,
		 * so we can let the IPC layer temporarily chdir() if
		 * it needs to when creating the server side of the
		 * Unix domain socket.
		 */
		.uds_disallow_chdir = 0
	};

	/*
	 * Start the IPC thread pool before the we've started the file
	 * system event listener thread so that we have the IPC handle
	 * before we need it.
	 */
	if (ipc_server_run_async(&state->ipc_server_data,
				 fsmonitor_ipc__get_path(), &ipc_opts,
				 handle_client, state))
		return error(_("could not start IPC thread pool"));

	/*
	 * Start the fsmonitor listener thread to collect filesystem
	 * events.
	 */
	if (pthread_create(&state->listener_thread, NULL,
			   fsmonitor_fs_listen__thread_proc, state) < 0) {
		ipc_server_stop_async(state->ipc_server_data);
		ipc_server_await(state->ipc_server_data);

		return error(_("could not start fsmonitor listener thread"));
	}

	/*
	 * The daemon is now fully functional in background threads.
	 * Wait for the IPC thread pool to shutdown (whether by client
	 * request or from filesystem activity).
	 */
	ipc_server_await(state->ipc_server_data);

	/*
	 * The fsmonitor listener thread may have received a shutdown
	 * event from the IPC thread pool, but it doesn't hurt to tell
	 * it again.  And wait for it to shutdown.
	 */
	fsmonitor_fs_listen__stop_async(state);
	pthread_join(state->listener_thread, NULL);

	return state->error_code;
}

static int fsmonitor_run_daemon(void)
{
	struct fsmonitor_daemon_state state;
	int err;

	memset(&state, 0, sizeof(state));

	pthread_mutex_init(&state.main_lock, NULL);
	state.error_code = 0;
	state.current_token_data = NULL;
	state.test_client_delay_ms = 0;

	/* Prepare to (recursively) watch the <worktree-root> directory. */
	strbuf_init(&state.path_worktree_watch, 0);
	strbuf_addstr(&state.path_worktree_watch, absolute_path(get_git_work_tree()));
	state.nr_paths_watching = 1;

	/*
	 * If ".git" is not a directory, then <gitdir> is not inside the
	 * cone of <worktree-root>, so set up a second watch for it.
	 */
	strbuf_init(&state.path_gitdir_watch, 0);
	strbuf_addbuf(&state.path_gitdir_watch, &state.path_worktree_watch);
	strbuf_addstr(&state.path_gitdir_watch, "/.git");
	if (!is_directory(state.path_gitdir_watch.buf)) {
		strbuf_reset(&state.path_gitdir_watch);
		strbuf_addstr(&state.path_gitdir_watch, absolute_path(get_git_dir()));
		state.nr_paths_watching = 2;
	}

	/*
	 * Confirm that we can create platform-specific resources for the
	 * filesystem listener before we bother starting all the threads.
	 */
	if (fsmonitor_fs_listen__ctor(&state)) {
		err = error(_("could not initialize listener thread"));
		goto done;
	}

	err = fsmonitor_run_daemon_1(&state);

done:
	pthread_mutex_destroy(&state.main_lock);
	fsmonitor_fs_listen__dtor(&state);

	ipc_server_free(state.ipc_server_data);

	strbuf_release(&state.path_worktree_watch);
	strbuf_release(&state.path_gitdir_watch);

	return err;
}

static int is_ipc_daemon_listening(void)
{
	return fsmonitor_ipc__get_state() == IPC_STATE__LISTENING;
}

static int try_to_run_foreground_daemon(void)
{
	/*
	 * Technically, we don't need to probe for an existing daemon
	 * process, since we could just call `fsmonitor_run_daemon()`
	 * and let it fail if the pipe/socket is busy.
	 *
	 * However, this method gives us a nicer error message for a
	 * common error case.
	 */
	if (is_ipc_daemon_listening())
		die("fsmonitor--daemon is already running.");

	return !!fsmonitor_run_daemon();
}

#ifndef GIT_WINDOWS_NATIVE
/*
 * This is adapted from `daemonize()`.  Use `fork()` to directly create
 * and run the daemon in a child process.  The fork-parent returns the
 * child PID so that we can wait for the child to startup before exiting.
 */
static int spawn_background_fsmonitor_daemon(pid_t *pid)
{
	*pid = fork();

	switch (*pid) {
	case 0:
		if (setsid() == -1)
			error_errno(_("setsid failed"));
		close(0);
		close(1);
		close(2);
		sanitize_stdfds();

		return !!fsmonitor_run_daemon();

	case -1:
		return error_errno(_("could not spawn fsmonitor--daemon in the background"));

	default:
		return 0;
	}
}
#else
/*
 * Conceptually like `daemonize()` but different because Windows does not
 * have `fork(2)`.  Spawn a normal Windows child process but without the
 * limitations of `start_command()` and `finish_command()`.
 */
static int spawn_background_fsmonitor_daemon(pid_t *pid)
{
	char git_exe[MAX_PATH];
	struct strvec args = STRVEC_INIT;
	int in, out;

	GetModuleFileNameA(NULL, git_exe, MAX_PATH);

	in = open("/dev/null", O_RDONLY);
	out = open("/dev/null", O_WRONLY);

	strvec_push(&args, git_exe);
	strvec_push(&args, "fsmonitor--daemon");
	strvec_push(&args, "--run");

	*pid = mingw_spawnvpe(args.v[0], args.v, NULL, NULL, in, out, out);
	close(in);
	close(out);

	strvec_clear(&args);

	if (*pid < 0)
		return error(_("could not spawn fsmonitor--daemon in the background"));

	return 0;
}
#endif

/*
 * This is adapted from `wait_or_whine()`.  Watch the child process and
 * let it get started and begin listening for requests on the socket
 * before reporting our success.
 */
static int wait_for_background_startup(pid_t pid_child)
{
	int status;
	pid_t pid_seen;
	enum ipc_active_state s;
	time_t time_limit, now;

	time(&time_limit);
	time_limit += fsmonitor__start_timeout_sec;

	for (;;) {
		pid_seen = waitpid(pid_child, &status, WNOHANG);

		if (pid_seen == -1)
			return error_errno(_("waitpid failed"));

		else if (pid_seen == 0) {
			/*
			 * The child is still running (this should be
			 * the normal case).  Try to connect to it on
			 * the socket and see if it is ready for
			 * business.
			 *
			 * If there is another daemon already running,
			 * our child will fail to start (possibly
			 * after a timeout on the lock), but we don't
			 * care (who responds) if the socket is live.
			 */
			s = fsmonitor_ipc__get_state();
			if (s == IPC_STATE__LISTENING)
				return 0;

			time(&now);
			if (now > time_limit)
				return error(_("fsmonitor--daemon not online yet"));

			continue;
		}

		else if (pid_seen == pid_child) {
			/*
			 * The new child daemon process shutdown while
			 * it was starting up, so it is not listening
			 * on the socket.
			 *
			 * Try to ping the socket in the odd chance
			 * that another daemon started (or was already
			 * running) while our child was starting.
			 *
			 * Again, we don't care who services the socket.
			 */
			s = fsmonitor_ipc__get_state();
			if (s == IPC_STATE__LISTENING)
				return 0;

			/*
			 * We don't care about the WEXITSTATUS() nor
			 * any of the WIF*(status) values because
			 * `cmd_fsmonitor__daemon()` does the `!!result`
			 * trick on all function return values.
			 *
			 * So it is sufficient to just report the
			 * early shutdown as an error.
			 */
			return error(_("fsmonitor--daemon failed to start"));
		}

		else
			return error(_("waitpid is confused"));
	}
}

static int try_to_start_background_daemon(void)
{
	pid_t pid_child;
	int ret;

	/*
	 * Before we try to create a background daemon process, see
	 * if a daemon process is already listening.  This makes it
	 * easier for us to report an already-listening error to the
	 * console, since our spawn/daemon can only report the success
	 * of creating the background process (and not whether it
	 * immediately exited).
	 */
	if (is_ipc_daemon_listening())
		die("fsmonitor--daemon is already running.");

	/*
	 * Run the actual daemon in a background process.
	 */
	ret = spawn_background_fsmonitor_daemon(&pid_child);
	if (pid_child <= 0)
		return ret;

	/*
	 * Wait (with timeout) for the background child process get
	 * started and begin listening on the socket/pipe.  This makes
	 * the "start" command more synchronous and more reliable in
	 * tests.
	 */
	ret = wait_for_background_startup(pid_child);

	return ret;
}

int cmd_fsmonitor__daemon(int argc, const char **argv, const char *prefix)
{
	enum daemon_mode {
		UNDEFINED_MODE,
		START,
		RUN,
		STOP,
		IS_RUNNING,
		QUERY,
		QUERY_INDEX,
		FLUSH,
	} mode = UNDEFINED_MODE;

	struct option options[] = {
		OPT_CMDMODE(0, "start", &mode,
			    N_("run the daemon in the background"),
			    START),
		OPT_CMDMODE(0, "run", &mode,
			    N_("run the daemon in the foreground"), RUN),
		OPT_CMDMODE(0, "stop", &mode, N_("stop the running daemon"),
			    STOP),

		OPT_CMDMODE(0, "is-running", &mode,
			    N_("test whether the daemon is running"),
			    IS_RUNNING),

		OPT_CMDMODE(0, "query", &mode,
			    N_("query the daemon (starting if necessary)"),
			    QUERY),
		OPT_CMDMODE(0, "query-index", &mode,
			    N_("query the daemon (starting if necessary) using token from index"),
			    QUERY_INDEX),
		OPT_CMDMODE(0, "flush", &mode, N_("flush cached filesystem events"),
			    FLUSH),

		OPT_GROUP(N_("Daemon options")),
		OPT_INTEGER(0, "ipc-threads",
			    &fsmonitor__ipc_threads,
			    N_("use <n> ipc worker threads")),
		OPT_INTEGER(0, "start-timeout",
			    &fsmonitor__start_timeout_sec,
			    N_("Max seconds to wait for background daemon startup")),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_fsmonitor__daemon_usage, options);

	git_config(fsmonitor_config, NULL);

	argc = parse_options(argc, argv, prefix, options,
			     builtin_fsmonitor__daemon_usage, 0);
	if (fsmonitor__ipc_threads < 1)
		die(_("invalid 'ipc-threads' value (%d)"),
		    fsmonitor__ipc_threads);

	switch (mode) {
	case START:
		return !!try_to_start_background_daemon();

	case RUN:
		return !!try_to_run_foreground_daemon();

	case STOP:
		return !!do_as_client__send_stop();

	case IS_RUNNING:
		return !is_ipc_daemon_listening();

	case QUERY:
		if (argc != 1)
			usage_with_options(builtin_fsmonitor__daemon_usage,
					   options);
		return !!do_as_client__query_token(argv[0]);

	case QUERY_INDEX:
		return !!do_as_client__query_from_index();

	case FLUSH:
		return !!do_as_client__send_flush();

	case UNDEFINED_MODE:
	default:
		die(_("Unhandled command mode %d"), mode);
	}
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
