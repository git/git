/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "git-compat-util.h"

static void report(const char *prefix, const char *err, va_list params)
{
	fputs(prefix, stderr);
	vfprintf(stderr, err, params);
	fputs("\n", stderr);
}

static NORETURN void usage_builtin(const char *err)
{
	fprintf(stderr, "usage: %s\n", err);
	exit(129);
}

static NORETURN void die_builtin(const char *err, va_list params)
{
	report("fatal: ", err, params);
	exit(128);
}

static void error_builtin(const char *err, va_list params)
{
	report("error: ", err, params);
}


/* If we are in a dlopen()ed .so write to a global variable would segfault
 * (ugh), so keep things static. */
static void (*usage_routine)(const char *err) NORETURN = usage_builtin;
static void (*die_routine)(const char *err, va_list params) NORETURN = die_builtin;
static void (*error_routine)(const char *err, va_list params) = error_builtin;

void set_usage_routine(void (*routine)(const char *err) NORETURN)
{
	usage_routine = routine;
}

void set_die_routine(void (*routine)(const char *err, va_list params) NORETURN)
{
	die_routine = routine;
}

void set_error_routine(void (*routine)(const char *err, va_list params))
{
	error_routine = routine;
}


void usage(const char *err)
{
	usage_routine(err);
}

void die(const char *err, ...)
{
	va_list params;

	va_start(params, err);
	die_routine(err, params);
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
