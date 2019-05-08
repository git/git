#ifndef RUN_COMMAND_H
#define RUN_COMMAND_H

#include "thread-utils.h"

#include "argv-array.h"

struct child_process {
	const char **argv;
	struct argv_array args;
	struct argv_array env_array;
	pid_t pid;

	int trace2_child_id;
	uint64_t trace2_child_us_start;
	const char *trace2_child_class;
	const char *trace2_hook_name;

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
	unsigned wait_after_clean:1;
	void (*clean_on_exit_handler)(struct child_process *process);
	void *clean_on_exit_handler_cbdata;
};

#define CHILD_PROCESS_INIT { NULL, ARGV_ARRAY_INIT, ARGV_ARRAY_INIT }
void child_process_init(struct child_process *);
void child_process_clear(struct child_process *);
int is_executable(const char *name);

int start_command(struct child_process *);
int finish_command(struct child_process *);
int finish_command_in_signal(struct child_process *);
int run_command(struct child_process *);

/*
 * Returns the path to the hook file, or NULL if the hook is missing
 * or disabled. Note that this points to static storage that will be
 * overwritten by further calls to find_hook and run_hook_*.
 */
const char *find_hook(const char *name);
LAST_ARG_MUST_BE_NULL
int run_hook_le(const char *const *env, const char *name, ...);
int run_hook_ve(const char *const *env, const char *name, va_list args);

#define RUN_COMMAND_NO_STDIN 1
#define RUN_GIT_CMD	     2	/*If this is to be git sub-command */
#define RUN_COMMAND_STDOUT_TO_STDERR 4
#define RUN_SILENT_EXEC_FAILURE 8
#define RUN_USING_SHELL 16
#define RUN_CLEAN_ON_EXIT 32
int run_command_v_opt(const char **argv, int opt);
int run_command_v_opt_tr2(const char **argv, int opt, const char *tr2_class);
/*
 * env (the environment) is to be formatted like environ: "VAR=VALUE".
 * To unset an environment variable use just "VAR".
 */
int run_command_v_opt_cd_env(const char **argv, int opt, const char *dir, const char *const *env);
int run_command_v_opt_cd_env_tr2(const char **argv, int opt, const char *dir,
				 const char *const *env, const char *tr2_class);

/**
 * Execute the given command, sending "in" to its stdin, and capturing its
 * stdout and stderr in the "out" and "err" strbufs. Any of the three may
 * be NULL to skip processing.
 *
 * Returns -1 if starting the command fails or reading fails, and otherwise
 * returns the exit code of the command. Any output collected in the
 * buffers is kept even if the command returns a non-zero exit. The hint fields
 * gives starting sizes for the strbuf allocations.
 *
 * The fields of "cmd" should be set up as they would for a normal run_command
 * invocation. But note that there is no need to set the in, out, or err
 * fields; pipe_command handles that automatically.
 */
int pipe_command(struct child_process *cmd,
		 const char *in, size_t in_len,
		 struct strbuf *out, size_t out_hint,
		 struct strbuf *err, size_t err_hint);

/**
 * Convenience wrapper around pipe_command for the common case
 * of capturing only stdout.
 */
static inline int capture_command(struct child_process *cmd,
				  struct strbuf *out,
				  size_t hint)
{
	return pipe_command(cmd, NULL, 0, out, hint, NULL, 0);
}

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
	int isolate_sigpipe;
};

int start_async(struct async *async);
int finish_async(struct async *async);
int in_async(void);
int async_with_fork(void);
void check_pipe(int err);

/**
 * This callback should initialize the child process and preload the
 * error channel if desired. The preloading of is useful if you want to
 * have a message printed directly before the output of the child process.
 * pp_cb is the callback cookie as passed to run_processes_parallel.
 * You can store a child process specific callback cookie in pp_task_cb.
 *
 * Even after returning 0 to indicate that there are no more processes,
 * this function will be called again until there are no more running
 * child processes.
 *
 * Return 1 if the next child is ready to run.
 * Return 0 if there are currently no more tasks to be processed.
 * To send a signal to other child processes for abortion,
 * return the negative signal number.
 */
typedef int (*get_next_task_fn)(struct child_process *cp,
				struct strbuf *out,
				void *pp_cb,
				void **pp_task_cb);

/**
 * This callback is called whenever there are problems starting
 * a new process.
 *
 * You must not write to stdout or stderr in this function. Add your
 * message to the strbuf out instead, which will be printed without
 * messing up the output of the other parallel processes.
 *
 * pp_cb is the callback cookie as passed into run_processes_parallel,
 * pp_task_cb is the callback cookie as passed into get_next_task_fn.
 *
 * Return 0 to continue the parallel processing. To abort return non zero.
 * To send a signal to other child processes for abortion, return
 * the negative signal number.
 */
typedef int (*start_failure_fn)(struct strbuf *out,
				void *pp_cb,
				void *pp_task_cb);

/**
 * This callback is called on every child process that finished processing.
 *
 * You must not write to stdout or stderr in this function. Add your
 * message to the strbuf out instead, which will be printed without
 * messing up the output of the other parallel processes.
 *
 * pp_cb is the callback cookie as passed into run_processes_parallel,
 * pp_task_cb is the callback cookie as passed into get_next_task_fn.
 *
 * Return 0 to continue the parallel processing.  To abort return non zero.
 * To send a signal to other child processes for abortion, return
 * the negative signal number.
 */
typedef int (*task_finished_fn)(int result,
				struct strbuf *out,
				void *pp_cb,
				void *pp_task_cb);

/**
 * Runs up to n processes at the same time. Whenever a process can be
 * started, the callback get_next_task_fn is called to obtain the data
 * required to start another child process.
 *
 * The children started via this function run in parallel. Their output
 * (both stdout and stderr) is routed to stderr in a manner that output
 * from different tasks does not interleave.
 *
 * start_failure_fn and task_finished_fn can be NULL to omit any
 * special handling.
 */
int run_processes_parallel(int n,
			   get_next_task_fn,
			   start_failure_fn,
			   task_finished_fn,
			   void *pp_cb);
int run_processes_parallel_tr2(int n, get_next_task_fn, start_failure_fn,
			       task_finished_fn, void *pp_cb,
			       const char *tr2_category, const char *tr2_label);

#endif
