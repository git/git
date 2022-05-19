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

struct fsmonitor_daemon_backend_data; /* opaque platform-specific data */

struct fsmonitor_daemon_state {
	pthread_t listener_thread;
	pthread_mutex_t main_lock;

	struct strbuf path_worktree_watch;
	struct strbuf path_butdir_watch;
	int nr_paths_watching;

	struct fsmonitor_token_data *current_token_data;

	struct strbuf path_cookie_prefix;
	pthread_cond_t cookies_cond;
	int cookie_seq;
	struct hashmap cookies;

	int error_code;
	struct fsmonitor_daemon_backend_data *backend_data;

	struct ipc_server_data *ipc_server_data;
};

/*
 * Pathname classifications.
 *
 * The daemon classifies the pathnames that it receives from file
 * system notification events into the following categories and uses
 * that to decide whether clients are told about them.  (And to watch
 * for file system synchronization events.)
 *
 * The daemon only collects and reports on the set of modified paths
 * within the working directory (proper).
 *
 * The client should only care about paths within the working
 * directory proper (inside the working directory and not ".but" nor
 * inside of ".but/").  That is, the client has read the index and is
 * asking for a list of any paths in the working directory that have
 * been modified since the last token.  The client does not care about
 * file system changes within the ".but/" directory (such as new loose
 * objects or packfiles).  So the client will only receive paths that
 * are classified as IS_WORKDIR_PATH.
 *
 * Note that ".but" is usually a directory and is therefore inside
 * the cone of the FS watch that we have on the working directory root,
 * so we will also get FS events for disk activity on and within ".but/"
 * that we need to respond to or filter from the client.
 *
 * But Git also allows ".but" to be a *file* that points to a BUTDIR
 * outside of the working directory.  When this happens, we need to
 * create FS watches on both the working directory root *and* on the
 * (external) BUTDIR root.  (The latter is required because we put
 * cookie files inside it and use them to sync with the FS event
 * stream.)
 *
 * Note that in the context of this discussion, I'm using "BUTDIR"
 * to only mean an external BUTDIR referenced by a ".but" file.
 *
 * The platform FS event backends will receive watch-specific
 * relative paths (except for those OS's that always emit absolute
 * paths).  We use the following enum and routines to classify each
 * path so that we know how to handle it.  There is a slight asymmetry
 * here because ".but/" is inside the working directory and the
 * (external) BUTDIR is not, and therefore how we handle events may
 * vary slightly, so I have different enums for "IS...DOT_BUT..." and
 * "IS...BUTDIR...".
 *
 * The daemon uses the IS_DOT_BUT and IS_BUTDIR internally to mean the
 * exact ".but" file/directory or BUTDIR directory.  If the daemon
 * receives a delete event for either of these paths, it will
 * automatically shutdown, for example.
 *
 * Note that the daemon DOES NOT explicitly watch nor special case the
 * index.  The daemon does not read the index nor have any internal
 * index-relative state, so there are no "IS...INDEX..." enum values.
 */
enum fsmonitor_path_type {
	IS_WORKDIR_PATH = 0,

	IS_DOT_BUT,
	IS_INSIDE_DOT_BUT,
	IS_INSIDE_DOT_BUT_WITH_COOKIE_PREFIX,

	IS_BUTDIR,
	IS_INSIDE_BUTDIR,
	IS_INSIDE_BUTDIR_WITH_COOKIE_PREFIX,

	IS_OUTSIDE_CONE,
};

/*
 * Classify a pathname relative to the root of the working directory.
 */
enum fsmonitor_path_type fsmonitor_classify_path_workdir_relative(
	const char *relative_path);

/*
 * Classify a pathname relative to a <butdir> that is external to the
 * worktree directory.
 */
enum fsmonitor_path_type fsmonitor_classify_path_butdir_relative(
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
