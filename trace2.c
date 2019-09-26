#include "cache.h"
#include "config.h"
#include "json-writer.h"
#include "quote.h"
#include "run-command.h"
#include "sigchain.h"
#include "thread-utils.h"
#include "version.h"
#include "trace2/tr2_cfg.h"
#include "trace2/tr2_cmd_name.h"
#include "trace2/tr2_dst.h"
#include "trace2/tr2_sid.h"
#include "trace2/tr2_sysenv.h"
#include "trace2/tr2_tgt.h"
#include "trace2/tr2_tls.h"

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
 * the TR2 and TLS machinery.
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

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_atexit)
			tgt_j->pfn_atexit(us_elapsed_absolute,
					  tr2main_exit_code);

	tr2_tgt_disable_builtins();

	tr2tls_release();
	tr2_sid_release();
	tr2_cmd_name_release();
	tr2_cfg_free_patterns();
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

int trace2_cmd_exit_fl(const char *file, int line, int code)
{
	struct tr2_tgt *tgt_j;
	int j;
	uint64_t us_now;
	uint64_t us_elapsed_absolute;

	code &= 0xff;

	if (!trace2_enabled)
		return code;

	trace2_collect_process_info(TRACE2_PROCESS_INFO_EXIT);

	tr2main_exit_code = code;

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	for_each_wanted_builtin (j, tgt_j)
		if (tgt_j->pfn_exit_fl)
			tgt_j->pfn_exit_fl(file, line, us_elapsed_absolute,
					   code);

	return code;
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

void trace2_thread_start_fl(const char *file, int line, const char *thread_name)
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
					      thread_name);
		return;
	}

	us_now = getnanotime() / 1000;
	us_elapsed_absolute = tr2tls_absolute_elapsed(us_now);

	tr2tls_create_self(thread_name, us_now);

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

#ifndef HAVE_VARIADIC_MACROS
void trace2_region_enter_printf(const char *category, const char *label,
				const struct repository *repo, const char *fmt,
				...)
{
	va_list ap;

	va_start(ap, fmt);
	trace2_region_enter_printf_va_fl(NULL, 0, category, label, repo, fmt,
					 ap);
	va_end(ap);
}
#endif

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

#ifndef HAVE_VARIADIC_MACROS
void trace2_region_leave_printf(const char *category, const char *label,
				const struct repository *repo, const char *fmt,
				...)
{
	va_list ap;

	va_start(ap, fmt);
	trace2_region_leave_printf_va_fl(NULL, 0, category, label, repo, fmt,
					 ap);
	va_end(ap);
}
#endif

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

#ifndef HAVE_VARIADIC_MACROS
void trace2_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	trace2_printf_va_fl(NULL, 0, fmt, ap);
	va_end(ap);
}
#endif
