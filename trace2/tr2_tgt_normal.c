#include "git-compat-util.h"
#include "config.h"
#include "repository.h"
#include "run-command.h"
#include "quote.h"
#include "version.h"
#include "trace2/tr2_dst.h"
#include "trace2/tr2_sysenv.h"
#include "trace2/tr2_tbuf.h"
#include "trace2/tr2_tgt.h"
#include "trace2/tr2_tls.h"
#include "trace2/tr2_tmr.h"

static struct tr2_dst tr2dst_normal = {
	.sysenv_var = TR2_SYSENV_NORMAL,
};

/*
 * Use the TR2_SYSENV_NORMAL_BRIEF setting to omit the "<time> <file>:<line>"
 * fields from each line written to the builtin normal target.
 *
 * Unit tests may want to use this to help with testing.
 */
static int tr2env_normal_be_brief;

#define TR2FMT_NORMAL_FL_WIDTH (50)

static int fn_init(void)
{
	int want = tr2_dst_trace_want(&tr2dst_normal);
	int want_brief;
	const char *brief;

	if (!want)
		return want;

	brief = tr2_sysenv_get(TR2_SYSENV_NORMAL_BRIEF);
	if (brief && *brief &&
	    ((want_brief = git_parse_maybe_bool(brief)) != -1))
		tr2env_normal_be_brief = want_brief;

	return want;
}

static void fn_term(void)
{
	tr2_dst_trace_disable(&tr2dst_normal);
}

static void normal_fmt_prepare(const char *file, int line, struct strbuf *buf)
{
	strbuf_setlen(buf, 0);

	if (!tr2env_normal_be_brief) {
		struct tr2_tbuf tb_now;

		tr2_tbuf_local_time(&tb_now);
		strbuf_addstr(buf, tb_now.buf);
		strbuf_addch(buf, ' ');

		if (file && *file)
			strbuf_addf(buf, "%s:%d ", file, line);
		while (buf->len < TR2FMT_NORMAL_FL_WIDTH)
			strbuf_addch(buf, ' ');
	}
}

static void normal_io_write_fl(const char *file, int line,
			       const struct strbuf *buf_payload)
{
	struct strbuf buf_line = STRBUF_INIT;

	normal_fmt_prepare(file, line, &buf_line);
	strbuf_addbuf(&buf_line, buf_payload);
	tr2_dst_write_line(&tr2dst_normal, &buf_line);
	strbuf_release(&buf_line);
}

static void fn_version_fl(const char *file, int line)
{
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "version %s", git_version_string);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_start_fl(const char *file, int line,
			uint64_t us_elapsed_absolute, const char **argv)
{
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addstr(&buf_payload, "start ");
	sq_append_quote_argv_pretty(&buf_payload, argv);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_exit_fl(const char *file, int line, uint64_t us_elapsed_absolute,
		       int code)
{
	struct strbuf buf_payload = STRBUF_INIT;
	double elapsed = (double)us_elapsed_absolute / 1000000.0;

	strbuf_addf(&buf_payload, "exit elapsed:%.6f code:%d", elapsed, code);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_signal(uint64_t us_elapsed_absolute, int signo)
{
	struct strbuf buf_payload = STRBUF_INIT;
	double elapsed = (double)us_elapsed_absolute / 1000000.0;

	strbuf_addf(&buf_payload, "signal elapsed:%.6f code:%d", elapsed,
		    signo);
	normal_io_write_fl(__FILE__, __LINE__, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_atexit(uint64_t us_elapsed_absolute, int code)
{
	struct strbuf buf_payload = STRBUF_INIT;
	double elapsed = (double)us_elapsed_absolute / 1000000.0;

	strbuf_addf(&buf_payload, "atexit elapsed:%.6f code:%d", elapsed, code);
	normal_io_write_fl(__FILE__, __LINE__, &buf_payload);
	strbuf_release(&buf_payload);
}

static void maybe_append_string_va(struct strbuf *buf, const char *fmt,
				   va_list ap)
{
	if (fmt && *fmt) {
		va_list copy_ap;

		va_copy(copy_ap, ap);
		strbuf_vaddf(buf, fmt, copy_ap);
		va_end(copy_ap);
		return;
	}
}

static void fn_error_va_fl(const char *file, int line, const char *fmt,
			   va_list ap)
{
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addstr(&buf_payload, "error");
	if (fmt && *fmt) {
		strbuf_addch(&buf_payload, ' ');
		maybe_append_string_va(&buf_payload, fmt, ap);
	}
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_command_path_fl(const char *file, int line, const char *pathname)
{
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "cmd_path %s", pathname);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_command_ancestry_fl(const char *file, int line, const char **parent_names)
{
	const char *parent_name = NULL;
	struct strbuf buf_payload = STRBUF_INIT;

	/* cmd_ancestry parent <- grandparent <- great-grandparent */
	strbuf_addstr(&buf_payload, "cmd_ancestry ");
	while ((parent_name = *parent_names++)) {
		strbuf_addstr(&buf_payload, parent_name);
		/* if we'll write another one after this, add a delimiter */
		if (parent_names && *parent_names)
			strbuf_addstr(&buf_payload, " <- ");
	}

	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_command_name_fl(const char *file, int line, const char *name,
			       const char *hierarchy)
{
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "cmd_name %s", name);
	if (hierarchy && *hierarchy)
		strbuf_addf(&buf_payload, " (%s)", hierarchy);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_command_mode_fl(const char *file, int line, const char *mode)
{
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "cmd_mode %s", mode);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_alias_fl(const char *file, int line, const char *alias,
			const char **argv)
{
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "alias %s -> ", alias);
	sq_append_quote_argv_pretty(&buf_payload, argv);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_child_start_fl(const char *file, int line,
			      uint64_t us_elapsed_absolute,
			      const struct child_process *cmd)
{
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "child_start[%d]", cmd->trace2_child_id);

	if (cmd->dir) {
		strbuf_addstr(&buf_payload, " cd ");
		sq_quote_buf_pretty(&buf_payload, cmd->dir);
		strbuf_addstr(&buf_payload, ";");
	}

	/*
	 * TODO if (cmd->env) { Consider dumping changes to environment. }
	 * See trace_add_env() in run-command.c as used by original trace.c
	 */

	strbuf_addch(&buf_payload, ' ');
	if (cmd->git_cmd)
		strbuf_addstr(&buf_payload, "git ");
	sq_append_quote_argv_pretty(&buf_payload, cmd->args.v);

	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_child_exit_fl(const char *file, int line,
			     uint64_t us_elapsed_absolute, int cid, int pid,
			     int code, uint64_t us_elapsed_child)
{
	struct strbuf buf_payload = STRBUF_INIT;
	double elapsed = (double)us_elapsed_child / 1000000.0;

	strbuf_addf(&buf_payload, "child_exit[%d] pid:%d code:%d elapsed:%.6f",
		    cid, pid, code, elapsed);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_child_ready_fl(const char *file, int line,
			      uint64_t us_elapsed_absolute, int cid, int pid,
			      const char *ready, uint64_t us_elapsed_child)
{
	struct strbuf buf_payload = STRBUF_INIT;
	double elapsed = (double)us_elapsed_child / 1000000.0;

	strbuf_addf(&buf_payload, "child_ready[%d] pid:%d ready:%s elapsed:%.6f",
		    cid, pid, ready, elapsed);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_exec_fl(const char *file, int line, uint64_t us_elapsed_absolute,
		       int exec_id, const char *exe, const char **argv)
{
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "exec[%d] ", exec_id);
	if (exe) {
		strbuf_addstr(&buf_payload, exe);
		strbuf_addch(&buf_payload, ' ');
	}
	sq_append_quote_argv_pretty(&buf_payload, argv);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_exec_result_fl(const char *file, int line,
			      uint64_t us_elapsed_absolute, int exec_id,
			      int code)
{
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "exec_result[%d] code:%d", exec_id, code);
	if (code > 0)
		strbuf_addf(&buf_payload, " err:%s", strerror(code));
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_param_fl(const char *file, int line, const char *param,
			const char *value, const struct key_value_info *kvi)
{
	struct strbuf buf_payload = STRBUF_INIT;
	enum config_scope scope = kvi->scope;
	const char *scope_name = config_scope_name(scope);

	strbuf_addf(&buf_payload, "def_param scope:%s %s=%s", scope_name, param,
		    value);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_repo_fl(const char *file, int line,
		       const struct repository *repo)
{
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addstr(&buf_payload, "worktree ");
	sq_quote_buf_pretty(&buf_payload, repo->worktree);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_printf_va_fl(const char *file, int line,
			    uint64_t us_elapsed_absolute, const char *fmt,
			    va_list ap)
{
	struct strbuf buf_payload = STRBUF_INIT;

	maybe_append_string_va(&buf_payload, fmt, ap);
	normal_io_write_fl(file, line, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_timer(const struct tr2_timer_metadata *meta,
		     const struct tr2_timer *timer,
		     int is_final_data)
{
	const char *event_name = is_final_data ? "timer" : "th_timer";
	struct strbuf buf_payload = STRBUF_INIT;
	double t_total = NS_TO_SEC(timer->total_ns);
	double t_min = NS_TO_SEC(timer->min_ns);
	double t_max = NS_TO_SEC(timer->max_ns);

	strbuf_addf(&buf_payload, ("%s %s/%s"
				   " intervals:%"PRIu64
				   " total:%8.6f min:%8.6f max:%8.6f"),
		    event_name, meta->category, meta->name,
		    timer->interval_count,
		    t_total, t_min, t_max);

	normal_io_write_fl(__FILE__, __LINE__, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_counter(const struct tr2_counter_metadata *meta,
		       const struct tr2_counter *counter,
		       int is_final_data)
{
	const char *event_name = is_final_data ? "counter" : "th_counter";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "%s %s/%s value:%"PRIu64,
		    event_name, meta->category, meta->name,
		    counter->value);

	normal_io_write_fl(__FILE__, __LINE__, &buf_payload);
	strbuf_release(&buf_payload);
}

struct tr2_tgt tr2_tgt_normal = {
	.pdst = &tr2dst_normal,

	.pfn_init = fn_init,
	.pfn_term = fn_term,

	.pfn_version_fl = fn_version_fl,
	.pfn_start_fl = fn_start_fl,
	.pfn_exit_fl = fn_exit_fl,
	.pfn_signal = fn_signal,
	.pfn_atexit = fn_atexit,
	.pfn_error_va_fl = fn_error_va_fl,
	.pfn_command_path_fl = fn_command_path_fl,
	.pfn_command_ancestry_fl = fn_command_ancestry_fl,
	.pfn_command_name_fl = fn_command_name_fl,
	.pfn_command_mode_fl = fn_command_mode_fl,
	.pfn_alias_fl = fn_alias_fl,
	.pfn_child_start_fl = fn_child_start_fl,
	.pfn_child_exit_fl = fn_child_exit_fl,
	.pfn_child_ready_fl = fn_child_ready_fl,
	.pfn_thread_start_fl = NULL,
	.pfn_thread_exit_fl = NULL,
	.pfn_exec_fl = fn_exec_fl,
	.pfn_exec_result_fl = fn_exec_result_fl,
	.pfn_param_fl = fn_param_fl,
	.pfn_repo_fl = fn_repo_fl,
	.pfn_region_enter_printf_va_fl = NULL,
	.pfn_region_leave_printf_va_fl = NULL,
	.pfn_data_fl = NULL,
	.pfn_data_json_fl = NULL,
	.pfn_printf_va_fl = fn_printf_va_fl,
	.pfn_timer = fn_timer,
	.pfn_counter = fn_counter,
};
