#include "builtin.h"
#include "config.h"
#include "parse-options.h"
#include "fsmonitor.h"
#include "fsmonitor-ipc.h"
#include "compat/fsmonitor/fsmonitor-fs-listen.h"
#include "fsmonitor--daemon.h"
#include "simple-ipc.h"
#include "khash.h"
#include "pkt-line.h"

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

enum fsmonitor_cookie_item_result {
	FCIR_ERROR = -1, /* could not create cookie file ? */
	FCIR_INIT = 0,
	FCIR_SEEN,
	FCIR_ABORT,
};

struct fsmonitor_cookie_item {
	struct hashmap_entry entry;
	const char *name;
	enum fsmonitor_cookie_item_result result;
};

static int cookies_cmp(const void *data, const struct hashmap_entry *he1,
		     const struct hashmap_entry *he2, const void *keydata)
{
	const struct fsmonitor_cookie_item *a =
		container_of(he1, const struct fsmonitor_cookie_item, entry);
	const struct fsmonitor_cookie_item *b =
		container_of(he2, const struct fsmonitor_cookie_item, entry);

	return strcmp(a->name, keydata ? keydata : b->name);
}

static enum fsmonitor_cookie_item_result fsmonitor_wait_for_cookie(
	struct fsmonitor_daemon_state *state)
{
	int fd;
	struct fsmonitor_cookie_item cookie;
	struct strbuf cookie_pathname = STRBUF_INIT;
	struct strbuf cookie_filename = STRBUF_INIT;
	const char *slash;
	int my_cookie_seq;

	pthread_mutex_lock(&state->main_lock);

	my_cookie_seq = state->cookie_seq++;

	strbuf_addbuf(&cookie_pathname, &state->path_cookie_prefix);
	strbuf_addf(&cookie_pathname, "%i-%i", getpid(), my_cookie_seq);

	slash = find_last_dir_sep(cookie_pathname.buf);
	if (slash)
		strbuf_addstr(&cookie_filename, slash + 1);
	else
		strbuf_addbuf(&cookie_filename, &cookie_pathname);
	cookie.name = strbuf_detach(&cookie_filename, NULL);
	cookie.result = FCIR_INIT;
	// TODO should we have case-insenstive hash (and in cookie_cmp()) ??
	hashmap_entry_init(&cookie.entry, strhash(cookie.name));

	/*
	 * Warning: we are putting the address of a stack variable into a
	 * global hashmap.  This feels dodgy.  We must ensure that we remove
	 * it before this thread and stack frame returns.
	 */
	hashmap_add(&state->cookies, &cookie.entry);

	trace_printf_key(&trace_fsmonitor, "cookie-wait: '%s' '%s'",
			 cookie.name, cookie_pathname.buf);

	/*
	 * Create the cookie file on disk and then wait for a notification
	 * that the listener thread has seen it.
	 */
	fd = open(cookie_pathname.buf, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd >= 0) {
		close(fd);
		unlink_or_warn(cookie_pathname.buf);

		while (cookie.result == FCIR_INIT)
			pthread_cond_wait(&state->cookies_cond,
					  &state->main_lock);

		hashmap_remove(&state->cookies, &cookie.entry, NULL);
	} else {
		error_errno(_("could not create fsmonitor cookie '%s'"),
			    cookie.name);

		cookie.result = FCIR_ERROR;
		hashmap_remove(&state->cookies, &cookie.entry, NULL);
	}

	pthread_mutex_unlock(&state->main_lock);

	free((char*)cookie.name);
	strbuf_release(&cookie_pathname);
	return cookie.result;
}

/*
 * Mark these cookies as _SEEN and wake up the corresponding client threads.
 */
static void fsmonitor_cookie_mark_seen(struct fsmonitor_daemon_state *state,
				       const struct string_list *cookie_names)
{
	/* assert state->main_lock */

	int k;
	int nr_seen = 0;

	for (k = 0; k < cookie_names->nr; k++) {
		struct fsmonitor_cookie_item key;
		struct fsmonitor_cookie_item *cookie;

		key.name = cookie_names->items[k].string;
		hashmap_entry_init(&key.entry, strhash(key.name));

		cookie = hashmap_get_entry(&state->cookies, &key, entry, NULL);
		if (cookie) {
			trace_printf_key(&trace_fsmonitor, "cookie-seen: '%s'",
					 cookie->name);
			cookie->result = FCIR_SEEN;
			nr_seen++;
		}
	}

	if (nr_seen)
		pthread_cond_broadcast(&state->cookies_cond);
}

/*
 * Set _ABORT on all pending cookies and wake up all client threads.
 */
static void fsmonitor_cookie_abort_all(struct fsmonitor_daemon_state *state)
{
	/* assert state->main_lock */

	struct hashmap_iter iter;
	struct fsmonitor_cookie_item *cookie;
	int nr_aborted = 0;

	hashmap_for_each_entry(&state->cookies, &iter, cookie, entry) {
		trace_printf_key(&trace_fsmonitor, "cookie-abort: '%s'",
				 cookie->name);
		cookie->result = FCIR_ABORT;
		nr_aborted++;
	}

	if (nr_aborted)
		pthread_cond_broadcast(&state->cookies_cond);
}

static int lookup_client_test_delay(void)
{
	static int delay_ms = -1;

	const char *s;
	int ms;

	if (delay_ms >= 0)
		return delay_ms;

	delay_ms = 0;

	s = getenv("GIT_TEST_FSMONITOR_CLIENT_DELAY");
	if (!s)
		return delay_ms;

	ms = atoi(s);
	if (ms < 0)
		return delay_ms;

	delay_ms = ms;
	return delay_ms;
}

/*
 * Requests to and from a FSMonitor Protocol V2 provider use an opaque
 * "token" as a virtual timestamp.  Clients can request a summary of all
 * created/deleted/modified files relative to a token.  In the response,
 * clients receive a new token for the next (relative) request.
 *
 *
 * Token Format
 * ============
 *
 * The contents of the token are private and provider-specific.
 *
 * For the built-in fsmonitor--daemon, we define a token as follows:
 *
 *     "builtin" ":" <token_id> ":" <sequence_nr>
 *
 * The <token_id> is an arbitrary OPAQUE string, such as a GUID,
 * UUID, or {timestamp,pid}.  It is used to group all filesystem
 * events that happened while the daemon was monitoring (and in-sync
 * with the filesystem).
 *
 *     Unlike FSMonitor Protocol V1, it is not defined as a timestamp
 *     and does not define less-than/greater-than relationships.
 *     (There are too many race conditions to rely on file system
 *     event timestamps.)
 *
 * The <sequence_nr> is a simple integer incremented for each event
 * received.  When a new <token_id> is created, the <sequence_nr> is
 * reset to zero.
 *
 *
 * About Token Ids
 * ===============
 *
 * A new token_id is created:
 *
 * [1] each time the daemon is started.
 *
 * [2] any time that the daemon must re-sync with the filesystem
 *     (such as when the kernel drops or we miss events on a very
 *     active volume).
 *
 * [3] in response to a client "flush" command (for dropped event
 *     testing).
 *
 * [4] MAYBE We might want to change the token_id after very complex
 *     filesystem operations are performed, such as a directory move
 *     sequence that affects many files within.  It might be simpler
 *     to just give up and fake a re-sync (and let the client do a
 *     full scan) than try to enumerate the effects of such a change.
 *
 * When a new token_id is created, the daemon is free to discard all
 * cached filesystem events associated with any previous token_ids.
 * Events associated with a non-current token_id will never be sent
 * to a client.  A token_id change implicitly means that the daemon
 * has gap in its event history.
 *
 * Therefore, clients that present a token with a stale (non-current)
 * token_id will always be given a trivial response.
 */
struct fsmonitor_token_data {
	struct strbuf token_id;
	struct fsmonitor_batch *batch_head;
	struct fsmonitor_batch *batch_tail;
	uint64_t client_ref_count;
};

static struct fsmonitor_token_data *fsmonitor_new_token_data(void)
{
	static int test_env_value = -1;
	static uint64_t flush_count = 0;
	struct fsmonitor_token_data *token;

	token = (struct fsmonitor_token_data *)xcalloc(1, sizeof(*token));

	strbuf_init(&token->token_id, 0);
	token->batch_head = NULL;
	token->batch_tail = NULL;
	token->client_ref_count = 0;

	if (test_env_value < 0)
		test_env_value = git_env_bool("GIT_TEST_FSMONITOR_TOKEN", 0);

	if (!test_env_value) {
		struct timeval tv;
		struct tm tm;
		time_t secs;

		gettimeofday(&tv, NULL);
		secs = tv.tv_sec;
		gmtime_r(&secs, &tm);

		strbuf_addf(&token->token_id,
			    "%"PRIu64".%d.%4d%02d%02dT%02d%02d%02d.%06ldZ",
			    flush_count++,
			    getpid(),
			    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			    tm.tm_hour, tm.tm_min, tm.tm_sec,
			    (long)tv.tv_usec);
	} else {
		strbuf_addf(&token->token_id, "test_%08x", test_env_value++);
	}

	return token;
}

struct fsmonitor_batch {
	struct fsmonitor_batch *next;
	uint64_t batch_seq_nr;
	const char **interned_paths;
	size_t nr, alloc;
	time_t pinned_time;
};

struct fsmonitor_batch *fsmonitor_batch__new(void)
{
	struct fsmonitor_batch *batch = xcalloc(1, sizeof(*batch));

	return batch;
}

struct fsmonitor_batch *fsmonitor_batch__free(struct fsmonitor_batch *batch)
{
	struct fsmonitor_batch *next;

	if (!batch)
		return NULL;

	next = batch->next;

	/*
	 * The actual strings within the array are interned, so we don't
	 * own them.
	 */
	free(batch->interned_paths);

	return next;
}

void fsmonitor_batch__add_path(struct fsmonitor_batch *batch,
			       const char *path)
{
	const char *interned_path = strintern(path);

	trace_printf_key(&trace_fsmonitor, "event: %s", interned_path);

	ALLOC_GROW(batch->interned_paths, batch->nr + 1, batch->alloc);
	batch->interned_paths[batch->nr++] = interned_path;
}

static void fsmonitor_batch__combine(struct fsmonitor_batch *batch_dest,
				     const struct fsmonitor_batch *batch_src)
{
	/* assert state->main_lock */

	size_t k;

	ALLOC_GROW(batch_dest->interned_paths,
		   batch_dest->nr + batch_src->nr + 1,
		   batch_dest->alloc);

	for (k = 0; k < batch_src->nr; k++)
		batch_dest->interned_paths[batch_dest->nr++] =
			batch_src->interned_paths[k];
}

/*
 * To keep the batch list from growing unbounded in response to filesystem
 * activity, we try to truncate old batches from the end of the list as
 * they become irrelevant.
 *
 * We assume that the .git/index will be updated with the most recent token
 * any time the index is updated.  And future commands will only ask for
 * recent changes *since* that new token.  So as tokens advance into the
 * future, older batch items will never be requested/needed.  So we can
 * truncate them without loss of functionality.
 *
 * However, multiple commands may be talking to the daemon concurrently
 * or perform a slow command, so a little "token skew" is possible.
 * Therefore, we want this to be a little bit lazy and have a generous
 * delay.
 *
 * The current reader thread walked backwards in time from `token->batch_head`
 * back to `batch_marker` somewhere in the middle of the batch list.
 *
 * Let's walk backwards in time from that marker an arbitrary delay
 * and truncate the list there.  Note that these timestamps are completely
 * artificial (based on when we pinned the batch item) and not on any
 * filesystem activity.
 */
#define MY_TIME_DELAY (5 * 60) /* seconds */

static void fsmonitor_batch__truncate(struct fsmonitor_daemon_state *state,
				      const struct fsmonitor_batch *batch_marker)
{
	/* assert state->main_lock */

	const struct fsmonitor_batch *batch;
	struct fsmonitor_batch *rest;
	struct fsmonitor_batch *p;
	time_t t;

	if (!batch_marker)
		return;

	trace_printf_key(&trace_fsmonitor, "TRNC mark (%"PRIu64",%"PRIu64")",
			 batch_marker->batch_seq_nr,
			 (uint64_t)batch_marker->pinned_time);

	for (batch = batch_marker; batch; batch = batch->next) {
		if (!batch->pinned_time) /* an overflow batch */
			continue;

		t = batch->pinned_time + MY_TIME_DELAY;
		if (t > batch_marker->pinned_time) /* too close to marker */
			continue;

		goto truncate_past_here;
	}

	return;

truncate_past_here:
	state->current_token_data->batch_tail = (struct fsmonitor_batch *)batch;

	rest = ((struct fsmonitor_batch *)batch)->next;
	((struct fsmonitor_batch *)batch)->next = NULL;

	for (p = rest; p; p = fsmonitor_batch__free(p)) {
		trace_printf_key(&trace_fsmonitor,
				 "TRNC kill (%"PRIu64",%"PRIu64")",
				 p->batch_seq_nr, (uint64_t)p->pinned_time);
	}
}

static void fsmonitor_free_token_data(struct fsmonitor_token_data *token)
{
	struct fsmonitor_batch *p;

	if (!token)
		return;

	assert(token->client_ref_count == 0);

	strbuf_release(&token->token_id);

	for (p = token->batch_head; p; p = fsmonitor_batch__free(p))
		;

	free(token);
}

/*
 * Flush all of our cached data about the filesystem.  Call this if we
 * lose sync with the filesystem and miss some notification events.
 *
 * [1] If we are missing events, then we no longer have a complete
 *     history of the directory (relative to our current start token).
 *     We should create a new token and start fresh (as if we just
 *     booted up).
 *
 * [2] Some of those lost events may have been for cookie files.  We
 *     should assume the worst and abort them rather letting them starve.
 *
 * If there are no readers of the the current token data series, we
 * can free it now.  Otherwise, let the last reader free it.  Either
 * way, the old token data series is no longer associated with our
 * state data.
 */
void fsmonitor_force_resync(struct fsmonitor_daemon_state *state)
{
	struct fsmonitor_token_data *free_me = NULL;
	struct fsmonitor_token_data *new_one = NULL;

	new_one = fsmonitor_new_token_data();

	pthread_mutex_lock(&state->main_lock);

	trace_printf_key(&trace_fsmonitor,
			 "force resync [old '%s'][new '%s']",
			 state->current_token_data->token_id.buf,
			 new_one->token_id.buf);

	fsmonitor_cookie_abort_all(state);

	if (state->current_token_data->client_ref_count == 0)
		free_me = state->current_token_data;
	state->current_token_data = new_one;

	pthread_mutex_unlock(&state->main_lock);

	fsmonitor_free_token_data(free_me);
}

/*
 * Format an opaque token string to send to the client.
 */
static void fsmonitor_format_response_token(
	struct strbuf *response_token,
	const struct strbuf *response_token_id,
	const struct fsmonitor_batch *batch)
{
	uint64_t seq_nr = (batch) ? batch->batch_seq_nr + 1 : 0;

	strbuf_reset(response_token);
	strbuf_addf(response_token, "builtin:%s:%"PRIu64,
		    response_token_id->buf, seq_nr);
}

/*
 * Parse an opaque token from the client.
 */
static int fsmonitor_parse_client_token(const char *buf_token,
					struct strbuf *requested_token_id,
					uint64_t *seq_nr)
{
	const char *p;
	char *p_end;

	strbuf_reset(requested_token_id);
	*seq_nr = 0;

	if (!skip_prefix(buf_token, "builtin:", &p))
		return 1;

	while (*p && *p != ':')
		strbuf_addch(requested_token_id, *p++);
	if (!*p++)
		return 1;

	*seq_nr = (uint64_t)strtoumax(p, &p_end, 10);
	if (*p_end)
		return 1;

	return 0;
}

KHASH_INIT(str, const char *, int, 0, kh_str_hash_func, kh_str_hash_equal);

static int do_handle_client(struct fsmonitor_daemon_state *state,
			    const char *command,
			    ipc_server_reply_cb *reply,
			    struct ipc_server_reply_data *reply_data)
{
	struct fsmonitor_token_data *token_data = NULL;
	struct strbuf response_token = STRBUF_INIT;
	struct strbuf requested_token_id = STRBUF_INIT;
	struct strbuf payload = STRBUF_INIT;
	uint64_t requested_oldest_seq_nr = 0;
	uint64_t total_response_len = 0;
	const char *p;
	const struct fsmonitor_batch *batch_head;
	const struct fsmonitor_batch *batch;
	intmax_t count = 0, duplicates = 0;
	kh_str_t *shown;
	int hash_ret;
	int result;
	enum fsmonitor_cookie_item_result cookie_result;

	/*
	 * We expect `command` to be of the form:
	 *
	 * <command> := quit NUL
	 *            | flush NUL
	 *            | <V1-time-since-epoch-ns> NUL
	 *            | <V2-opaque-fsmonitor-token> NUL
	 */

	if (!strcmp(command, "quit")) {
		/*
		 * A client has requested over the socket/pipe that the
		 * daemon shutdown.
		 *
		 * Tell the IPC thread pool to shutdown (which completes
		 * the await in the main thread (which can stop the
		 * fsmonitor listener thread)).
		 *
		 * There is no reply to the client.
		 */
		return SIMPLE_IPC_QUIT;
	}

	/*
	 * For testing purposes, introduce an artificial delay in this
	 * worker to allow the filesystem listener thread to receive
	 * any fs events that may have been generated by the client
	 * process on the other end of the pipe/socket.  This helps
	 * make the CI/PR test suite runs a little more predictable
	 * and hopefully eliminates the need to introduce `sleep`
	 * commands in the test scripts.
	 */
	if (state->test_client_delay_ms)
		sleep_millisec(state->test_client_delay_ms);

	if (!strcmp(command, "flush")) {
		/*
		 * Flush all of our cached data and generate a new token
		 * just like if we lost sync with the filesystem.
		 *
		 * Then send a trivial response using the new token.
		 */
		fsmonitor_force_resync(state);
		result = 0;
		goto send_trivial_response;
	}

	if (!skip_prefix(command, "builtin:", &p)) {
		/* assume V1 timestamp or garbage */

		char *p_end;

		strtoumax(command, &p_end, 10);
		trace_printf_key(&trace_fsmonitor,
				 ((*p_end) ?
				  "fsmonitor: invalid command line '%s'" :
				  "fsmonitor: unsupported V1 protocol '%s'"),
				 command);
		result = -1;
		goto send_trivial_response;
	}

	/* try V2 token */

	if (fsmonitor_parse_client_token(command, &requested_token_id,
					 &requested_oldest_seq_nr)) {
		trace_printf_key(&trace_fsmonitor,
				 "fsmonitor: invalid V2 protocol token '%s'",
				 command);
		result = -1;
		goto send_trivial_response;
	}

	pthread_mutex_lock(&state->main_lock);

	if (!state->current_token_data) {
		/*
		 * We don't have a current token.  This may mean that
		 * the listener thread has not yet started.
		 */
		pthread_mutex_unlock(&state->main_lock);
		result = 0;
		goto send_trivial_response;
	}
	if (strcmp(requested_token_id.buf,
		   state->current_token_data->token_id.buf)) {
		/*
		 * The client last spoke to a different daemon
		 * instance -OR- the daemon had to resync with
		 * the filesystem (and lost events), so reject.
		 */
		pthread_mutex_unlock(&state->main_lock);
		result = 0;
		trace2_data_string("fsmonitor", the_repository,
				   "response/token", "different");
		goto send_trivial_response;
	}
	if (!state->current_token_data->batch_tail) {
		/*
		 * The listener has not received any filesystem
		 * events yet since we created the current token.
		 * We can respond with an empty list, since the
		 * client has already seen the current token and
		 * we have nothing new to report.  (This is
		 * instead of sending a trivial response.)
		 */
		pthread_mutex_unlock(&state->main_lock);
		result = 0;
		goto send_empty_response;
	}
	if (requested_oldest_seq_nr <
	    state->current_token_data->batch_tail->batch_seq_nr) {
		/*
		 * The client wants older events than we have for
		 * this token_id.  This means that the end of our
		 * batch list was truncated and we cannot give the
		 * client a complete snapshot relative to their
		 * request.
		 */
		pthread_mutex_unlock(&state->main_lock);

		trace_printf_key(&trace_fsmonitor,
				 "client requested truncated data");
		result = 0;
		goto send_trivial_response;
	}

	pthread_mutex_unlock(&state->main_lock);

	/*
	 * Write a cookie file inside the directory being watched in an
	 * effort to flush out existing filesystem events that we actually
	 * care about.  Suspend this client thread until we see the filesystem
	 * events for this cookie file.
	 */
	cookie_result = fsmonitor_wait_for_cookie(state);
	if (cookie_result != FCIR_SEEN) {
		error(_("fsmonitor: cookie_result '%d' != SEEN"),
		      cookie_result);
		result = 0;
		goto send_trivial_response;
	}

	pthread_mutex_lock(&state->main_lock);

	if (strcmp(requested_token_id.buf,
		   state->current_token_data->token_id.buf)) {
		/*
		 * Ack! The listener thread lost sync with the filesystem
		 * and created a new token while we were waiting for the
		 * cookie file to be created!  Just give up.
		 */
		pthread_mutex_unlock(&state->main_lock);

		trace_printf_key(&trace_fsmonitor,
				 "lost filesystem sync");
		result = 0;
		goto send_trivial_response;
	}

	/*
	 * We're going to hold onto a pointer to the current
	 * token-data while we walk the list of batches of files.
	 * During this time, we will NOT be under the lock.
	 * So we ref-count it.
	 *
	 * This allows the listener thread to continue prepending
	 * new batches of items to the token-data (which we'll ignore).
	 *
	 * AND it allows the listener thread to do a token-reset
	 * (and install a new `current_token_data`).
	 *
	 * We mark the current head of the batch list as "pinned" so
	 * that the listener thread will treat this item as read-only
	 * (and prevent any more paths from being added to it) from
	 * now on.
	 */
	token_data = state->current_token_data;
	token_data->client_ref_count++;

	batch_head = token_data->batch_head;
	((struct fsmonitor_batch *)batch_head)->pinned_time = time(NULL);

	pthread_mutex_unlock(&state->main_lock);

	/*
	 * FSMonitor Protocol V2 requires that we send a response header
	 * with a "new current token" and then all of the paths that changed
	 * since the "requested token".
	 */
	fsmonitor_format_response_token(&response_token,
					&token_data->token_id,
					batch_head);

	reply(reply_data, response_token.buf, response_token.len + 1);
	total_response_len += response_token.len + 1;

	trace2_data_string("fsmonitor", the_repository, "response/token",
			   response_token.buf);
	trace_printf_key(&trace_fsmonitor, "response token: %s", response_token.buf);

	shown = kh_init_str();
	for (batch = batch_head;
	     batch && batch->batch_seq_nr >= requested_oldest_seq_nr;
	     batch = batch->next) {
		size_t k;

		for (k = 0; k < batch->nr; k++) {
			const char *s = batch->interned_paths[k];
			size_t s_len;

			if (kh_get_str(shown, s) != kh_end(shown))
				duplicates++;
			else {
				kh_put_str(shown, s, &hash_ret);

				trace_printf_key(&trace_fsmonitor,
						 "send[%"PRIuMAX"]: %s",
						 count, s);

				/* Each path gets written with a trailing NUL */
				s_len = strlen(s) + 1;

				if (payload.len + s_len >=
				    LARGE_PACKET_DATA_MAX) {
					reply(reply_data, payload.buf,
					      payload.len);
					total_response_len += payload.len;
					strbuf_reset(&payload);
				}

				strbuf_add(&payload, s, s_len);
				count++;
			}
		}
	}

	if (payload.len) {
		reply(reply_data, payload.buf, payload.len);
		total_response_len += payload.len;
	}

	kh_release_str(shown);

	pthread_mutex_lock(&state->main_lock);
	if (token_data->client_ref_count > 0)
		token_data->client_ref_count--;

	if (token_data->client_ref_count == 0) {
		if (token_data != state->current_token_data) {
			/*
			 * The listener thread did a token-reset while we were
			 * walking the batch list.  Therefore, this token is
			 * stale and can be discarded completely.  If we are
			 * the last reader thread using this token, we own
			 * that work.
			 */
			fsmonitor_free_token_data(token_data);
		} else if (batch) {
			/*
			 * This batch is the first item in the list
			 * that is older than the requested sequence
			 * number and might be considered to be
			 * obsolete.  See if we can truncate the list
			 * and save some memory.
			 */
			fsmonitor_batch__truncate(state, batch);
		}
	}

	pthread_mutex_unlock(&state->main_lock);

	trace2_data_intmax("fsmonitor", the_repository, "response/length", total_response_len);
	trace2_data_intmax("fsmonitor", the_repository, "response/count/files", count);
	trace2_data_intmax("fsmonitor", the_repository, "response/count/duplicates", duplicates);

	strbuf_release(&response_token);
	strbuf_release(&requested_token_id);
	strbuf_release(&payload);

	return 0;

send_trivial_response:
	pthread_mutex_lock(&state->main_lock);
	fsmonitor_format_response_token(&response_token,
					&state->current_token_data->token_id,
					state->current_token_data->batch_head);
	pthread_mutex_unlock(&state->main_lock);

	reply(reply_data, response_token.buf, response_token.len + 1);
	trace2_data_string("fsmonitor", the_repository, "response/token",
			   response_token.buf);
	reply(reply_data, "/", 2);
	trace2_data_intmax("fsmonitor", the_repository, "response/trivial", 1);

	strbuf_release(&response_token);
	strbuf_release(&requested_token_id);

	return result;

send_empty_response:
	pthread_mutex_lock(&state->main_lock);
	fsmonitor_format_response_token(&response_token,
					&state->current_token_data->token_id,
					NULL);
	pthread_mutex_unlock(&state->main_lock);

	reply(reply_data, response_token.buf, response_token.len + 1);
	trace2_data_string("fsmonitor", the_repository, "response/token",
			   response_token.buf);
	trace2_data_intmax("fsmonitor", the_repository, "response/empty", 1);

	strbuf_release(&response_token);
	strbuf_release(&requested_token_id);

	return 0;
}

static ipc_server_application_cb handle_client;

static int handle_client(void *data, const char *command,
			 ipc_server_reply_cb *reply,
			 struct ipc_server_reply_data *reply_data)
{
	struct fsmonitor_daemon_state *state = data;
	int result;

	trace_printf_key(&trace_fsmonitor, "requested token: %s", command);

	trace2_region_enter("fsmonitor", "handle_client", the_repository);
	trace2_data_string("fsmonitor", the_repository, "request", command);

	result = do_handle_client(state, command, reply, reply_data);

	trace2_region_leave("fsmonitor", "handle_client", the_repository);

	return result;
}

#define FSMONITOR_COOKIE_PREFIX ".fsmonitor-daemon-"

enum fsmonitor_path_type fsmonitor_classify_path_workdir_relative(
	const char *rel)
{
	if (fspathncmp(rel, ".git", 4))
		return IS_WORKDIR_PATH;
	rel += 4;

	if (!*rel)
		return IS_DOT_GIT;
	if (*rel != '/')
		return IS_WORKDIR_PATH; /* e.g. .gitignore */
	rel++;

	if (!fspathncmp(rel, FSMONITOR_COOKIE_PREFIX,
			strlen(FSMONITOR_COOKIE_PREFIX)))
		return IS_INSIDE_DOT_GIT_WITH_COOKIE_PREFIX;

	return IS_INSIDE_DOT_GIT;
}

enum fsmonitor_path_type fsmonitor_classify_path_gitdir_relative(
	const char *rel)
{
	if (!fspathncmp(rel, FSMONITOR_COOKIE_PREFIX,
			strlen(FSMONITOR_COOKIE_PREFIX)))
		return IS_INSIDE_GITDIR_WITH_COOKIE_PREFIX;

	return IS_INSIDE_GITDIR;
}

static enum fsmonitor_path_type try_classify_workdir_abs_path(
	struct fsmonitor_daemon_state *state,
	const char *path)
{
	const char *rel;

	if (fspathncmp(path, state->path_worktree_watch.buf,
		       state->path_worktree_watch.len))
		return IS_OUTSIDE_CONE;

	rel = path + state->path_worktree_watch.len;

	if (!*rel)
		return IS_WORKDIR_PATH; /* it is the root dir exactly */
	if (*rel != '/')
		return IS_OUTSIDE_CONE;
	rel++;

	return fsmonitor_classify_path_workdir_relative(rel);
}

enum fsmonitor_path_type fsmonitor_classify_path_absolute(
	struct fsmonitor_daemon_state *state,
	const char *path)
{
	const char *rel;
	enum fsmonitor_path_type t;

	t = try_classify_workdir_abs_path(state, path);
	if (state->nr_paths_watching == 1)
		return t;
	if (t != IS_OUTSIDE_CONE)
		return t;

	if (fspathncmp(path, state->path_gitdir_watch.buf,
		       state->path_gitdir_watch.len))
		return IS_OUTSIDE_CONE;

	rel = path + state->path_gitdir_watch.len;

	if (!*rel)
		return IS_GITDIR; /* it is the <gitdir> exactly */
	if (*rel != '/')
		return IS_OUTSIDE_CONE;
	rel++;

	return fsmonitor_classify_path_gitdir_relative(rel);
}

/*
 * We try to combine small batches at the front of the batch-list to avoid
 * having a long list.  This hopefully makes it a little easier when we want
 * to truncate and maintain the list.  However, we don't want the paths array
 * to just keep growing and growing with realloc, so we insert an arbitrary
 * limit.
 */
#define MY_COMBINE_LIMIT (1024)

void fsmonitor_publish(struct fsmonitor_daemon_state *state,
		       struct fsmonitor_batch *batch,
		       const struct string_list *cookie_names)
{
	if (!batch && !cookie_names->nr)
		return;

	pthread_mutex_lock(&state->main_lock);

	if (batch) {
		struct fsmonitor_batch *head;

		head = state->current_token_data->batch_head;
		if (!head) {
			batch->batch_seq_nr = 0;
			batch->next = NULL;
			state->current_token_data->batch_head = batch;
			state->current_token_data->batch_tail = batch;
		} else if (head->pinned_time) {
			/*
			 * We cannot alter the current batch list
			 * because:
			 *
			 * [a] it is being transmitted to at least one
			 * client and the handle_client() thread has a
			 * ref-count, but not a lock on the batch list
			 * starting with this item.
			 *
			 * [b] it has been transmitted in the past to
			 * at least one client such that future
			 * requests are relative to this head batch.
			 *
			 * So, we can only prepend a new batch onto
			 * the front of the list.
			 */
			batch->batch_seq_nr = head->batch_seq_nr + 1;
			batch->next = head;
			state->current_token_data->batch_head = batch;
		} else if (head->nr + batch->nr > MY_COMBINE_LIMIT) {
			/*
			 * The head batch in the list has never been
			 * transmitted to a client, but folding the
			 * contents of the new batch onto it would
			 * exceed our arbitrary limit, so just prepend
			 * the new batch onto the list.
			 */
			batch->batch_seq_nr = head->batch_seq_nr + 1;
			batch->next = head;
			state->current_token_data->batch_head = batch;
		} else {
			/*
			 * We are free to append the paths in the given
			 * batch onto the end of the current head batch.
			 */
			fsmonitor_batch__combine(head, batch);
			fsmonitor_batch__free(batch);
		}
	}

	if (cookie_names->nr)
		fsmonitor_cookie_mark_seen(state, cookie_names);

	pthread_mutex_unlock(&state->main_lock);
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

	pthread_mutex_lock(&state->main_lock);
	if (state->current_token_data &&
	    state->current_token_data->client_ref_count == 0)
		fsmonitor_free_token_data(state->current_token_data);
	state->current_token_data = NULL;
	pthread_mutex_unlock(&state->main_lock);

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

	hashmap_init(&state.cookies, cookies_cmp, NULL, 0);
	pthread_mutex_init(&state.main_lock, NULL);
	pthread_cond_init(&state.cookies_cond, NULL);
	state.error_code = 0;
	state.current_token_data = fsmonitor_new_token_data();
	state.test_client_delay_ms = lookup_client_test_delay();

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
	 * We will write filesystem syncing cookie files into
	 * <gitdir>/<cookie-prefix><pid>-<seq>.
	 */
	strbuf_init(&state.path_cookie_prefix, 0);
	strbuf_addbuf(&state.path_cookie_prefix, &state.path_gitdir_watch);
	strbuf_addch(&state.path_cookie_prefix, '/');
	strbuf_addstr(&state.path_cookie_prefix, FSMONITOR_COOKIE_PREFIX);

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
	pthread_cond_destroy(&state.cookies_cond);
	pthread_mutex_destroy(&state.main_lock);
	fsmonitor_fs_listen__dtor(&state);

	ipc_server_free(state.ipc_server_data);

	strbuf_release(&state.path_worktree_watch);
	strbuf_release(&state.path_gitdir_watch);
	strbuf_release(&state.path_cookie_prefix);

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
