#ifndef FSMONITOR_DAEMON_H
#define FSMONITOR_DAEMON_H

#ifdef HAVE_FSMONITOR_DAEMON_BACKEND

#include "cache.h"
#include "dir.h"
#include "run-command.h"
#include "simple-ipc.h"
#include "thread-utils.h"

struct fsmonitor_batch;
struct fsmonitor_token_data;

/*
 * Create a new batch of path(s).  The returned batch is considered
 * private and not linked into the fsmonitor daemon state.  The caller
 * should fill this batch with one or more paths and then publish it.
 */
struct fsmonitor_batch *fsmonitor_batch__new(void);

/*
 * Free the list of batches starting with this one.
 */
void fsmonitor_batch__free_list(struct fsmonitor_batch *batch);

/*
 * Add this path to this batch of modified files.
 *
 * The batch should be private and NOT (yet) linked into the fsmonitor
 * daemon state and therefore not yet visible to worker threads and so
 * no locking is required.
 */
void fsmonitor_batch__add_path(struct fsmonitor_batch *batch, const char *path);

struct fsm_listen_data; /* opaque platform-specific data for listener thread */
struct fsm_health_data; /* opaque platform-specific data for health thread */

struct fsmonitor_daemon_state {
	pthread_t listener_thread;
	pthread_t health_thread;
	pthread_mutex_t main_lock;

	struct strbuf path_worktree_watch;
	struct strbuf path_gitdir_watch;
	int nr_paths_watching;

	struct fsmonitor_token_data *current_token_data;

	struct strbuf path_cookie_prefix;
	pthread_cond_t cookies_cond;
	int cookie_seq;
	struct hashmap cookies;

	int listen_error_code;
	int health_error_code;
	struct fsm_listen_data *listen_data;
	struct fsm_health_data *health_data;

	struct ipc_server_data *ipc_server_data;
	struct strbuf path_ipc;
};

/*
 * Pathname classifications.
 *
 * The daemon classifies the pathnames that it receives from file
 * system notification events into the following categories and uses
 * that to decide whether clients are told about them.  (And to watch
 * for file system synchronization events.)
 *
 * The client should only care about paths within the working
 * directory proper (inside the working directory and not ".git" nor
 * inside of ".git/").  That is, the client has read the index and is
 * asking for a list of any paths in the working directory that have
 * been modified since the last token.  The client does not care about
 * file system changes within the .git directory (such as new loose
 * objects or packfiles).  So the client will only receive paths that
 * are classified as IS_WORKDIR_PATH.
 *
 * The daemon uses the IS_DOT_GIT and IS_GITDIR internally to mean the
 * exact ".git" directory or GITDIR.  If the daemon receives a delete
 * event for either of these directories, it will automatically
 * shutdown, for example.
 *
 * Note that the daemon DOES NOT explicitly watch nor special case the
 * ".git/index" file.  The daemon does not read the index and does not
 * have any internal index-relative state.  The daemon only collects
 * the set of modified paths within the working directory.
 */
enum fsmonitor_path_type {
	IS_WORKDIR_PATH = 0,

	IS_DOT_GIT,
	IS_INSIDE_DOT_GIT,
	IS_INSIDE_DOT_GIT_WITH_COOKIE_PREFIX,

	IS_GITDIR,
	IS_INSIDE_GITDIR,
	IS_INSIDE_GITDIR_WITH_COOKIE_PREFIX,

	IS_OUTSIDE_CONE,
};

/*
 * Classify a pathname relative to the root of the working directory.
 */
enum fsmonitor_path_type fsmonitor_classify_path_workdir_relative(
	const char *relative_path);

/*
 * Classify a pathname relative to a <gitdir> that is external to the
 * worktree directory.
 */
enum fsmonitor_path_type fsmonitor_classify_path_gitdir_relative(
	const char *relative_path);

/*
 * Classify an absolute pathname received from a filesystem event.
 */
enum fsmonitor_path_type fsmonitor_classify_path_absolute(
	struct fsmonitor_daemon_state *state,
	const char *path);

/*
 * Prepend the this batch of path(s) onto the list of batches associated
 * with the current token.  This makes the batch visible to worker threads.
 *
 * The caller no longer owns the batch and must not free it.
 *
 * Wake up the client threads waiting on these cookies.
 */
void fsmonitor_publish(struct fsmonitor_daemon_state *state,
		       struct fsmonitor_batch *batch,
		       const struct string_list *cookie_names);

/*
 * If the platform-specific layer loses sync with the filesystem,
 * it should call this to invalidate cached data and abort waiting
 * threads.
 */
void fsmonitor_force_resync(struct fsmonitor_daemon_state *state);

#endif /* HAVE_FSMONITOR_DAEMON_BACKEND */
#endif /* FSMONITOR_DAEMON_H */
