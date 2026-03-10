#ifndef HOOK_H
#define HOOK_H
#include "strvec.h"
#include "run-command.h"
#include "string-list.h"
#include "strmap.h"

struct repository;

/**
 * Represents a hook command to be run.
 * Hooks can be:
 * 1. "traditional" (found in the hooks directory)
 * 2. "configured" (defined in Git's configuration via hook.<name>.event).
 * The 'kind' field determines which part of the union 'u' is valid.
 */
struct hook {
	enum {
		HOOK_TRADITIONAL,
		HOOK_CONFIGURED,
	} kind;
	union {
		struct {
			const char *path;
		} traditional;
		struct {
			const char *friendly_name;
			const char *command;
		} configured;
	} u;

	/**
	 * Opaque data pointer used to keep internal state across callback calls.
	 *
	 * It can be accessed directly via the third hook callback arg:
	 * struct ... *state = pp_task_cb;
	 *
	 * The caller is responsible for managing the memory for this data by
	 * providing alloc/free callbacks to `run_hooks_opt`.
	 *
	 * Only useful when using `run_hooks_opt.feed_pipe`, otherwise ignore it.
	 */
	void *feed_pipe_cb_data;
};

typedef void (*cb_data_free_fn)(void *data);
typedef void *(*cb_data_alloc_fn)(void *init_ctx);

struct run_hooks_opt
{
	/* Environment vars to be set for each hook */
	struct strvec env;

	/* Args to be passed to each hook */
	struct strvec args;

	/* Emit an error if the hook is missing */
	unsigned int error_if_missing:1;

	/**
	 *  Number of processes to parallelize across.
	 *
	 * If > 1, output will be buffered and de-interleaved (ungroup=0).
	 * If == 1, output will be real-time (ungroup=1).
	 */
	unsigned int jobs;

	/**
	 * An optional initial working directory for the hook,
	 * translates to "struct child_process"'s "dir" member.
	 */
	const char *dir;

	/**
	 * A pointer which if provided will be set to 1 or 0 depending
	 * on if a hook was started, regardless of whether or not that
	 * was successful. I.e. if the underlying start_command() was
	 * successful this will be set to 1.
	 *
	 * Used for avoiding TOCTOU races in code that would otherwise
	 * call hook_exist() after a "maybe hook run" to see if a hook
	 * was invoked.
	 */
	int *invoked_hook;

	/**
	 * Send the hook's stdout to stderr.
	 *
	 * This is the default behavior for all hooks except pre-push,
	 * which has separate stdout and stderr streams for backwards
	 * compatibility reasons.
	 */
	unsigned int stdout_to_stderr:1;

	/**
	 * Path to file which should be piped to stdin for each hook.
	 */
	const char *path_to_stdin;

	/**
	 * Callback used to incrementally feed a child hook stdin pipe.
	 *
	 * Useful especially if a hook consumes large quantities of data
	 * (e.g. a list of all refs in a client push), so feeding it via
	 * in-memory strings or slurping to/from files is inefficient.
	 * While the callback allows piecemeal writing, it can also be
	 * used for smaller inputs, where it gets called only once.
	 *
	 * Add hook callback initalization context to `feed_pipe_ctx`.
	 * Add hook callback internal state to `feed_pipe_cb_data`.
	 *
	 */
	feed_pipe_fn feed_pipe;

	/**
	 * Opaque data pointer used to pass context to `feed_pipe_fn`.
	 *
	 * It can be accessed via the second callback arg 'pp_cb':
	 * ((struct hook_cb_data *) pp_cb)->hook_cb->options->feed_pipe_ctx;
	 *
	 * The caller is responsible for managing the memory for this data.
	 * Only useful when using `run_hooks_opt.feed_pipe`, otherwise ignore it.
	 */
	void *feed_pipe_ctx;

	/**
	 * Some hooks need to create a fresh `feed_pipe_cb_data` internal state,
	 * so they can keep track of progress without affecting one another.
	 *
	 * If provided, this function will be called to alloc & initialize the
	 * `feed_pipe_cb_data` for each hook.
	 *
	 * The `feed_pipe_ctx` pointer can be used to pass initialization data.
	 */
	cb_data_alloc_fn feed_pipe_cb_data_alloc;

	/**
	 * Called to free the memory initialized by `feed_pipe_cb_data_alloc`.
	 *
	 * Must always be provided when `feed_pipe_cb_data_alloc` is provided.
	 */
	cb_data_free_fn feed_pipe_cb_data_free;
};

#define RUN_HOOKS_OPT_INIT { \
	.env = STRVEC_INIT, \
	.args = STRVEC_INIT, \
	.stdout_to_stderr = 1, \
	.jobs = 1, \
}

struct hook_cb_data {
	/* rc reflects the cumulative failure state */
	int rc;
	const char *hook_name;

	/**
	 * A list of hook commands/paths to run for the 'hook_name' event.
	 *
	 * The 'string' member of each item holds the path (for traditional hooks)
	 * or the unique friendly-name for hooks specified in configs.
	 * The 'util' member of each item points to the corresponding struct hook.
	 */
	struct string_list *hook_command_list;

	/* Iterator/cursor for the above list, pointing to the next hook to run. */
	size_t hook_to_run_index;

	struct run_hooks_opt *options;
};

/**
 * Provides a list of hook commands to run for the 'hookname' event.
 *
 * This function consolidates hooks from two sources:
 * 1. The config-based hooks (not yet implemented).
 * 2. The "traditional" hook found in the repository hooks directory
 *    (e.g., .git/hooks/pre-commit).
 *
 * The list is ordered by execution priority.
 *
 * The caller is responsible for freeing the memory of the returned list
 * using string_list_clear() and free().
 */
struct string_list *list_hooks(struct repository *r, const char *hookname,
			       struct run_hooks_opt *options);

/**
 * Frees the memory allocated for the hook list, including the `struct hook`
 * items and their internal state.
 */
void hook_list_clear(struct string_list *hooks, cb_data_free_fn cb_data_free);

/**
 * Frees the hook configuration cache stored in `struct repository`.
 * Called by repo_clear().
 */
void hook_cache_clear(struct strmap *cache);

/**
 * Returns the path to the hook file, or NULL if the hook is missing
 * or disabled. Note that this points to static storage that will be
 * overwritten by further calls to find_hook and run_hook_*.
 */
const char *find_hook(struct repository *r, const char *name);

/**
 * A boolean version of find_hook()
 */
int hook_exists(struct repository *r, const char *hookname);

/**
 * Takes a `hook_name`, resolves it to a path with find_hook(), and
 * runs the hook for you with the options specified in "struct
 * run_hooks opt". Will free memory associated with the "struct run_hooks_opt".
 *
 * Returns the status code of the run hook, or a negative value on
 * error().
 */
int run_hooks_opt(struct repository *r, const char *hook_name,
		  struct run_hooks_opt *options);

/**
 * A wrapper for run_hooks_opt() which provides a dummy "struct
 * run_hooks_opt" initialized with "RUN_HOOKS_OPT_INIT".
 */
int run_hooks(struct repository *r, const char *hook_name);

/**
 * Like run_hooks(), a wrapper for run_hooks_opt().
 *
 * In addition to the wrapping behavior provided by run_hooks(), this
 * wrapper takes a list of strings terminated by a NULL
 * argument. These things will be used as positional arguments to the
 * hook. This function behaves like the old run_hook_le() API.
 */
LAST_ARG_MUST_BE_NULL
int run_hooks_l(struct repository *r, const char *hook_name, ...);
#endif
