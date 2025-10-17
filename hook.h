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
	 * Path to file which should be piped to stdin for each hook.
	 */
	const char *path_to_stdin;

	/**
	 * Callback to ask for more content to pipe to each hook stdin.
	 *
	 * If a hook needs to consume large quantities of data (e.g. a
	 * list of all refs received in a client push), feeding data via
	 * in-memory strings or slurping to/from files via path_to_stdin
	 * is inefficient, so this callback allows for piecemeal writes.
	 *
	 * Add initalization context to hook.feed_pipe_ctx.
	 *
	 * The caller owns hook.feed_pipe_ctx and has to release any
	 * resources after hooks finish execution.
	 */
	feed_pipe_fn feed_pipe;
	void *feed_pipe_ctx;

	/**
	 * Use this to keep internal state for your feed_pipe_fn callback.
	 * Only useful when using run_hooks_opt.feed_pipe, otherwise ignore it.
	 */
	void *feed_pipe_cb_data;
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
