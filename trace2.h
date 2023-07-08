#ifndef TRACE2_H
#define TRACE2_H

/**
 * The Trace2 API can be used to print debug, performance, and telemetry
 * information to stderr or a file.  The Trace2 feature is inactive unless
 * explicitly enabled by enabling one or more Trace2 Targets.
 *
 * The Trace2 API is intended to replace the existing (Trace1)
 * printf-style tracing provided by the existing `GIT_TRACE` and
 * `GIT_TRACE_PERFORMANCE` facilities.  During initial implementation,
 * Trace2 and Trace1 may operate in parallel.
 *
 * The Trace2 API defines a set of high-level messages with known fields,
 * such as (`start`: `argv`) and (`exit`: {`exit-code`, `elapsed-time`}).
 *
 * Trace2 instrumentation throughout the Git code base sends Trace2
 * messages to the enabled Trace2 Targets.  Targets transform these
 * messages content into purpose-specific formats and write events to
 * their data streams.  In this manner, the Trace2 API can drive
 * many different types of analysis.
 *
 * Targets are defined using a VTable allowing easy extension to other
 * formats in the future.  This might be used to define a binary format,
 * for example.
 *
 * Trace2 is controlled using `trace2.*` config values in the system and
 * global config files and `GIT_TRACE2*` environment variables.  Trace2 does
 * not read from repo local or worktree config files or respect `-c`
 * command line config settings.
 *
 * For more info about: trace2 targets, conventions for public functions and
 * macros, trace2 target formats and examples on trace2 API usage refer to
 * Documentation/technical/api-trace2.txt
 *
 */

struct child_process;
struct repository;
struct json_writer;

/*
 * The public TRACE2 routines are grouped into the following groups:
 *
 * [] trace2_initialize -- initialization.
 * [] trace2_cmd_*      -- emit command/control messages.
 * [] trace2_child*     -- emit child start/stop messages.
 * [] trace2_exec*      -- emit exec start/stop messages.
 * [] trace2_thread*    -- emit thread start/stop messages.
 * [] trace2_def*       -- emit definition/parameter mesasges.
 * [] trace2_region*    -- emit region nesting messages.
 * [] trace2_data*      -- emit region/thread/repo data messages.
 * [] trace2_printf*    -- legacy trace[1] messages.
 * [] trace2_timer*     -- stopwatch timers (messages are deferred).
 * [] trace2_counter*   -- global counters (messages are deferred).
 */

/*
 * Initialize the TRACE2 clock and do nothing else, in particular
 * no mallocs, no system inspection, and no environment inspection.
 *
 * This should be called at the very top of main() to capture the
 * process start time.  This is intended to reduce chicken-n-egg
 * bootstrap pressure.
 *
 * It is safe to call this more than once.  This allows capturing
 * absolute startup costs on Windows which uses a little trickery
 * to do setup work before common-main.c:main() is called.
 *
 * The main trace2_initialize_fl() may be called a little later
 * after more infrastructure is established.
 */
void trace2_initialize_clock(void);

/*
 * Initialize TRACE2 tracing facility if any of the builtin TRACE2
 * targets are enabled in the system config or the environment.
 * This emits a 'version' message containing the version of git
 * and the Trace2 protocol.
 *
 * This function should be called from `main()` as early as possible in
 * the life of the process after essential process initialization.
 *
 * Cleanup/Termination is handled automatically by a registered
 * atexit() routine.
 */
void trace2_initialize_fl(const char *file, int line);

#define trace2_initialize() trace2_initialize_fl(__FILE__, __LINE__)

/*
 * Return 1 if trace2 is enabled (at least one target is active).
 */
int trace2_is_enabled(void);

/*
 * Emit a 'start' event with the original (unmodified) argv.
 */
void trace2_cmd_start_fl(const char *file, int line, const char **argv);

#define trace2_cmd_start(argv) trace2_cmd_start_fl(__FILE__, __LINE__, (argv))

/*
 * Emit an 'exit' event.
 */
void trace2_cmd_exit_fl(const char *file, int line, int code);

#define trace2_cmd_exit(code) (trace2_cmd_exit_fl(__FILE__, __LINE__, (code)))

/*
 * Emit an 'error' event.
 *
 * Write an error message to the TRACE2 targets.
 */
void trace2_cmd_error_va_fl(const char *file, int line, const char *fmt,
			    va_list ap);

#define trace2_cmd_error_va(fmt, ap) \
	trace2_cmd_error_va_fl(__FILE__, __LINE__, (fmt), (ap))

/*
 * Emit a 'pathname' event with the canonical pathname of the current process
 * This gives post-processors a simple field to identify the command without
 * having to parse the argv.  For example, to distinguish invocations from
 * installed versus debug executables.
 */
void trace2_cmd_path_fl(const char *file, int line, const char *pathname);

#define trace2_cmd_path(p) trace2_cmd_path_fl(__FILE__, __LINE__, (p))

/*
 * Emit an 'ancestry' event with the process name of the current process's
 * parent process.
 * This gives post-processors a way to determine what invoked the command and
 * learn more about usage patterns.
 */
void trace2_cmd_ancestry_fl(const char *file, int line, const char **parent_names);

#define trace2_cmd_ancestry(v) trace2_cmd_ancestry_fl(__FILE__, __LINE__, (v))

/*
 * Emit a 'cmd_name' event with the canonical name of the command.
 * This gives post-processors a simple field to identify the command
 * without having to parse the argv.
 */
void trace2_cmd_name_fl(const char *file, int line, const char *name);

#define trace2_cmd_name(v) trace2_cmd_name_fl(__FILE__, __LINE__, (v))

/*
 * Emit a 'cmd_mode' event to further describe the command being run.
 * For example, "checkout" can checkout a single file or can checkout a
 * different branch.  This gives post-processors a simple field to compare
 * equivalent commands without having to parse the argv.
 */
void trace2_cmd_mode_fl(const char *file, int line, const char *mode);

#define trace2_cmd_mode(sv) trace2_cmd_mode_fl(__FILE__, __LINE__, (sv))

/*
 * Emits an "alias" message containing the alias used and the argument
 * expansion.
 */
void trace2_cmd_alias_fl(const char *file, int line, const char *alias,
			 const char **argv);

#define trace2_cmd_alias(alias, argv) \
	trace2_cmd_alias_fl(__FILE__, __LINE__, (alias), (argv))

/*
 * Emit one or more 'def_param' events for "important" configuration
 * settings.
 *
 * Use the TR2_SYSENV_CFG_PARAM setting to register a comma-separated
 * list of patterns configured important.  For example:
 *     git config --system trace2.configParams 'core.*,remote.*.url'
 * or:
 *     GIT_TRACE2_CONFIG_PARAMS=core.*,remote.*.url"
 *
 * Note: this routine does a read-only iteration on the config data
 * (using read_early_config()), so it must not be called until enough
 * of the process environment has been established.  This includes the
 * location of the git and worktree directories, expansion of any "-c"
 * and "-C" command line options, and etc.
 */
void trace2_cmd_list_config_fl(const char *file, int line);

#define trace2_cmd_list_config() trace2_cmd_list_config_fl(__FILE__, __LINE__)

/*
 * Emit one or more 'def_param' events for "important" environment variables.
 *
 * Use the TR2_SYSENV_ENV_VARS setting to register a comma-separated list of
 * environment variables considered important.  For example:
 *     git config --system trace2.envVars 'GIT_HTTP_USER_AGENT,GIT_CONFIG'
 * or:
 *     GIT_TRACE2_ENV_VARS="GIT_HTTP_USER_AGENT,GIT_CONFIG"
 */
void trace2_cmd_list_env_vars_fl(const char *file, int line);

#define trace2_cmd_list_env_vars() trace2_cmd_list_env_vars_fl(__FILE__, __LINE__)

/*
 * Emit a "def_param" event for the given config key/value pair IF
 * we consider the key to be "important".
 *
 * Use this for new/updated config settings created/updated after
 * trace2_cmd_list_config() is called.
 */
void trace2_cmd_set_config_fl(const char *file, int line, const char *key,
			      const char *value);

#define trace2_cmd_set_config(k, v) \
	trace2_cmd_set_config_fl(__FILE__, __LINE__, (k), (v))

/**
 * Emits a "child_start" message containing the "child-id",
 * "child-argv", and "child-classification".
 *
 * Before calling optionally set "cmd->trace2_child_class" to a string
 * describing the type of the child process.  For example, "editor" or
 * "pager".
 *
 * This function assigns a unique "child-id" to `cmd->trace2_child_id`.
 * This field is used later during the "child_exit" message to associate
 * it with the "child_start" message.
 *
 * This function should be called before spawning the child process.
 */
void trace2_child_start_fl(const char *file, int line,
			   struct child_process *cmd);

#define trace2_child_start(cmd) trace2_child_start_fl(__FILE__, __LINE__, (cmd))

/**
 * Emits a "child_exit" message containing the "child-id",
 * the child's elapsed time and exit-code.
 *
 * The reported elapsed time includes the process creation overhead and
 * time spend waiting for it to exit, so it may be slightly longer than
 * the time reported by the child itself.
 *
 * This function should be called after reaping the child process.
 */
void trace2_child_exit_fl(const char *file, int line, struct child_process *cmd,
			  int child_exit_code);

#define trace2_child_exit(cmd, code) \
	trace2_child_exit_fl(__FILE__, __LINE__, (cmd), (code))

/**
 * Emits a "child_ready" message containing the "child-id" and a flag
 * indicating whether the child was considered "ready" when we
 * released it.
 *
 * This function should be called after starting a daemon process in
 * the background (and after giving it sufficient time to boot
 * up) to indicate that we no longer control or own it.
 *
 * The "ready" argument should contain one of { "ready", "timeout",
 * "error" } to indicate the state of the running daemon when we
 * released it.
 *
 * If the daemon process fails to start or it exits or is terminated
 * while we are still waiting for it, the caller should emit a
 * regular "child_exit" to report the normal process exit information.
 *
 */
void trace2_child_ready_fl(const char *file, int line,
			   struct child_process *cmd,
			   const char *ready);

#define trace2_child_ready(cmd, ready) \
	trace2_child_ready_fl(__FILE__, __LINE__, (cmd), (ready))

/**
 * Emit an 'exec' event prior to calling one of exec(), execv(),
 * execvp(), and etc.  On Unix-derived systems, this will be the
 * last event emitted for the current process, unless the exec
 * fails.  On Windows, exec() behaves like 'child_start' and a
 * waitpid(), so additional events may be emitted.
 *
 * Returns a unique "exec-id".  This value is used later
 * if the exec() fails and a "exec-result" message is necessary.
 */
int trace2_exec_fl(const char *file, int line, const char *exe,
		   const char **argv);

#define trace2_exec(exe, argv) trace2_exec_fl(__FILE__, __LINE__, (exe), (argv))

/**
 * Emit an 'exec_result' when possible.  On Unix-derived systems,
 * this should be called after exec() returns (which only happens
 * when there is an error starting the new process).  On Windows,
 * this should be called after the waitpid().
 *
 * The "exec_id" should be the value returned from trace2_exec().
 */
void trace2_exec_result_fl(const char *file, int line, int exec_id, int code);

#define trace2_exec_result(id, code) \
	trace2_exec_result_fl(__FILE__, __LINE__, (id), (code))

/*
 * Emit a 'thread_start' event.  This must be called from inside the
 * thread-proc to allow the thread to create its own thread-local
 * storage.
 *
 * The thread base name should be descriptive, like "preload_index" or
 * taken from the thread-proc function.  A unique thread name will be
 * created from the given base name and the thread id automatically.
 */
void trace2_thread_start_fl(const char *file, int line,
			    const char *thread_base_name);

#define trace2_thread_start(thread_base_name) \
	trace2_thread_start_fl(__FILE__, __LINE__, (thread_base_name))

/*
 * Emit a 'thread_exit' event.  This must be called from inside the
 * thread-proc so that the thread can access and clean up its
 * thread-local storage.
 */
void trace2_thread_exit_fl(const char *file, int line);

#define trace2_thread_exit() trace2_thread_exit_fl(__FILE__, __LINE__)

struct key_value_info;
/*
 * Emits a "def_param" message containing a key/value pair.
 *
 * This message is intended to report some global aspect of the current
 * command, such as a configuration setting or command line switch that
 * significantly affects program performance or behavior, such as
 * `core.abbrev`, `status.showUntrackedFiles`, or `--no-ahead-behind`.
 */
void trace2_def_param_fl(const char *file, int line, const char *param,
			 const char *value, const struct key_value_info *kvi);

#define trace2_def_param(param, value) \
	trace2_def_param_fl(__FILE__, __LINE__, (param), (value))

/*
 * Tell trace2 about a newly instantiated repo object and assign
 * a trace2-repo-id to be used in subsequent activity events.
 *
 * Emits a 'worktree' event for this repo instance.
 *
 * Region and data messages may refer to this repo-id.
 *
 * The main/top-level repository will have repo-id value 1 (aka "r1").
 *
 * The repo-id field is in anticipation of future in-proc submodule
 * repositories.
 */
void trace2_def_repo_fl(const char *file, int line, struct repository *repo);

#define trace2_def_repo(repo) trace2_def_repo_fl(__FILE__, __LINE__, repo)

/**
 * Emit a 'region_enter' event for <category>.<label> with optional
 * repo-id and printf message.
 *
 * This function pushes a new region nesting stack level on the current
 * thread and starts a clock for the new stack frame.
 *
 * The `category` field is an arbitrary category name used to classify
 * regions by feature area, such as "status" or "index".  At this time
 * it is only just printed along with the rest of the message.  It may
 * be used in the future to filter messages.
 *
 * The `label` field is an arbitrary label used to describe the activity
 * being started, such as "read_recursive" or "do_read_index".
 *
 * The `repo` field, if set, will be used to get the "repo-id", so that
 * recursive operations can be attributed to the correct repository.
 */
void trace2_region_enter_fl(const char *file, int line, const char *category,
			    const char *label, const struct repository *repo, ...);

#define trace2_region_enter(category, label, repo) \
	trace2_region_enter_fl(__FILE__, __LINE__, (category), (label), (repo))

void trace2_region_enter_printf_va_fl(const char *file, int line,
				      const char *category, const char *label,
				      const struct repository *repo,
				      const char *fmt, va_list ap);

#define trace2_region_enter_printf_va(category, label, repo, fmt, ap)    \
	trace2_region_enter_printf_va_fl(__FILE__, __LINE__, (category), \
					 (label), (repo), (fmt), (ap))

void trace2_region_enter_printf_fl(const char *file, int line,
				   const char *category, const char *label,
				   const struct repository *repo,
				   const char *fmt, ...);

#define trace2_region_enter_printf(category, label, repo, ...)                 \
	trace2_region_enter_printf_fl(__FILE__, __LINE__, (category), (label), \
				      (repo), __VA_ARGS__)

/**
 * Emit a 'region_leave' event for <category>.<label> with optional
 * repo-id and printf message.
 *
 * Leave current nesting level and report the elapsed time spent
 * in this nesting level.
 *
 * The `category`, `label`, and `repo` fields are the same as
 * trace2_region_enter_fl. The `category` and `label` do not
 * need to match the corresponding "region_enter" message,
 * but it makes the data stream easier to understand.
 */
void trace2_region_leave_fl(const char *file, int line, const char *category,
			    const char *label, const struct repository *repo, ...);

#define trace2_region_leave(category, label, repo) \
	trace2_region_leave_fl(__FILE__, __LINE__, (category), (label), (repo))

void trace2_region_leave_printf_va_fl(const char *file, int line,
				      const char *category, const char *label,
				      const struct repository *repo,
				      const char *fmt, va_list ap);

#define trace2_region_leave_printf_va(category, label, repo, fmt, ap)    \
	trace2_region_leave_printf_va_fl(__FILE__, __LINE__, (category), \
					 (label), (repo), (fmt), (ap))

void trace2_region_leave_printf_fl(const char *file, int line,
				   const char *category, const char *label,
				   const struct repository *repo,
				   const char *fmt, ...);

#define trace2_region_leave_printf(category, label, repo, ...)                 \
	trace2_region_leave_printf_fl(__FILE__, __LINE__, (category), (label), \
				      (repo), __VA_ARGS__)

/**
 * Emit a key-value pair 'data' event of the form <category>.<key> = <value>.
 * This event implicitly contains information about thread, nesting region,
 * and optional repo-id.
 * This could be used to print the number of files in a directory during
 * a multi-threaded recursive tree walk.
 *
 * On event-based TRACE2 targets, this generates a 'data' event suitable
 * for post-processing.  On printf-based TRACE2 targets, this is converted
 * into a fixed-format printf message.
 */
void trace2_data_string_fl(const char *file, int line, const char *category,
			   const struct repository *repo, const char *key,
			   const char *value);

#define trace2_data_string(category, repo, key, value)                       \
	trace2_data_string_fl(__FILE__, __LINE__, (category), (repo), (key), \
			      (value))

void trace2_data_intmax_fl(const char *file, int line, const char *category,
			   const struct repository *repo, const char *key,
			   intmax_t value);

#define trace2_data_intmax(category, repo, key, value)                       \
	trace2_data_intmax_fl(__FILE__, __LINE__, (category), (repo), (key), \
			      (value))

void trace2_data_json_fl(const char *file, int line, const char *category,
			 const struct repository *repo, const char *key,
			 const struct json_writer *jw);

#define trace2_data_json(category, repo, key, value)                       \
	trace2_data_json_fl(__FILE__, __LINE__, (category), (repo), (key), \
			    (value))

/*
 * Emit a 'printf' event.
 *
 * Write an arbitrary formatted message to the TRACE2 targets.  These
 * text messages should be considered as human-readable strings without
 * any formatting guidelines.  Post-processors may choose to ignore
 * them.
 */
void trace2_printf_va_fl(const char *file, int line, const char *fmt,
			 va_list ap);

#define trace2_printf_va(fmt, ap) \
	trace2_printf_va_fl(__FILE__, __LINE__, (fmt), (ap))

void trace2_printf_fl(const char *file, int line, const char *fmt, ...);

#define trace2_printf(...) trace2_printf_fl(__FILE__, __LINE__, __VA_ARGS__)

/*
 * Define the set of stopwatch timers.
 *
 * We can add more at any time, but they must be defined at compile
 * time (to avoid the need to dynamically allocate and synchronize
 * them between different threads).
 *
 * These must start at 0 and be contiguous (because we use them
 * elsewhere as array indexes).
 *
 * Any values added to this enum must also be added to the
 * `tr2_timer_metadata[]` in `trace2/tr2_tmr.c`.
 */
enum trace2_timer_id {
	/*
	 * Define two timers for testing.  See `t/helper/test-trace2.c`.
	 * These can be used for ad hoc testing, but should not be used
	 * for permanent analysis code.
	 */
	TRACE2_TIMER_ID_TEST1 = 0, /* emits summary event only */
	TRACE2_TIMER_ID_TEST2,     /* emits summary and thread events */

	/* Add additional timer definitions before here. */
	TRACE2_NUMBER_OF_TIMERS
};

/*
 * Start/Stop the indicated stopwatch timer in the current thread.
 *
 * The time spent by the current thread between the _start and _stop
 * calls will be added to the thread's partial sum for this timer.
 *
 * Timer events are emitted at thread and program exit.
 *
 * Note: Since the stopwatch API routines do not generate individual
 * events, they do not take (file, line) arguments.  Similarly, the
 * category and timer name values are defined at compile-time in the
 * timer definitions array, so they are not needed here in the API.
 */
void trace2_timer_start(enum trace2_timer_id tid);
void trace2_timer_stop(enum trace2_timer_id tid);

/*
 * Define the set of global counters.
 *
 * We can add more at any time, but they must be defined at compile
 * time (to avoid the need to dynamically allocate and synchronize
 * them between different threads).
 *
 * These must start at 0 and be contiguous (because we use them
 * elsewhere as array indexes).
 *
 * Any values added to this enum be also be added to the
 * `tr2_counter_metadata[]` in `trace2/tr2_tr2_ctr.c`.
 */
enum trace2_counter_id {
	/*
	 * Define two counters for testing.  See `t/helper/test-trace2.c`.
	 * These can be used for ad hoc testing, but should not be used
	 * for permanent analysis code.
	 */
	TRACE2_COUNTER_ID_TEST1 = 0, /* emits summary event only */
	TRACE2_COUNTER_ID_TEST2,     /* emits summary and thread events */

	/* Add additional counter definitions before here. */
	TRACE2_NUMBER_OF_COUNTERS
};

/*
 * Increase the named global counter by value.
 *
 * Note that this adds `value` to the current thread's partial sum for
 * this counter (without locking) and that the complete sum is not
 * available until all threads have exited, so it does not return the
 * new value of the counter.
 */
void trace2_counter_add(enum trace2_counter_id cid, uint64_t value);

/*
 * Optional platform-specific code to dump information about the
 * current and any parent process(es).  This is intended to allow
 * post-processors to know who spawned this git instance and anything
 * else that the platform may be able to tell us about the current process.
 */

enum trace2_process_info_reason {
	TRACE2_PROCESS_INFO_STARTUP,
	TRACE2_PROCESS_INFO_EXIT,
};

void trace2_collect_process_info(enum trace2_process_info_reason reason);

const char *trace2_session_id(void);

#endif /* TRACE2_H */
