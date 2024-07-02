/*
 * test-fsmonitor-client.c: client code to send commands/requests to
 * a `git fsmonitor--daemon` daemon.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "parse-options.h"
#include "fsmonitor-ipc.h"
#include "read-cache-ll.h"
#include "repository.h"
#include "setup.h"
#include "thread-utils.h"
#include "trace2.h"

#ifndef HAVE_FSMONITOR_DAEMON_BACKEND
int cmd__fsmonitor_client(int argc UNUSED, const char **argv UNUSED)
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
		die("could not query fsmonitor--daemon");

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

struct hammer_thread_data
{
	pthread_t pthread_id;
	int thread_nr;

	int nr_requests;
	const char *token;

	int sum_successful;
	int sum_errors;
};

static void *hammer_thread_proc(void *_hammer_thread_data)
{
	struct hammer_thread_data *data = _hammer_thread_data;
	struct strbuf answer = STRBUF_INIT;
	int k;
	int ret;

	trace2_thread_start("hammer");

	for (k = 0; k < data->nr_requests; k++) {
		strbuf_reset(&answer);

		ret = fsmonitor_ipc__send_query(data->token, &answer);
		if (ret < 0)
			data->sum_errors++;
		else
			data->sum_successful++;
	}

	strbuf_release(&answer);
	trace2_thread_exit();
	return NULL;
}

/*
 * Start a pool of client threads that will each send a series of
 * commands to the daemon.
 *
 * The goal is to overload the daemon with a sustained series of
 * concurrent requests.
 */
static int do_hammer(const char *token, int nr_threads, int nr_requests)
{
	struct hammer_thread_data *data = NULL;
	int k;
	int sum_join_errors = 0;
	int sum_commands = 0;
	int sum_errors = 0;

	if (!token || !*token)
		token = get_token_from_index();
	if (nr_threads < 1)
		nr_threads = 1;
	if (nr_requests < 1)
		nr_requests = 1;

	CALLOC_ARRAY(data, nr_threads);

	for (k = 0; k < nr_threads; k++) {
		struct hammer_thread_data *p = &data[k];
		p->thread_nr = k;
		p->nr_requests = nr_requests;
		p->token = token;

		if (pthread_create(&p->pthread_id, NULL, hammer_thread_proc, p)) {
			warning("failed to create thread[%d] skipping remainder", k);
			nr_threads = k;
			break;
		}
	}

	for (k = 0; k < nr_threads; k++) {
		struct hammer_thread_data *p = &data[k];

		if (pthread_join(p->pthread_id, NULL))
			sum_join_errors++;
		sum_commands += p->sum_successful;
		sum_errors += p->sum_errors;
	}

	fprintf(stderr, "HAMMER: [threads %d][requests %d] [ok %d][err %d][join %d]\n",
		nr_threads, nr_requests, sum_commands, sum_errors, sum_join_errors);

	free(data);

	/*
	 * Return an error if any of the _send_query requests failed.
	 * We don't care about thread create/join errors.
	 */
	return sum_errors > 0;
}

int cmd__fsmonitor_client(int argc, const char **argv)
{
	const char *subcmd;
	const char *token = NULL;
	int nr_threads = 1;
	int nr_requests = 1;

	const char * const fsmonitor_client_usage[] = {
		"test-tool fsmonitor-client query [<token>]",
		"test-tool fsmonitor-client flush",
		"test-tool fsmonitor-client hammer [<token>] [<threads>] [<requests>]",
		NULL,
	};

	struct option options[] = {
		OPT_STRING(0, "token", &token, "token",
			   "command token to send to the server"),

		OPT_INTEGER(0, "threads", &nr_threads, "number of client threads"),
		OPT_INTEGER(0, "requests", &nr_requests, "number of requests per thread"),

		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, options, fsmonitor_client_usage, 0);

	if (argc != 1)
		usage_with_options(fsmonitor_client_usage, options);

	subcmd = argv[0];

	setup_git_directory();

	if (!strcmp(subcmd, "query"))
		return !!do_send_query(token);

	if (!strcmp(subcmd, "flush"))
		return !!do_send_flush();

	if (!strcmp(subcmd, "hammer"))
		return !!do_hammer(token, nr_threads, nr_requests);

	die("Unhandled subcommand: '%s'", subcmd);
}
#endif
