#ifndef RUN_COMMAND_H
#define RUN_COMMAND_H

#include "thread-utils.h"

#include "argv-array.h"

/**
 * The run-command API offers a versatile tool to run sub-processes with
 * redirected input and output as well as with a modified environment
 * and an alternate current directory.
 *
 * A similar API offers the capability to run a function asynchronously,
 * which is primarily used to capture the output that the function
 * produces in the caller in order to process it.
 */


/**
 * This describes the arguments, redirections, and environment of a
 * command to run in a sub-process.
 *
 * The caller:
 *
 * 1. allocates and clears (using child_process_init() or
 *    CHILD_PROCESS_INIT) a struct child_process variable;
 * 2. initializes the members;
 * 3. calls start_command();
 * 4. processes the data;
 * 5. closes file descriptors (if necessary; see below);
 * 6. calls finish_command().
 *
 * Special forms of redirection are available by setting these members
 * to 1:
 *
 *  .no_stdin, .no_stdout, .no_stderr: The respective channel is
 *		redirected to /dev/null.
 *
 *	.stdout_to_stderr: stdout of the child is redirected to its
 *		stderr. This happens after stderr is itself redirected.
 *		So stdout will follow stderr to wherever it is
 *		redirected.
 */
struct child_process {

	/**
	 * The .argv member is set up as an array of string pointers (NULL
	 * terminated), of which .argv[0] is the program name to run (usually
	 * without a path). If the command to run is a git command, set argv[0] to
	 * the command name without the 'git-' prefix and set .git_cmd = 1.
	 *
	 * Note that the ownership of the memory pointed to by .argv stays with the
	 * caller, but it should survive until `finish_command` completes. If the
	 * .argv member is NULL, `start_command` will point it at the .args
	 * `argv_array` (so you may use one or the other, but you must use exactly
	 * one). The memory in .args will be cleaned up automatically during
	 * `finish_command` (or during `start_command` when it is unsuccessful).
	 *
	 */
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
	 * - Specify 0 for no redirections. No new file descriptor is allocated.
	 * (child inherits stdin, stdout, stderr from parent).
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

	/**
	 * To specify a new initial working directory for the sub-process,
	 * specify it in the .dir member.
	 */
	const char *dir;

	/**
	 * To modify the environment of the sub-process, specify an array of
	 * string pointers (NULL terminated) in .env:
	 *
	 * - If the string is of the form "VAR=value", i.e. it contains '='
	 *   the variable is added to the child process's environment.
	 *
	 * - If the string does not contain '=', it names an environment
	 *   variable that will be removed from the child process's environment.
	 *
	 * If the .env member is NULL, `start_command` will point it at the
	 * .env_array `argv_array` (so you may use one or the other, but not both).
	 * The memory in .env_array will be cleaned up automatically during
	 * `finish_command` (or during `start_command` when it is unsuccessful).
	 */
	const char *const *env;

	unsigned no_stdin:1;
	unsigned no_stdout:1;
	unsigned no_stderr:1;
	unsigned git_cmd:1; /* if this is to be git sub-command */

	/**
	 * If the program cannot be found, the functions return -1 and set
	 * errno to ENOENT. Normally, an error message is printed, but if
	 * .silent_exec_failure is set to 1, no message is printed for this
	 * special error condition.
	 */
	unsigned silent_exec_failure:1;

	unsigned stdout_to_stderr:1;
	unsigned use_shell:1;
	unsigned clean_on_exit:1;
	unsigned wait_after_clean:1;
	void (*clean_on_exit_handler)(struct child_process *process);
	void *clean_on_exit_handler_cbdata;
};

#define CHILD_PROCESS_INIT { NULL, ARGV_ARRAY_INIT, ARGV_ARRAY_INIT }

/**
 * The functions: child_process_init, start_command, finish_command,
 * run_command, run_command_v_opt, run_command_v_opt_cd_env, child_process_clear
 * do the following:
 *
 * - If a system call failed, errno is set and -1 is returned. A diagnostic
 *   is printed.
 *
 * - If the program was not found, then -1 is returned and errno is set to
 *   ENOENT; a diagnostic is printed only if .silent_exec_failure is 0.
 *
 * - Otherwise, the program is run. If it terminates regularly, its exit
 *   code is returned. No diagnostic is printed, even if the exit code is
 *   non-zero.
 *
 * - If the program terminated due to a signal, then the return value is the
 *   signal number + 128, ie. the same value that a POSIX shell's $? would
 *   report.  A diagnostic is printed.
 *
 */

/**
 * Initialize a struct child_process variable.
 */
void child_process_init(struct child_process *);

/**
 * Release the memory associated with the struct child_process.
 * Most users of the run-command API don't need to call this
 * function explicitly because `start_command` invokes it on
 * failure and `finish_command` calls it automatically already.
 */
void child_process_clear(struct child_process *);

int is_executable(const char *name);

/**
 * Start a sub-process. Takes a pointer to a `struct child_process`
 * that specifies the details and returns pipe FDs (if requested).
 * See below for details.
 */
int start_command(struct child_process *);

/**
 * Wait for the completion of a sub-process that was started with
 * start_command().
 */
int finish_command(struct child_process *);

int finish_command_in_signal(struct child_process *);

/**
 * A convenience function that encapsulates a sequence of
 * start_command() followed by finish_command(). Takes a pointer
 * to a `struct child_process` that specifies the details.
 */
int run_command(struct child_process *);

/*
 * Returns the path to the hook file, or NULL if the hook is missing
 * or disabled. Note that this points to static storage that will be
 * overwritten by further calls to find_hook and run_hook_*.
 */
const char *find_hook(const char *name);

/**
 * Run a hook.
 * The first argument is a pathname to an index file, or NULL
 * if the hook uses the default index file or no index is needed.
 * The second argument is the name of the hook.
 * The further arguments correspond to the hook arguments.
 * The last argument has to be NULL to terminate the arguments list.
 * If the hook does not exist or is not executable, the return
 * value will be zero.
 * If it is executable, the hook will be executed and the exit
 * status of the hook is returned.
 * On execution, .stdout_to_stderr and .no_stdin will be set.
 */
LAST_ARG_MUST_BE_NULL
int run_hook_le(const char *const *env, const char *name, ...);
int run_hook_ve(const char *const *env, const char *name, va_list args);

#define RUN_COMMAND_NO_STDIN 1
#define RUN_GIT_CMD	     2	/*If this is to be git sub-command */
#define RUN_COMMAND_STDOUT_TO_STDERR 4
#define RUN_SILENT_EXEC_FAILURE 8
#define RUN_USING_SHELL 16
#define RUN_CLEAN_ON_EXIT 32

/**
 * Convenience functions that encapsulate a sequence of
 * start_command() followed by finish_command(). The argument argv
 * specifies the program and its arguments. The argument opt is zero
 * or more of the flags `RUN_COMMAND_NO_STDIN`, `RUN_GIT_CMD`,
 * `RUN_COMMAND_STDOUT_TO_STDERR`, or `RUN_SILENT_EXEC_FAILURE`
 * that correspond to the members .no_stdin, .git_cmd,
 * .stdout_to_stderr, .silent_exec_failure of `struct child_process`.
 * The argument dir corresponds the member .dir. The argument env
 * corresponds to the member .env.
 */
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
 *
 * The caller:
 *
 * 1. allocates and clears (memset(&asy, 0, sizeof(asy));) a
 *    struct async variable;
 * 2. initializes .proc and .data;
 * 3. calls start_async();
 * 4. processes communicates with proc through .in and .out;
 * 5. closes .in and .out;
 * 6. calls finish_async().
 *
 * There are serious restrictions on what the asynchronous function can do
 * because this facility is implemented by a thread in the same address
 * space on most platforms (when pthreads is available), but by a pipe to
 * a forked process otherwise:
 *
 * - It cannot change the program's state (global variables, environment,
 *   etc.) in a way that the caller notices; in other words, .in and .out
 *   are the only communication channels to the caller.
 *
 * - It must not change the program's state that the caller of the
 *   facility also uses.
 *
 */
struct async {

	/**
	 * The function pointer in .proc has the following signature:
	 *
	 *	int proc(int in, int out, void *data);
	 *
	 * - in, out specifies a set of file descriptors to which the function
	 *  must read/write the data that it needs/produces.  The function
	 *  *must* close these descriptors before it returns.  A descriptor
	 *  may be -1 if the caller did not configure a descriptor for that
	 *  direction.
	 *
	 * - data is the value that the caller has specified in the .data member
	 *  of struct async.
	 *
	 * - The return value of the function is 0 on success and non-zero
	 *  on failure. If the function indicates failure, finish_async() will
	 *  report failure as well.
	 *
	 */
	int (*proc)(int in, int out, void *data);

	void *data;

	/**
	 * The members .in, .out are used to provide a set of fd's for
	 * communication between the caller and the callee as follows:
	 *
	 * - Specify 0 to have no file descriptor passed.  The callee will
	 *   receive -1 in the corresponding argument.
	 *
	 * - Specify < 0 to have a pipe allocated; start_async() replaces
	 *   with the pipe FD in the following way:
	 *
	 * 	.in: Returns the writable pipe end into which the caller
	 * 	writes; the readable end of the pipe becomes the function's
	 * 	in argument.
	 *
	 * 	.out: Returns the readable pipe end from which the caller
	 * 	reads; the writable end of the pipe becomes the function's
	 * 	out argument.
	 *
	 *   The caller of start_async() must close the returned FDs after it
	 *   has completed reading from/writing from them.
	 *
	 * - Specify a file descriptor > 0 to be used by the function:
	 *
	 * 	.in: The FD must be readable; it becomes the function's in.
	 * 	.out: The FD must be writable; it becomes the function's out.
	 *
	 *   The specified FD is closed by start_async(), even if it fails to
	 *   run the function.
	 */
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

/**
 * Run a function asynchronously. Takes a pointer to a `struct
 * async` that specifies the details and returns a set of pipe FDs
 * for communication with the function. See below for details.
 */
int start_async(struct async *async);

/**
 * Wait for the completion of an asynchronous function that was
 * started with start_async().
 */
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
