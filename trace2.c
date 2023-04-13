#include "git-compat-util.h"
#include "config.h"
#include "json-writer.h"
#include "quote.h"
#include "run-command.h"
#include "sigchain.h"
#include "thread-utils.h"
#include "version.h"
#include "trace.h"
#include "trace2.h"
#include "trace2/tr2_cfg.h"
#include "trace2/tr2_cmd_name.h"
#include "trace2/tr2_ctr.h"
#include "trace2/tr2_dst.h"
#include "trace2/tr2_sid.h"
#include "trace2/tr2_sysenv.h"
#include "trace2/tr2_tgt.h"
#include "trace2/tr2_tls.h"
#include "trace2/tr2_tmr.h"

static int trace2_enabled;

static int tr2_next_child_id; /* modify under lock */
static int tr2_next_exec_id; /* modify under lock */
static int tr2_next_repo_id = 1; /* modify under lock. zero is reserved */

/*
 * A table of the builtin TRACE2 targets.  Each of these may be independently
 * enabled or disabled.  Each TRACE2 API method will try to write an event to
 * *each* of the enabled targets.
 */
/* clang-format off */
static struct tr2_tgt *tr2_tgt_builtins[] =
{
	&tr2_tgt_normal,
	&tr2_tgt_perf,
	&tr2_tgt_event,
	NULL
};
/* clang-format on */

/* clang-format off */
#define for_each_builtin(j, tgt_j)			\
	for (j = 0, tgt_j = tr2_tgt_builtins[j];	\
	     tgt_j;					\
	     j++, tgt_j = tr2_tgt_builtins[j])
/* clang-format on */

/* clang-format off */
#define for_each_wanted_builtin(j, tgt_j)            \
	for_each_builtin(j, tgt_j)                   \
		if (tr2_dst_trace_want(tgt_j->pdst))
/* clang-format on */

/*
 * Force (rather than lazily) initialize any of the requested
 * builtin TRACE2 targets at startup (and before we've seen an
 * actual TRACE2 event call) so we can see if we need to setup
 * private data structures and thread-local storage.
 *
 * Return the number of builtin targets enabled.
 */
static int tr2_tgt_want_builtins(void)
{
	struct tr2_tgt *tgt_j;
	int j;
	int sum = 0;

	for_each_builtin (j, tgt_j)
		if (tgt_j->pfn_init())
			sum++;

	return sum;
}

/*
 * Properly terminate each builtin target.  Give each target
 * a chance to write a summary event and/or flush if necessary
 * and then close the fd.
 */
static void tr2_tgt_disable_builtins(void)
{
	struct tr2_tgt *tgt_j;
	int j;

	for_each_builtin (j, tgt_j)
		tgt_j->pfn_term();
}

/*
 * The signature of this function must match the pfn_timer
 * method in the targets.  (Think of this is an apply operation
 * across the set of active targets.)
 */
static void tr2_tgt_emit_a_timer(const struct tr2_timer_metadata *meta,
				 const struct tr2_timer *timer,
				 int is_final_data)
{
	struct tr2_tgt *tgt_j;
	int j;

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_timer)
			tgt_j->pfn_timer(meta, timer, is_final_data);
}

/*
 * The signature of this function must match the pfn_counter
 * method in the targets.
 */
static void tr2_tgt_emit_a_counter(const struct tr2_counter_metadata *meta,
				   const struct tr2_counter *counter,
				   int is_final_data)
{
	struct tr2_tgt *tgt_j;
	int j;

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_counter)
			tgt_j->pfn_counter(meta, counter, is_final_data);
}

static int tr2main_exit_code;

/*
 * Our atexit routine should run after everything has finished.
 *
 * Note that events generated here might not actually appear if
 * we are writing to fd 1 or 2 and our atexit routine runs after
 * the pager's atexit routine (since it closes them to shutdown
 * the pipes).
 */
static void tr2main_atexit_handler(void)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	/*
	 * Clear any unbalanced regions so that our atexit message
	 * does not appear nested.  This improves the appearance of
	 * the trace output if someone calls die(), for example.
	 */
	tr2tls_pop_unwind_self();

	/*
	 * Some timers want per-thread details.  If the main thread
	 * used one of those timers, emit the details now (before
	 * we emit the aggregate timer values).
	 *
	 * Likewise for counters.
	 */
	tr2_emit_per_thread_timers(tr2_tgt_emit_a_timer);
	tr2_emit_per_thread_counters(tr2_tgt_emit_a_counter);

	/*
	 * Add stopwatch timer and counter data for the main thread to
	 * the final totals.  And then emit the final values.
	 *
	 * Technically, we shouldn't need to hold the lock to update
	 * and output the final_timer_block and final_counter_block
	 * (since all other threads should be dead by now), but it
	 * doesn't hurt anything.
	 */
	tr2tls_lock();
	tr2_update_final_timers();
	tr2_update_final_counters();
	tr2_emit_final_timers(tr2_tgt_emit_a_timer);
	tr2_emit_final_counters(tr2_tgt_emit_a_counter);
	tr2tls_unlock();

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_atexit)
			tgt_j->pfn_atexit(us_elapsed_absolute,
					  tr2main_exit_code);

	tr2_tgt_disable_builtins();

	tr2tls_release();
	tr2_sid_release();
	tr2_cmd_name_release();
	tr2_cfg_free_patterns();
	tr2_cfg_free_env_vars();
	tr2_sysenv_release();

	trace2_enabled = 0;
}

static void tr2main_signal_handler(int signo)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_signal)
			tgt_j->pfn_signal(us_elapsed_absolute, signo);

	sigchain_pop(signo);
	raise(signo);
}

void trace2_initialize_clock(void)
{
	tr2tls_start_process_clock();
}

void trace2_initialize_fl(const char *file, int line)
{
	struct tr2_tgt *tgt_j;
	int j;

	if (trace2_enabled)
		return;

	tr2_sysenv_load();

	if (!tr2_tgt_want_builtins())
		return;
	trace2_enabled = 1;

	tr2_sid_get();

	atexit(tr2main_atexit_handler);
	sigchain_push(SIGPIPE, tr2main_signal_handler);
	tr2tls_init();

	/*
	 * Emit 'version' message on each active builtin target.
	 */
	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_version_fl)
			tgt_j->pfn_version_fl(file, line);
}

int trace2_is_enabled(void)
{
	return trace2_enabled;
}

void trace2_cmd_start_fl(const char *file, int line, const char **argv)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;

	if (!trace2_enabled)
		return;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_start_fl)
			tgt_j->pfn_start_fl(file, line, us_elapsed_absolute,
					    argv);
}

void trace2_cmd_exit_fl(const char *file, int line, int code)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;

	if (!trace2_enabled)
		return;

	trace_git_fsync_stats();
	trace2_collect_process_info(TRACE2_PROCESS_INFO_EXIT);

	tr2main_exit_code = code;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_exit_fl)
			tgt_j->pfn_exit_fl(file, line, us_elapsed_absolute,
					   code);
}

void trace2_cmd_error_va_fl(const char *file, int line, const char *fmt,
			    va_list ap)
{
	struct tr2_tgt *tgt_j;
	int j;

	if (!trace2_enabled)
		return;

	/*
	 * We expect each target function to treat 'ap' as constant
	 * and use va_copy (because an 'ap' can only be walked once).
	 */
	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_error_va_fl)
			tgt_j->pfn_error_va_fl(file, line, fmt, ap);
}

void trace2_cmd_path_fl(const char *file, int line, const char *pathname)
{
	struct tr2_tgt *tgt_j;
	int j;

	if (!trace2_enabled)
		return;

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_command_path_fl)
			tgt_j->pfn_command_path_fl(file, line, pathname);
}

void trace2_cmd_ancestry_fl(const char *file, int line, const char **parent_names)
{
	struct tr2_tgt *tgt_j;
	int j;

	if (!trace2_enabled)
		return;

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_command_ancestry_fl)
			tgt_j->pfn_command_ancestry_fl(file, line, parent_names);
}

void trace2_cmd_name_fl(const char *file, int line, const char *name)
{
	struct tr2_tgt *tgt_j;
	const char *hierarchy;
	int j;

	if (!trace2_enabled)
		return;

	tr2_cmd_name_append_hierarchy(name);
	hierarchy = tr2_cmd_name_get_hierarchy();

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_command_name_fl)
			tgt_j->pfn_command_name_fl(file, line, name, hierarchy);
}

void trace2_cmd_mode_fl(const char *file, int line, const char *mode)
{
	struct tr2_tgt *tgt_j;
	int j;

	if (!trace2_enabled)
		return;

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_command_mode_fl)
			tgt_j->pfn_command_mode_fl(file, line, mode);
}

void trace2_cmd_alias_fl(const char *file, int line, const char *alias,
			 const char **argv)
{
	struct tr2_tgt *tgt_j;
	int j;

	if (!trace2_enabled)
		return;

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_alias_fl)
			tgt_j->pfn_alias_fl(file, line, alias, argv);
}

void trace2_cmd_list_config_fl(const char *file, int line)
{
	if (!trace2_enabled)
		return;

	tr2_cfg_list_config_fl(file, line);
}

void trace2_cmd_list_env_vars_fl(const char *file, int line)
{
	if (!trace2_enabled)
		return;

	tr2_list_env_vars_fl(file, line);
}

void trace2_cmd_set_config_fl(const char *file, int line, const char *key,
			      const char *value)
{
	if (!trace2_enabled)
		return;

	tr2_cfg_set_fl(file, line, key, value);
}

void trace2_child_start_fl(const char *file, int line,
			   struct child_process *cmd)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;

	if (!trace2_enabled)
		return;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	cmd->trace2_child_id = tr2tls_locked_increment(&tr2_next_child_id);
	cmd->trace2_child_us_start = us_now;

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_child_start_fl)
			tgt_j->pfn_child_start_fl(file, line,
						  us_elapsed_absolute, cmd);
}

void trace2_child_exit_fl(const char *file, int line, struct child_process *cmd,
			  int child_exit_code)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;
	uint64_t us_elapsed_child;

	if (!trace2_enabled)
		return;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	if (cmd->trace2_child_us_start)
		us_elapsed_child = us_now - cmd->trace2_child_us_start;
	else
		us_elapsed_child = 0;

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_child_exit_fl)
			tgt_j->pfn_child_exit_fl(file, line,
						 us_elapsed_absolute,
						 cmd->trace2_child_id, cmd->pid,
						 child_exit_code,
						 us_elapsed_child);
}

void trace2_child_ready_fl(const char *file, int line,
			   struct child_process *cmd,
			   const char *ready)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;
	uint64_t us_elapsed_child;

	if (!trace2_enabled)
		return;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	if (cmd->trace2_child_us_start)
		us_elapsed_child = us_now - cmd->trace2_child_us_start;
	else
		us_elapsed_child = 0;

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_child_ready_fl)
			tgt_j->pfn_child_ready_fl(file, line,
						  us_elapsed_absolute,
						  cmd->trace2_child_id,
						  cmd->pid,
						  ready,
						  us_elapsed_child);
}

int trace2_exec_fl(const char *file, int line, const char *exe,
		   const char **argv)
{
	struct tr2_tgt *tgt_j;
	int j;
	int exec_id;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;

	if (!trace2_enabled)
		return -1;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	exec_id = tr2tls_locked_increment(&tr2_next_exec_id);

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_exec_fl)
			tgt_j->pfn_exec_fl(file, line, us_elapsed_absolute,
					   exec_id, exe, argv);

	return exec_id;
}

void trace2_exec_result_fl(const char *file, int line, int exec_id, int code)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;

	if (!trace2_enabled)
		return;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_exec_result_fl)
			tgt_j->pfn_exec_result_fl(
				file, line, us_elapsed_absolute, exec_id, code);
}

void trace2_thread_start_fl(const char *file, int line, const char *thread_base_name)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;

	if (!trace2_enabled)
		return;

	if (tr2tls_is_main_thread()) {
		/*
		 * We should only be called from the new thread's thread-proc,
		 * so this is technically a bug.  But in those cases where the
		 * main thread also runs the thread-proc function (or when we
		 * are built with threading disabled), we need to allow it.
		 *
		 * Convert this call to a region-enter so the nesting looks
		 * correct.
		 */
		trace2_region_enter_printf_fl(file, line, NULL, NULL, NULL,
					      "thread-proc on main: %s",
					      thread_base_name);
		return;
	}

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	tr2tls_create_self(thread_base_name, us_now);

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_thread_start_fl)
			tgt_j->pfn_thread_start_fl(file, line,
						   us_elapsed_absolute);
}

void trace2_thread_exit_fl(const char *file, int line)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;
	uint64_t us_elapsed_thread;

	if (!trace2_enabled)
		return;

	if (tr2tls_is_main_thread()) {
		/*
		 * We should only be called from the exiting thread's
		 * thread-proc, so this is technically a bug.  But in
		 * those cases where the main thread also runs the
		 * thread-proc function (or when we are built with
		 * threading disabled), we need to allow it.
		 *
		 * Convert this call to a region-leave so the nesting
		 * looks correct.
		 */
		trace2_region_leave_printf_fl(file, line, NULL, NULL, NULL,
					      "thread-proc on main");
		return;
	}

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	/*
	 * Clear any unbalanced regions and then get the relative time
	 * for the outer-most region (which we pushed when the thread
	 * started).  This gives us the run time of the thread.
	 */
	tr2tls_pop_unwind_self();
	us_elapsed_thread = tr2tls_region_elasped_self(us_now);

	/*
	 * Some timers want per-thread details.  If this thread used
	 * one of those timers, emit the details now.
	 *
	 * Likewise for counters.
	 */
	tr2_emit_per_thread_timers(tr2_tgt_emit_a_timer);
	tr2_emit_per_thread_counters(tr2_tgt_emit_a_counter);

	/*
	 * Add stopwatch timer and counter data from the current
	 * (non-main) thread to the final totals.  (We'll accumulate
	 * data for the main thread later during "atexit".)
	 */
	tr2tls_lock();
	tr2_update_final_timers();
	tr2_update_final_counters();
	tr2tls_unlock();

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_thread_exit_fl)
			tgt_j->pfn_thread_exit_fl(file, line,
						  us_elapsed_absolute,
						  us_elapsed_thread);

	tr2tls_unset_self();
}

void trace2_def_param_fl(const char *file, int line, const char *param,
			 const char *value)
{
	struct tr2_tgt *tgt_j;
	int j;

	if (!trace2_enabled)
		return;

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_param_fl)
			tgt_j->pfn_param_fl(file, line, param, value);
}

void trace2_def_repo_fl(const char *file, int line, struct repository *repo)
{
	struct tr2_tgt *tgt_j;
	int j;

	if (!trace2_enabled)
		return;

	if (repo->trace2_repo_id)
		return;

	repo->trace2_repo_id = tr2tls_locked_increment(&tr2_next_repo_id);

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_repo_fl)
			tgt_j->pfn_repo_fl(file, line, repo);
}

void trace2_region_enter_printf_va_fl(const char *file, int line,
				      const char *category, const char *label,
				      const struct repository *repo,
				      const char *fmt, va_list ap)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;

	if (!trace2_enabled)
		return;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	/*
	 * Print the region-enter message at the current nesting
	 * (indentation) level and then push a new level.
	 *
	 * We expect each target function to treat 'ap' as constant
	 * and use va_copy.
	 */
	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_region_enter_printf_va_fl)
			tgt_j->pfn_region_enter_printf_va_fl(
				file, line, us_elapsed_absolute, category,
				label, repo, fmt, ap);

	tr2tls_push_self(us_now);
}

void trace2_region_enter_fl(const char *file, int line, const char *category,
			    const char *label, const struct repository *repo, ...)
{
	va_list ap;
	va_start(ap, repo);
	trace2_region_enter_printf_va_fl(file, line, category, label, repo,
					 NULL, ap);
	va_end(ap);

}

void trace2_region_enter_printf_fl(const char *file, int line,
				   const char *category, const char *label,
				   const struct repository *repo,
				   const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	trace2_region_enter_printf_va_fl(file, line, category, label, repo, fmt,
					 ap);
	va_end(ap);
}

void trace2_region_leave_printf_va_fl(const char *file, int line,
				      const char *category, const char *label,
				      const struct repository *repo,
				      const char *fmt, va_list ap)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;
	uint64_t us_elapsed_region;

	if (!trace2_enabled)
		return;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	/*
	 * Get the elapsed time in the current region before we
	 * pop it off the stack.  Pop the stack.  And then print
	 * the perf message at the new (shallower) level so that
	 * it lines up with the corresponding push/enter.
	 */
	us_elapsed_region = tr2tls_region_elasped_self(us_now);

	tr2tls_pop_self();

	/*
	 * We expect each target function to treat 'ap' as constant
	 * and use va_copy.
	 */
	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_region_leave_printf_va_fl)
			tgt_j->pfn_region_leave_printf_va_fl(
				file, line, us_elapsed_absolute,
				us_elapsed_region, category, label, repo, fmt,
				ap);
}

void trace2_region_leave_fl(const char *file, int line, const char *category,
			    const char *label, const struct repository *repo, ...)
{
	va_list ap;
	va_start(ap, repo);
	trace2_region_leave_printf_va_fl(file, line, category, label, repo,
					 NULL, ap);
	va_end(ap);
}

void trace2_region_leave_printf_fl(const char *file, int line,
				   const char *category, const char *label,
				   const struct repository *repo,
				   const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	trace2_region_leave_printf_va_fl(file, line, category, label, repo, fmt,
					 ap);
	va_end(ap);
}

void trace2_data_string_fl(const char *file, int line, const char *category,
			   const struct repository *repo, const char *key,
			   const char *value)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;
	uint64_t us_elapsed_region;

	if (!trace2_enabled)
		return;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);
	us_elapsed_region = tr2tls_region_elasped_self(us_now);

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_data_fl)
			tgt_j->pfn_data_fl(file, line, us_elapsed_absolute,
					   us_elapsed_region, category, repo,
					   key, value);
}

void trace2_data_intmax_fl(const char *file, int line, const char *category,
			   const struct repository *repo, const char *key,
			   intmax_t value)
{
	struct strbuf buf_string = STRBUF_INIT;

	if (!trace2_enabled)
		return;

	strbuf_addf(&buf_string, "%" PRIdMAX, value);
	trace2_data_string_fl(file, line, category, repo, key, buf_string.buf);
	strbuf_release(&buf_string);
}

void trace2_data_json_fl(const char *file, int line, const char *category,
			 const struct repository *repo, const char *key,
			 const struct json_writer *value)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;
	uint64_t us_elapsed_region;

	if (!trace2_enabled)
		return;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);
	us_elapsed_region = tr2tls_region_elasped_self(us_now);

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_data_json_fl)
			tgt_j->pfn_data_json_fl(file, line, us_elapsed_absolute,
						us_elapsed_region, category,
						repo, key, value);
}

void trace2_printf_va_fl(const char *file, int line, const char *fmt,
			 va_list ap)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;

	if (!trace2_enabled)
		return;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	/*
	 * We expect each target function to treat 'ap' as constant
	 * and use va_copy.
	 */
	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_printf_va_fl)
			tgt_j->pfn_printf_va_fl(file, line, us_elapsed_absolute,
						fmt, ap);
}

void trace2_printf_fl(const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	trace2_printf_va_fl(file, line, fmt, ap);
	va_end(ap);
}

void trace2_timer_start(enum trace2_timer_id tid)
{
	if (!trace2_enabled)
		return;

	if (tid < 0 || tid >= TRACE2_NUMBER_OF_TIMERS)
		BUG("trace2_timer_start: invalid timer id: %d", tid);

	tr2_start_timer(tid);
}

void trace2_timer_stop(enum trace2_timer_id tid)
{
	if (!trace2_enabled)
		return;

	if (tid < 0 || tid >= TRACE2_NUMBER_OF_TIMERS)
		BUG("trace2_timer_stop: invalid timer id: %d", tid);

	tr2_stop_timer(tid);
}

void trace2_counter_add(enum trace2_counter_id cid, uint64_t value)
{
	if (!trace2_enabled)
		return;

	if (cid < 0 || cid >= TRACE2_NUMBER_OF_COUNTERS)
		BUG("trace2_counter_add: invalid counter id: %d", cid);

	tr2_counter_increment(cid, value);
}

const char *trace2_session_id(void)
{
	return tr2_sid_get();
}
