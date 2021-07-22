#include "cache.h"
#include "config.h"
#include "run-command.h"
#include "quote.h"
#include "version.h"
#include "json-writer.h"
#include "trace2/tr2_dst.h"
#include "trace2/tr2_sid.h"
#include "trace2/tr2_sysenv.h"
#include "trace2/tr2_tbuf.h"
#include "trace2/tr2_tgt.h"
#include "trace2/tr2_tls.h"

static struct tr2_dst tr2dst_perf = { TR2_SYSENV_PERF, 0, 0, 0, 0 };

/*
 * Use TR2_SYSENV_PERF_BRIEF to omit the "<time> <file>:<line>"
 * fields from each line written to the builtin performance target.
 *
 * Unit tests may want to use this to help with testing.
 */
static int tr2env_perf_be_brief;

#define TR2FMT_PERF_FL_WIDTH (28)
#define TR2FMT_PERF_MAX_EVENT_NAME (12)
#define TR2FMT_PERF_REPO_WIDTH (3)
#define TR2FMT_PERF_CATEGORY_WIDTH (12)

#define TR2_INDENT (2)
#define TR2_INDENT_LENGTH(ctx) (((ctx)->nr_open_regions - 1) * TR2_INDENT)

static int fn_init(void)
{
	int want = tr2_dst_trace_want(&tr2dst_perf);
	int want_brief;
	const char *brief;

	if (!want)
		return want;

	brief = tr2_sysenv_get(TR2_SYSENV_PERF_BRIEF);
	if (brief && *brief &&
	    ((want_brief = git_parse_maybe_bool(brief)) != -1))
		tr2env_perf_be_brief = want_brief;

	return want;
}

static void fn_term(void)
{
	tr2_dst_trace_disable(&tr2dst_perf);
}

/*
 * Format trace line prefix in human-readable classic format for
 * the performance target:
 *     "[<time> [<file>:<line>] <bar>] <nr_parents> <bar>
 *         <thread_name> <bar> <event_name> <bar> [<repo>] <bar>
 *         [<elapsed_absolute>] [<elapsed_relative>] <bar>
 *         [<category>] <bar> [<dots>] "
 */
static void perf_fmt_prepare(const char *event_name,
			     struct tr2tls_thread_ctx *ctx, const char *file,
			     int line, const struct repository *repo,
			     uint64_t *p_us_elapsed_absolute,
			     uint64_t *p_us_elapsed_relative,
			     const char *category, struct strbuf *buf)
{
	int len;

	strbuf_setlen(buf, 0);

	if (!tr2env_perf_be_brief) {
		struct tr2_tbuf tb_now;
		size_t fl_end_col;

		tr2_tbuf_local_time(&tb_now);
		strbuf_addstr(buf, tb_now.buf);
		strbuf_addch(buf, ' ');

		fl_end_col = buf->len + TR2FMT_PERF_FL_WIDTH;

		if (file && *file) {
			struct strbuf buf_fl = STRBUF_INIT;

			strbuf_addf(&buf_fl, "%s:%d", file, line);

			if (buf_fl.len <= TR2FMT_PERF_FL_WIDTH)
				strbuf_addbuf(buf, &buf_fl);
			else {
				size_t avail = TR2FMT_PERF_FL_WIDTH - 3;
				strbuf_addstr(buf, "...");
				strbuf_add(buf,
					   &buf_fl.buf[buf_fl.len - avail],
					   avail);
			}

			strbuf_release(&buf_fl);
		}

		while (buf->len < fl_end_col)
			strbuf_addch(buf, ' ');

		strbuf_addstr(buf, " | ");
	}

	strbuf_addf(buf, "d%d | ", tr2_sid_depth());
	strbuf_addf(buf, "%-*s | %-*s | ", TR2_MAX_THREAD_NAME,
		    ctx->thread_name.buf, TR2FMT_PERF_MAX_EVENT_NAME,
		    event_name);

	len = buf->len + TR2FMT_PERF_REPO_WIDTH;
	if (repo)
		strbuf_addf(buf, "r%d ", repo->trace2_repo_id);
	while (buf->len < len)
		strbuf_addch(buf, ' ');
	strbuf_addstr(buf, " | ");

	if (p_us_elapsed_absolute)
		strbuf_addf(buf, "%9.6f | ",
			    ((double)(*p_us_elapsed_absolute)) / 1000000.0);
	else
		strbuf_addf(buf, "%9s | ", " ");

	if (p_us_elapsed_relative)
		strbuf_addf(buf, "%9.6f | ",
			    ((double)(*p_us_elapsed_relative)) / 1000000.0);
	else
		strbuf_addf(buf, "%9s | ", " ");

	strbuf_addf(buf, "%-*.*s | ", TR2FMT_PERF_CATEGORY_WIDTH,
		    TR2FMT_PERF_CATEGORY_WIDTH, (category ? category : ""));

	if (ctx->nr_open_regions > 0)
		strbuf_addchars(buf, '.', TR2_INDENT_LENGTH(ctx));
}

static void perf_io_write_fl(const char *file, int line, const char *event_name,
			     const struct repository *repo,
			     uint64_t *p_us_elapsed_absolute,
			     uint64_t *p_us_elapsed_relative,
			     const char *category,
			     const struct strbuf *buf_payload)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	struct strbuf buf_line = STRBUF_INIT;

	perf_fmt_prepare(event_name, ctx, file, line, repo,
			 p_us_elapsed_absolute, p_us_elapsed_relative, category,
			 &buf_line);
	strbuf_addbuf(&buf_line, buf_payload);
	tr2_dst_write_line(&tr2dst_perf, &buf_line);
	strbuf_release(&buf_line);
}

static void fn_version_fl(const char *file, int line)
{
	const char *event_name = "version";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addstr(&buf_payload, git_version_string);

	perf_io_write_fl(file, line, event_name, NULL, NULL, NULL, NULL,
			 &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_start_fl(const char *file, int line,
			uint64_t us_elapsed_absolute, const char **argv)
{
	const char *event_name = "start";
	struct strbuf buf_payload = STRBUF_INIT;

	sq_append_quote_argv_pretty(&buf_payload, argv);

	perf_io_write_fl(file, line, event_name, NULL, &us_elapsed_absolute,
			 NULL, NULL, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_exit_fl(const char *file, int line, uint64_t us_elapsed_absolute,
		       int code)
{
	const char *event_name = "exit";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "code:%d", code);

	perf_io_write_fl(file, line, event_name, NULL, &us_elapsed_absolute,
			 NULL, NULL, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_signal(uint64_t us_elapsed_absolute, int signo)
{
	const char *event_name = "signal";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "signo:%d", signo);

	perf_io_write_fl(__FILE__, __LINE__, event_name, NULL,
			 &us_elapsed_absolute, NULL, NULL, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_atexit(uint64_t us_elapsed_absolute, int code)
{
	const char *event_name = "atexit";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "code:%d", code);

	perf_io_write_fl(__FILE__, __LINE__, event_name, NULL,
			 &us_elapsed_absolute, NULL, NULL, &buf_payload);
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
	const char *event_name = "error";
	struct strbuf buf_payload = STRBUF_INIT;

	maybe_append_string_va(&buf_payload, fmt, ap);

	perf_io_write_fl(file, line, event_name, NULL, NULL, NULL, NULL,
			 &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_command_path_fl(const char *file, int line, const char *pathname)
{
	const char *event_name = "cmd_path";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addstr(&buf_payload, pathname);

	perf_io_write_fl(file, line, event_name, NULL, NULL, NULL, NULL,
			 &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_command_ancestry_fl(const char *file, int line, const char **parent_names)
{
	const char *event_name = "cmd_ancestry";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addstr(&buf_payload, "ancestry:[");
	/* It's not an argv but the rules are basically the same. */
	sq_append_quote_argv_pretty(&buf_payload, parent_names);
	strbuf_addch(&buf_payload, ']');

	perf_io_write_fl(file, line, event_name, NULL, NULL, NULL, NULL,
			 &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_command_name_fl(const char *file, int line, const char *name,
			       const char *hierarchy)
{
	const char *event_name = "cmd_name";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addstr(&buf_payload, name);
	if (hierarchy && *hierarchy)
		strbuf_addf(&buf_payload, " (%s)", hierarchy);

	perf_io_write_fl(file, line, event_name, NULL, NULL, NULL, NULL,
			 &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_command_mode_fl(const char *file, int line, const char *mode)
{
	const char *event_name = "cmd_mode";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addstr(&buf_payload, mode);

	perf_io_write_fl(file, line, event_name, NULL, NULL, NULL, NULL,
			 &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_alias_fl(const char *file, int line, const char *alias,
			const char **argv)
{
	const char *event_name = "alias";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "alias:%s argv:[", alias);
	sq_append_quote_argv_pretty(&buf_payload, argv);
	strbuf_addch(&buf_payload, ']');

	perf_io_write_fl(file, line, event_name, NULL, NULL, NULL, NULL,
			 &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_child_start_fl(const char *file, int line,
			      uint64_t us_elapsed_absolute,
			      const struct child_process *cmd)
{
	const char *event_name = "child_start";
	struct strbuf buf_payload = STRBUF_INIT;

	if (cmd->trace2_hook_name) {
		strbuf_addf(&buf_payload, "[ch%d] class:hook hook:%s",
			    cmd->trace2_child_id, cmd->trace2_hook_name);
	} else {
		const char *child_class =
			cmd->trace2_child_class ? cmd->trace2_child_class : "?";
		strbuf_addf(&buf_payload, "[ch%d] class:%s",
			    cmd->trace2_child_id, child_class);
	}

	if (cmd->dir) {
		strbuf_addstr(&buf_payload, " cd:");
		sq_quote_buf_pretty(&buf_payload, cmd->dir);
	}

	strbuf_addstr(&buf_payload, " argv:[");
	if (cmd->git_cmd) {
		strbuf_addstr(&buf_payload, "git");
		if (cmd->argv[0])
			strbuf_addch(&buf_payload, ' ');
	}
	sq_append_quote_argv_pretty(&buf_payload, cmd->argv);
	strbuf_addch(&buf_payload, ']');

	perf_io_write_fl(file, line, event_name, NULL, &us_elapsed_absolute,
			 NULL, NULL, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_child_exit_fl(const char *file, int line,
			     uint64_t us_elapsed_absolute, int cid, int pid,
			     int code, uint64_t us_elapsed_child)
{
	const char *event_name = "child_exit";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "[ch%d] pid:%d code:%d", cid, pid, code);

	perf_io_write_fl(file, line, event_name, NULL, &us_elapsed_absolute,
			 &us_elapsed_child, NULL, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_thread_start_fl(const char *file, int line,
			       uint64_t us_elapsed_absolute)
{
	const char *event_name = "thread_start";
	struct strbuf buf_payload = STRBUF_INIT;

	perf_io_write_fl(file, line, event_name, NULL, &us_elapsed_absolute,
			 NULL, NULL, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_thread_exit_fl(const char *file, int line,
			      uint64_t us_elapsed_absolute,
			      uint64_t us_elapsed_thread)
{
	const char *event_name = "thread_exit";
	struct strbuf buf_payload = STRBUF_INIT;

	perf_io_write_fl(file, line, event_name, NULL, &us_elapsed_absolute,
			 &us_elapsed_thread, NULL, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_exec_fl(const char *file, int line, uint64_t us_elapsed_absolute,
		       int exec_id, const char *exe, const char **argv)
{
	const char *event_name = "exec";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "id:%d ", exec_id);
	strbuf_addstr(&buf_payload, "argv:[");
	if (exe) {
		strbuf_addstr(&buf_payload, exe);
		if (argv[0])
			strbuf_addch(&buf_payload, ' ');
	}
	sq_append_quote_argv_pretty(&buf_payload, argv);
	strbuf_addch(&buf_payload, ']');

	perf_io_write_fl(file, line, event_name, NULL, &us_elapsed_absolute,
			 NULL, NULL, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_exec_result_fl(const char *file, int line,
			      uint64_t us_elapsed_absolute, int exec_id,
			      int code)
{
	const char *event_name = "exec_result";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "id:%d code:%d", exec_id, code);
	if (code > 0)
		strbuf_addf(&buf_payload, " err:%s", strerror(code));

	perf_io_write_fl(file, line, event_name, NULL, &us_elapsed_absolute,
			 NULL, NULL, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_param_fl(const char *file, int line, const char *param,
			const char *value)
{
	const char *event_name = "def_param";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "%s:%s", param, value);

	perf_io_write_fl(file, line, event_name, NULL, NULL, NULL, NULL,
			 &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_repo_fl(const char *file, int line,
		       const struct repository *repo)
{
	const char *event_name = "def_repo";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addstr(&buf_payload, "worktree:");
	sq_quote_buf_pretty(&buf_payload, repo->worktree);

	perf_io_write_fl(file, line, event_name, repo, NULL, NULL, NULL,
			 &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_region_enter_printf_va_fl(const char *file, int line,
					 uint64_t us_elapsed_absolute,
					 const char *category,
					 const char *label,
					 const struct repository *repo,
					 const char *fmt, va_list ap)
{
	const char *event_name = "region_enter";
	struct strbuf buf_payload = STRBUF_INIT;

	if (label)
		strbuf_addf(&buf_payload, "label:%s", label);
	if (fmt && *fmt) {
		strbuf_addch(&buf_payload, ' ');
		maybe_append_string_va(&buf_payload, fmt, ap);
	}

	perf_io_write_fl(file, line, event_name, repo, &us_elapsed_absolute,
			 NULL, category, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_region_leave_printf_va_fl(
	const char *file, int line, uint64_t us_elapsed_absolute,
	uint64_t us_elapsed_region, const char *category, const char *label,
	const struct repository *repo, const char *fmt, va_list ap)
{
	const char *event_name = "region_leave";
	struct strbuf buf_payload = STRBUF_INIT;

	if (label)
		strbuf_addf(&buf_payload, "label:%s", label);
	if (fmt && *fmt) {
		strbuf_addch(&buf_payload, ' ' );
		maybe_append_string_va(&buf_payload, fmt, ap);
	}

	perf_io_write_fl(file, line, event_name, repo, &us_elapsed_absolute,
			 &us_elapsed_region, category, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_data_fl(const char *file, int line, uint64_t us_elapsed_absolute,
		       uint64_t us_elapsed_region, const char *category,
		       const struct repository *repo, const char *key,
		       const char *value)
{
	const char *event_name = "data";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "%s:%s", key, value);

	perf_io_write_fl(file, line, event_name, repo, &us_elapsed_absolute,
			 &us_elapsed_region, category, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_data_json_fl(const char *file, int line,
			    uint64_t us_elapsed_absolute,
			    uint64_t us_elapsed_region, const char *category,
			    const struct repository *repo, const char *key,
			    const struct json_writer *value)
{
	const char *event_name = "data_json";
	struct strbuf buf_payload = STRBUF_INIT;

	strbuf_addf(&buf_payload, "%s:%s", key, value->json.buf);

	perf_io_write_fl(file, line, event_name, repo, &us_elapsed_absolute,
			 &us_elapsed_region, category, &buf_payload);
	strbuf_release(&buf_payload);
}

static void fn_printf_va_fl(const char *file, int line,
			    uint64_t us_elapsed_absolute, const char *fmt,
			    va_list ap)
{
	const char *event_name = "printf";
	struct strbuf buf_payload = STRBUF_INIT;

	maybe_append_string_va(&buf_payload, fmt, ap);

	perf_io_write_fl(file, line, event_name, NULL, &us_elapsed_absolute,
			 NULL, NULL, &buf_payload);
	strbuf_release(&buf_payload);
}

struct tr2_tgt tr2_tgt_perf = {
	&tr2dst_perf,

	fn_init,
	fn_term,

	fn_version_fl,
	fn_start_fl,
	fn_exit_fl,
	fn_signal,
	fn_atexit,
	fn_error_va_fl,
	fn_command_path_fl,
	fn_command_ancestry_fl,
	fn_command_name_fl,
	fn_command_mode_fl,
	fn_alias_fl,
	fn_child_start_fl,
	fn_child_exit_fl,
	fn_thread_start_fl,
	fn_thread_exit_fl,
	fn_exec_fl,
	fn_exec_result_fl,
	fn_param_fl,
	fn_repo_fl,
	fn_region_enter_printf_va_fl,
	fn_region_leave_printf_va_fl,
	fn_data_fl,
	fn_data_json_fl,
	fn_printf_va_fl,
};
