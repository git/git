#ifndef HOOK_H
#define HOOK_H
#include "strvec.h"
#include "run-command.h"

struct repository;

struct run_hooks_opt
{
	/* Environment vars to be set for each hook */
	struct strvec env;

	/* Args to be passed to each hook */
	struct strvec args;

	/* Emit an error if the hook is missing */
	unsigned int error_if_missing:1;

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
	 * Allow hooks to set run_processes_parallel() 'ungroup' behavior.
	 */
	unsigned int ungroup:1;

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
	 * Opaque data pointer used to keep internal state across callback calls.
	 *
	 * It can be accessed directly via the third callback arg 'pp_task_cb':
	 * struct ... *state = pp_task_cb;
	 *
	 * The caller is responsible for managing the memory for this data.
	 * Only useful when using `run_hooks_opt.feed_pipe`, otherwise ignore it.
	 */
	void *feed_pipe_cb_data;

	/*
	 * Populate this to capture output and prevent it from being printed to
	 * stderr. This will be passed directly through to
	 * run_command:run_parallel_processes(). See t/helper/test-run-command.c
	 * for an example.
	 */
	consume_output_fn consume_output;
};

#define RUN_HOOKS_OPT_INIT { \
	.env = STRVEC_INIT, \
	.args = STRVEC_INIT, \
}

struct hook_cb_data {
	/* rc reflects the cumulative failure state */
	int rc;
	const char *hook_name;
	const char *hook_path;
	struct run_hooks_opt *options;
};

/*
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
