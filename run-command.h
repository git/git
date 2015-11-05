#ifndef RUN_COMMAND_H
#define RUN_COMMAND_H

#ifndef NO_PTHREADS
#include <pthread.h>
#endif

#include "argv-array.h"

struct child_process {
	const char **argv;
	struct argv_array args;
	struct argv_array env_array;
	pid_t pid;
	/*
	 * Using .in, .out, .err:
	 * - Specify 0 for no redirections (child inherits stdin, stdout,
	 *   stderr from parent).
	 * - Specify -1 to have a pipe allocated as follows:
	 *     .in: returns the writable pipe end; parent writes to it,
	 *          the readable pipe end becomes child's stdin
	 *     .out, .err: returns the readable pipe end; parent reads from
	 *          it, the writable pipe end becomes child's stdout/stderr
	 *   The caller of start_command() must close the returned FDs
	 *   after it has completed reading from/writing to it!
	 * - Specify > 0 to set a channel to a particular FD as follows:
	 *     .in: a readable FD, becomes child's stdin
	 *     .out: a writable FD, becomes child's stdout/stderr
	 *     .err: a writable FD, becomes child's stderr
	 *   The specified FD is closed by start_command(), even in case
	 *   of errors!
	 */
	int in;
	int out;
	int err;
	const char *dir;
	const char *const *env;
	unsigned no_stdin:1;
	unsigned no_stdout:1;
	unsigned no_stderr:1;
	unsigned git_cmd:1; /* if this is to be git sub-command */
	unsigned silent_exec_failure:1;
	unsigned stdout_to_stderr:1;
	unsigned use_shell:1;
	unsigned clean_on_exit:1;
};

#define CHILD_PROCESS_INIT { NULL, ARGV_ARRAY_INIT, ARGV_ARRAY_INIT }
void child_process_init(struct child_process *);
void child_process_clear(struct child_process *);

int start_command(struct child_process *);
int finish_command(struct child_process *);
int finish_command_in_signal(struct child_process *);
int run_command(struct child_process *);

/*
 * Returns the path to the hook file, or NULL if the hook is missing
 * or disabled. Note that this points to static storage that will be
 * overwritten by further calls to find_hook and run_hook_*.
 */
extern const char *find_hook(const char *name);
LAST_ARG_MUST_BE_NULL
extern int run_hook_le(const char *const *env, const char *name, ...);
extern int run_hook_ve(const char *const *env, const char *name, va_list args);

#define RUN_COMMAND_NO_STDIN 1
#define RUN_GIT_CMD	     2	/*If this is to be git sub-command */
#define RUN_COMMAND_STDOUT_TO_STDERR 4
#define RUN_SILENT_EXEC_FAILURE 8
#define RUN_USING_SHELL 16
#define RUN_CLEAN_ON_EXIT 32
int run_command_v_opt(const char **argv, int opt);

/*
 * env (the environment) is to be formatted like environ: "VAR=VALUE".
 * To unset an environment variable use just "VAR".
 */
int run_command_v_opt_cd_env(const char **argv, int opt, const char *dir, const char *const *env);

/**
 * Execute the given command, capturing its stdout in the given strbuf.
 * Returns -1 if starting the command fails or reading fails, and otherwise
 * returns the exit code of the command. The output collected in the
 * buffer is kept even if the command returns a non-zero exit. The hint field
 * gives a starting size for the strbuf allocation.
 *
 * The fields of "cmd" should be set up as they would for a normal run_command
 * invocation. But note that there is no need to set cmd->out; the function
 * sets it up for the caller.
 */
int capture_command(struct child_process *cmd, struct strbuf *buf, size_t hint);

/*
 * The purpose of the following functions is to feed a pipe by running
 * a function asynchronously and providing output that the caller reads.
 *
 * It is expected that no synchronization and mutual exclusion between
 * the caller and the feed function is necessary so that the function
 * can run in a thread without interfering with the caller.
 */
struct async {
	/*
	 * proc reads from in; closes it before return
	 * proc writes to out; closes it before return
	 * returns 0 on success, non-zero on failure
	 */
	int (*proc)(int in, int out, void *data);
	void *data;
	int in;		/* caller writes here and closes it */
	int out;	/* caller reads from here and closes it */
#ifdef NO_PTHREADS
	pid_t pid;
#else
	pthread_t tid;
	int proc_in;
	int proc_out;
#endif
};

int start_async(struct async *async);
int finish_async(struct async *async);

#endif
