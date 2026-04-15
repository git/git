#include "git-compat-util.h"

#include "strbuf.h"
#include "strvec.h"
#include "trace2.h"

/*
 * We need more complex parsing in stat_parent_pid() and
 * parse_proc_stat() below than a dumb fscanf(). That's because while
 * the statcomm field is surrounded by parentheses, the process itself
 * is free to insert any arbitrary byte sequence its its name. That
 * can include newlines, spaces, closing parentheses etc.
 *
 * See do_task_stat() in fs/proc/array.c in linux.git, this is in
 * contrast with the escaped version of the name found in
 * /proc/%d/status.
 *
 * So instead of using fscanf() we'll read N bytes from it, look for
 * the first "(", and then the last ")", anything in-between is our
 * process name.
 *
 * How much N do we need? On Linux /proc/sys/kernel/pid_max is 2^15 by
 * default, but it can be raised set to values of up to 2^22. So
 * that's 7 digits for a PID. We have 2 PIDs in the first four fields
 * we're interested in, so 2 * 7 = 14.
 *
 * We then have 3 spaces between those four values, and we'd like to
 * get to the space between the 4th and the 5th (the "pgrp" field) to
 * make sure we read the entire "ppid" field. So that brings us up to
 * 14 + 3 + 1 = 18. Add the two parentheses around the "comm" value
 * and it's 20. The "state" value itself is then one character (now at
 * 21).
 *
 * Finally the maximum length of the "comm" name itself is 15
 * characters, e.g. a setting of "123456789abcdefg" will be truncated
 * to "123456789abcdef". See PR_SET_NAME in prctl(2). So all in all
 * we'd need to read 21 + 15 = 36 bytes.
 *
 * Let's just read 2^6 (64) instead for good measure. If PID_MAX ever
 * grows past 2^22 we'll be future-proof. We'll then anchor at the
 * last ")" we find to locate the parent PID.
 */
#define STAT_PARENT_PID_READ_N 64

static int parse_proc_stat(struct strbuf *sb, struct strbuf *name,
			    int *statppid)
{
	const char *comm_lhs = strchr(sb->buf, '(');
	const char *comm_rhs = strrchr(sb->buf, ')');
	const char *ppid_lhs, *ppid_rhs;
	char *p;
	pid_t ppid;

	if (!comm_lhs || !comm_rhs)
		goto bad_kernel;

	/*
	 * We're at the ")", that's followed by " X ", where X is a
	 * single "state" character. So advance by 4 bytes.
	 */
	ppid_lhs = comm_rhs + 4;

	/*
	 * Read until the space between the "ppid" and "pgrp" fields
	 * to make sure we're anchored after the untruncated "ppid"
	 * field..
	 */
	ppid_rhs = strchr(ppid_lhs, ' ');
	if (!ppid_rhs)
		goto bad_kernel;

	ppid = strtol(ppid_lhs, &p, 10);
	if (ppid_rhs == p) {
		const char *comm = comm_lhs + 1;
		size_t commlen = comm_rhs - comm;

		strbuf_add(name, comm, commlen);
		*statppid = ppid;

		return 0;
	}

bad_kernel:
	/*
	 * We were able to read our STAT_PARENT_PID_READ_N bytes from
	 * /proc/%d/stat, but the content is bad. Broken kernel?
	 * Should not happen, but handle it gracefully.
	 */
	return -1;
}

static int stat_parent_pid(pid_t pid, struct strbuf *name, int *statppid)
{
	struct strbuf procfs_path = STRBUF_INIT;
	struct strbuf sb = STRBUF_INIT;
	FILE *fp;
	int ret = -1;

	/* try to use procfs if it's present. */
	strbuf_addf(&procfs_path, "/proc/%d/stat", pid);
	fp = fopen(procfs_path.buf, "r");
	if (!fp)
		goto cleanup;

	/*
	 * We could be more strict here and assert that we read at
	 * least STAT_PARENT_PID_READ_N. My reading of procfs(5) is
	 * that on any modern kernel (at least since 2.6.0 released in
	 * 2003) even if all the mandatory numeric fields were zero'd
	 * out we'd get at least 100 bytes, but let's just check that
	 * we got anything at all and trust the parse_proc_stat()
	 * function to handle its "Bad Kernel?" error checking.
	 */
	if (!strbuf_fread(&sb, STAT_PARENT_PID_READ_N, fp))
		goto cleanup;
	if (parse_proc_stat(&sb, name, statppid) < 0)
		goto cleanup;

	ret = 0;
cleanup:
	if (fp)
		fclose(fp);
	strbuf_release(&procfs_path);
	strbuf_release(&sb);

	return ret;
}

static void push_ancestry_name(struct strvec *names, pid_t pid)
{
	struct strbuf name = STRBUF_INIT;
	int ppid;

	if (stat_parent_pid(pid, &name, &ppid) < 0)
		goto cleanup;

	strvec_push(names, name.buf);

	/*
	 * Both errors and reaching the end of the process chain are
	 * reported as fields of 0 by proc(5)
	 */
	if (ppid)
		push_ancestry_name(names, ppid);
cleanup:
	strbuf_release(&name);

	return;
}

void trace2_collect_process_info(enum trace2_process_info_reason reason)
{
	struct strvec names = STRVEC_INIT;

	if (!trace2_is_enabled())
		return;

	switch (reason) {
	case TRACE2_PROCESS_INFO_EXIT:
		/*
		 * The Windows version of this calls its
		 * get_peak_memory_info() here. We may want to insert
		 * similar process-end statistics here in the future.
		 */
		break;
	case TRACE2_PROCESS_INFO_STARTUP:
		push_ancestry_name(&names, getppid());

		if (names.nr)
			trace2_cmd_ancestry(names.v);
		strvec_clear(&names);
		break;
	}

	return;
}
