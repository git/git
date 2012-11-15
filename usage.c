/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "git-compat-util.h"
#include "cache.h"

static int dying;

void vreportf(const char *prefix, const char *err, va_list params)
{
	char msg[4096];
	vsnprintf(msg, sizeof(msg), err, params);
	fprintf(stderr, "%s%s\n", prefix, msg);
}

void vwritef(int fd, const char *prefix, const char *err, va_list params)
{
	char msg[4096];
	int len = vsnprintf(msg, sizeof(msg), err, params);
	if (len > sizeof(msg))
		len = sizeof(msg);

	write_in_full(fd, prefix, strlen(prefix));
	write_in_full(fd, msg, len);
	write_in_full(fd, "\n", 1);
}

static NORETURN void usage_builtin(const char *err, va_list params)
{
	vreportf("usage: ", err, params);
	exit(129);
}

static NORETURN void die_builtin(const char *err, va_list params)
{
	vreportf("fatal: ", err, params);
	exit(128);
}

static void error_builtin(const char *err, va_list params)
{
	vreportf("error: ", err, params);
}

static void warn_builtin(const char *warn, va_list params)
{
	vreportf("warning: ", warn, params);
}

/* If we are in a dlopen()ed .so write to a global variable would segfault
 * (ugh), so keep things static. */
static NORETURN_PTR void (*usage_routine)(const char *err, va_list params) = usage_builtin;
static NORETURN_PTR void (*die_routine)(const char *err, va_list params) = die_builtin;
static void (*error_routine)(const char *err, va_list params) = error_builtin;
static void (*warn_routine)(const char *err, va_list params) = warn_builtin;

void set_die_routine(NORETURN_PTR void (*routine)(const char *err, va_list params))
{
	die_routine = routine;
}

void set_error_routine(void (*routine)(const char *err, va_list params))
{
	error_routine = routine;
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

	if (dying) {
		fputs("fatal: recursion detected in die handler\n", stderr);
		exit(128);
	}
	dying = 1;

	va_start(params, err);
	die_routine(err, params);
	va_end(params);
}

void NORETURN die_errno(const char *fmt, ...)
{
	va_list params;
	char fmt_with_err[1024];
	char str_error[256], *err;
	int i, j;

	if (dying) {
		fputs("fatal: recursion detected in die_errno handler\n",
			stderr);
		exit(128);
	}
	dying = 1;

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
	snprintf(fmt_with_err, sizeof(fmt_with_err), "%s: %s", fmt, str_error);

	va_start(params, fmt);
	die_routine(fmt_with_err, params);
	va_end(params);
}

int error(const char *err, ...)
{
	va_list params;

	va_start(params, err);
	error_routine(err, params);
	va_end(params);
	return -1;
}

void warning(const char *warn, ...)
{
	va_list params;

	va_start(params, warn);
	warn_routine(warn, params);
	va_end(params);
}
