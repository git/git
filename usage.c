/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "git-compat-util.h"

void vreport(const char *prefix, const char *err, va_list params)
{
	char msg[1024];
	vsnprintf(msg, sizeof(msg), err, params);
	fprintf(stderr, "%s%s\n", prefix, msg);
}

static NORETURN void usage_builtin(const char *err)
{
	fprintf(stderr, "usage: %s\n", err);
	exit(129);
}

static NORETURN void die_builtin(const char *err, va_list params)
{
	vreport("fatal: ", err, params);
	exit(128);
}

static void error_builtin(const char *err, va_list params)
{
	vreport("error: ", err, params);
}

static void warn_builtin(const char *warn, va_list params)
{
	vreport("warning: ", warn, params);
}

typedef void (*die_fn_t)(const char *err, va_list params) NORETURN;

static DWORD tls_index;

static void tls_init(void) __attribute__((constructor));
static void tls_init(void)
{
	tls_index = TlsAlloc();
}

struct routines {
	die_fn_t die_routine;
};
/* If we are in a dlopen()ed .so write to a global variable would segfault
 * (ugh), so keep things static. */
static void (*usage_routine)(const char *err) NORETURN = usage_builtin;
static void (*error_routine)(const char *err, va_list params) = error_builtin;
static void (*warn_routine)(const char *err, va_list params) = warn_builtin;

void set_die_routine(void (*routine)(const char *err, va_list params) NORETURN)
{
	struct routines *r = TlsGetValue(tls_index);
	if (r == NULL) {
		/* avoid die()! */
		r = calloc(sizeof(*r), 1);
		if (r == NULL) {
			fprintf(stderr, "cannot allocate thread-local storage");
			return;
		}
		TlsSetValue(tls_index, r);
	}
	r->die_routine = routine;
}

void usage(const char *err)
{
	usage_routine(err);
}

void die(const char *err, ...)
{
	va_list params;
	struct routines *r = TlsGetValue(tls_index);

	va_start(params, err);
	if (r == NULL || r->die_routine == NULL)
		die_builtin(err, params);
	else
		r->die_routine(err, params);
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
