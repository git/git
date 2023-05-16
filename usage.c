/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "git-compat-util.h"
#include "gettext.h"
#include "trace2.h"
#include "wrapper.h"

static void vreportf(const char *prefix, const char *err, va_list params)
{
	char msg[4096];
	char *p, *pend = msg + sizeof(msg);
	size_t prefix_len = strlen(prefix);

	if (sizeof(msg) <= prefix_len) {
		fprintf(stderr, "BUG!!! too long a prefix '%s'\n", prefix);
		abort();
	}
	memcpy(msg, prefix, prefix_len);
	p = msg + prefix_len;
	if (vsnprintf(p, pend - p, err, params) < 0)
		*p = '\0'; /* vsnprintf() failed, clip at prefix */

	for (; p != pend - 1 && *p; p++) {
		if (iscntrl(*p) && *p != '\t' && *p != '\n')
			*p = '?';
	}

	*(p++) = '\n'; /* we no longer need a NUL */
	fflush(stderr);
	write_in_full(2, msg, p - msg);
}

static NORETURN void usage_builtin(const char *err, va_list params)
{
	vreportf(_("usage: "), err, params);

	/*
	 * When we detect a usage error *before* the command dispatch in
	 * cmd_main(), we don't know what verb to report.  Force it to this
	 * to facilitate post-processing.
	 */
	trace2_cmd_name("_usage_");

	/*
	 * Currently, the (err, params) are usually just the static usage
	 * string which isn't very useful here.  Usually, the call site
	 * manually calls fprintf(stderr,...) with the actual detailed
	 * syntax error before calling usage().
	 *
	 * TODO It would be nice to update the call sites to pass both
	 * the static usage string and the detailed error message.
	 */

	exit(129);
}

static void die_message_builtin(const char *err, va_list params)
{
	trace2_cmd_error_va(err, params);
	vreportf(_("fatal: "), err, params);
}

/*
 * We call trace2_cmd_error_va() in the below functions first and
 * expect it to va_copy 'params' before using it (because an 'ap' can
 * only be walked once).
 */
static NORETURN void die_builtin(const char *err, va_list params)
{
	report_fn die_message_fn = get_die_message_routine();

	die_message_fn(err, params);
	exit(128);
}

static void error_builtin(const char *err, va_list params)
{
	trace2_cmd_error_va(err, params);

	vreportf(_("error: "), err, params);
}

static void warn_builtin(const char *warn, va_list params)
{
	trace2_cmd_error_va(warn, params);

	vreportf(_("warning: "), warn, params);
}

static int die_is_recursing_builtin(void)
{
	static int dying;
	/*
	 * Just an arbitrary number X where "a < x < b" where "a" is
	 * "maximum number of pthreads we'll ever plausibly spawn" and
	 * "b" is "something less than Inf", since the point is to
	 * prevent infinite recursion.
	 */
	static const int recursion_limit = 1024;

	dying++;
	if (dying > recursion_limit) {
		return 1;
	} else if (dying == 2) {
		warning("die() called many times. Recursion error or racy threaded death!");
		return 0;
	} else {
		return 0;
	}
}

/* If we are in a dlopen()ed .so write to a global variable would segfault
 * (ugh), so keep things static. */
static NORETURN_PTR report_fn usage_routine = usage_builtin;
static NORETURN_PTR report_fn die_routine = die_builtin;
static report_fn die_message_routine = die_message_builtin;
static report_fn error_routine = error_builtin;
static report_fn warn_routine = warn_builtin;
static int (*die_is_recursing)(void) = die_is_recursing_builtin;

void set_die_routine(NORETURN_PTR report_fn routine)
{
	die_routine = routine;
}

report_fn get_die_message_routine(void)
{
	return die_message_routine;
}

void set_error_routine(report_fn routine)
{
	error_routine = routine;
}

report_fn get_error_routine(void)
{
	return error_routine;
}

void set_warn_routine(report_fn routine)
{
	warn_routine = routine;
}

report_fn get_warn_routine(void)
{
	return warn_routine;
}

void set_die_is_recursing_routine(int (*routine)(void))
{
	die_is_recursing = routine;
}

void NORETURN usagef(const char *err, ...)
{
	va_list params;

	va_start(params, err);
	usage_routine(err, params);
	va_end(params);
}

void NORETURN usage(const char *err)
{
	usagef("%s", err);
}

void NORETURN die(const char *err, ...)
{
	va_list params;

	if (die_is_recursing()) {
		fputs("fatal: recursion detected in die handler\n", stderr);
		exit(128);
	}

	va_start(params, err);
	die_routine(err, params);
	va_end(params);
}

static const char *fmt_with_err(char *buf, int n, const char *fmt)
{
	char str_error[256], *err;
	int i, j;

	err = strerror(errno);
	for (i = j = 0; err[i] && j < sizeof(str_error) - 1; ) {
		if ((str_error[j++] = err[i++]) != '%')
			continue;
		if (j < sizeof(str_error) - 1) {
			str_error[j++] = '%';
		} else {
			/* No room to double the '%', so we overwrite it with
			 * '\0' below */
			j--;
			break;
		}
	}
	str_error[j] = 0;
	/* Truncation is acceptable here */
	snprintf(buf, n, "%s: %s", fmt, str_error);
	return buf;
}

void NORETURN die_errno(const char *fmt, ...)
{
	char buf[1024];
	va_list params;

	if (die_is_recursing()) {
		fputs("fatal: recursion detected in die_errno handler\n",
			stderr);
		exit(128);
	}

	va_start(params, fmt);
	die_routine(fmt_with_err(buf, sizeof(buf), fmt), params);
	va_end(params);
}

#undef die_message
int die_message(const char *err, ...)
{
	va_list params;

	va_start(params, err);
	die_message_routine(err, params);
	va_end(params);
	return 128;
}

#undef die_message_errno
int die_message_errno(const char *fmt, ...)
{
	char buf[1024];
	va_list params;

	va_start(params, fmt);
	die_message_routine(fmt_with_err(buf, sizeof(buf), fmt), params);
	va_end(params);
	return 128;
}

#undef error_errno
int error_errno(const char *fmt, ...)
{
	char buf[1024];
	va_list params;

	va_start(params, fmt);
	error_routine(fmt_with_err(buf, sizeof(buf), fmt), params);
	va_end(params);
	return -1;
}

#undef error
int error(const char *err, ...)
{
	va_list params;

	va_start(params, err);
	error_routine(err, params);
	va_end(params);
	return -1;
}

void warning_errno(const char *warn, ...)
{
	char buf[1024];
	va_list params;

	va_start(params, warn);
	warn_routine(fmt_with_err(buf, sizeof(buf), warn), params);
	va_end(params);
}

void warning(const char *warn, ...)
{
	va_list params;

	va_start(params, warn);
	warn_routine(warn, params);
	va_end(params);
}

/* Only set this, ever, from t/helper/, when verifying that bugs are caught. */
int BUG_exit_code;

static void BUG_vfl_common(const char *file, int line, const char *fmt,
			   va_list params)
{
	char prefix[256];

	/* truncation via snprintf is OK here */
	snprintf(prefix, sizeof(prefix), "BUG: %s:%d: ", file, line);

	vreportf(prefix, fmt, params);
}

static NORETURN void BUG_vfl(const char *file, int line, const char *fmt, va_list params)
{
	va_list params_copy;
	static int in_bug;

	va_copy(params_copy, params);
	BUG_vfl_common(file, line, fmt, params);

	if (in_bug)
		abort();
	in_bug = 1;

	trace2_cmd_error_va(fmt, params_copy);

	if (BUG_exit_code)
		exit(BUG_exit_code);
	abort();
}

NORETURN void BUG_fl(const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	bug_called_must_BUG = 0;

	va_start(ap, fmt);
	BUG_vfl(file, line, fmt, ap);
	va_end(ap);
}

int bug_called_must_BUG;
void bug_fl(const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	bug_called_must_BUG = 1;

	va_start(ap, fmt);
	BUG_vfl_common(file, line, fmt, ap);
	va_end(ap);

	va_start(ap, fmt);
	trace2_cmd_error_va(fmt, ap);
	va_end(ap);
}

#ifdef SUPPRESS_ANNOTATED_LEAKS
void unleak_memory(const void *ptr, size_t len)
{
	static struct suppressed_leak_root {
		struct suppressed_leak_root *next;
		char data[FLEX_ARRAY];
	} *suppressed_leaks;
	struct suppressed_leak_root *root;

	FLEX_ALLOC_MEM(root, data, ptr, len);
	root->next = suppressed_leaks;
	suppressed_leaks = root;
}
#endif
