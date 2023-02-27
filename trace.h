#ifndef TRACE_H
#define TRACE_H

#include "git-compat-util.h"
#include "strbuf.h"

/**
 * The trace API can be used to print debug messages to stderr or a file. Trace
 * code is inactive unless explicitly enabled by setting `GIT_TRACE*` environment
 * variables.
 *
 * The trace implementation automatically adds `timestamp file:line ... \n` to
 * all trace messages. E.g.:
 *
 * ------------
 * 23:59:59.123456 git.c:312               trace: built-in: git 'foo'
 * 00:00:00.000001 builtin/foo.c:99        foo: some message
 * ------------
 *
 * Bugs & Caveats
 * --------------
 *
 * GIT_TRACE_* environment variables can be used to tell Git to show
 * trace output to its standard error stream. Git can often spawn a pager
 * internally to run its subcommand and send its standard output and
 * standard error to it.
 *
 * Because GIT_TRACE_PERFORMANCE trace is generated only at the very end
 * of the program with atexit(), which happens after the pager exits, it
 * would not work well if you send its log to the standard error output
 * and let Git spawn the pager at the same time.
 *
 * As a work around, you can for example use '--no-pager', or set
 * GIT_TRACE_PERFORMANCE to another file descriptor which is redirected
 * to stderr, or set GIT_TRACE_PERFORMANCE to a file specified by its
 * absolute path.
 *
 * For example instead of the following command which by default may not
 * print any performance information:
 *
 * ------------
 * GIT_TRACE_PERFORMANCE=2 git log -1
 * ------------
 *
 * you may want to use:
 *
 * ------------
 * GIT_TRACE_PERFORMANCE=2 git --no-pager log -1
 * ------------
 *
 * or:
 *
 * ------------
 * GIT_TRACE_PERFORMANCE=3 3>&2 git log -1
 * ------------
 *
 * or:
 *
 * ------------
 * GIT_TRACE_PERFORMANCE=/path/to/log/file git log -1
 * ------------
 *
 */

/**
 * Defines a trace key (or category). The default (for API functions that
 * don't take a key) is `GIT_TRACE`.
 *
 * E.g. to define a trace key controlled by environment variable `GIT_TRACE_FOO`:
 *
 * ------------
 * static struct trace_key trace_foo = TRACE_KEY_INIT(FOO);
 *
 * static void trace_print_foo(const char *message)
 * {
 * 	trace_printf_key(&trace_foo, "%s", message);
 * }
 * ------------
 *
 * Note: don't use `const` as the trace implementation stores internal state in
 * the `trace_key` structure.
 */
struct trace_key {
	const char * const key;
	int fd;
	unsigned int initialized : 1;
	unsigned int  need_close : 1;
};

extern struct trace_key trace_default_key;

#define TRACE_KEY_INIT(name) { .key = "GIT_TRACE_" #name }
extern struct trace_key trace_perf_key;
extern struct trace_key trace_setup_key;

void trace_repo_setup(void);

/**
 * Checks whether the trace key is enabled. Used to prevent expensive
 * string formatting before calling one of the printing APIs.
 */
int trace_want(struct trace_key *key);

/**
 * Enables or disables tracing for the specified key, as if the environment
 * variable was set to the given value.
 */
void trace_override_envvar(struct trace_key *key, const char *value);

/**
 * Disables tracing for the specified key, even if the environment variable
 * was set.
 */
void trace_disable(struct trace_key *key);

/**
 * Returns nanoseconds since the epoch (01/01/1970), typically used
 * for performance measurements.
 * Currently there are high precision timer implementations for Linux (using
 * `clock_gettime(CLOCK_MONOTONIC)`) and Windows (`QueryPerformanceCounter`).
 * Other platforms use `gettimeofday` as time source.
 */
uint64_t getnanotime(void);

void trace_command_performance(const char **argv);
void trace_verbatim(struct trace_key *key, const void *buf, unsigned len);
uint64_t trace_performance_enter(void);

/*
 * TRACE_CONTEXT may be set to __FUNCTION__ if the compiler supports it. The
 * default is __FILE__, as it is consistent with assert(), and static function
 * names are not necessarily unique.
 *
 * __FILE__ ":" __FUNCTION__ doesn't work with GNUC, as __FILE__ is supplied
 * by the preprocessor as a string literal, and __FUNCTION__ is filled in by
 * the compiler as a string constant.
 */
#ifndef TRACE_CONTEXT
# define TRACE_CONTEXT __FILE__
#endif

/**
 * Macros to add the file:line of the calling code, instead of that of
 * the trace function itself.
 *
 * Note: with C99 variadic macros, __VA_ARGS__ must include the last fixed
 * parameter ('format' in this case). Otherwise, a call without variable
 * arguments will have a surplus ','. E.g.:
 *
 *  #define foo(format, ...) bar(format, __VA_ARGS__)
 *  foo("test");
 *
 * will expand to
 *
 *  bar("test",);
 *
 * which is invalid (note the ',)'). With GNUC, '##__VA_ARGS__' drops the
 * comma, but this is non-standard.
 */

/**
 * trace_printf(), accepts "const char *format, ...".
 *
 * Prints a formatted message, similar to printf.
 */
#define trace_printf(...) trace_printf_key(&trace_default_key, __VA_ARGS__)

/**
 * trace_printf_key(), accepts "struct trace_key *key, const char *format, ...".
 */
#define trace_printf_key(key, ...)					    \
	do {								    \
		if (trace_pass_fl(key))					    \
			trace_printf_key_fl(TRACE_CONTEXT, __LINE__, key,   \
					    __VA_ARGS__);		    \
	} while (0)

/**
 * trace_argv_printf(), accepts "struct trace_key *key, const char *format, ...)".
 *
 * Prints a formatted message, followed by a quoted list of arguments.
 */
#define trace_argv_printf(argv, ...)					    \
	do {								    \
		if (trace_pass_fl(&trace_default_key))			    \
			trace_argv_printf_fl(TRACE_CONTEXT, __LINE__,	    \
					    argv, __VA_ARGS__);		    \
	} while (0)

/**
 * trace_strbuf(), accepts "struct trace_key *key, const struct strbuf *data".
 *
 * Prints the strbuf, without additional formatting (i.e. doesn't
 * choke on `%` or even `\0`).
 */
#define trace_strbuf(key, data)						    \
	do {								    \
		if (trace_pass_fl(key))					    \
			trace_strbuf_fl(TRACE_CONTEXT, __LINE__, key, data);\
	} while (0)

/**
 * trace_performance(), accepts "uint64_t nanos, const char *format, ...".
 *
 * Prints elapsed time (in nanoseconds) if GIT_TRACE_PERFORMANCE is enabled.
 *
 * Example:
 * ------------
 * uint64_t t = 0;
 * for (;;) {
 * 	// ignore
 * t -= getnanotime();
 * // code section to measure
 * t += getnanotime();
 * // ignore
 * }
 * trace_performance(t, "frotz");
 * ------------
 */
#define trace_performance(nanos, ...)					    \
	do {								    \
		if (trace_pass_fl(&trace_perf_key))			    \
			trace_performance_fl(TRACE_CONTEXT, __LINE__, nanos,\
					     __VA_ARGS__);		    \
	} while (0)

/**
 * trace_performance_since(), accepts "uint64_t start, const char *format, ...".
 *
 * Prints elapsed time since 'start' if GIT_TRACE_PERFORMANCE is enabled.
 *
 * Example:
 * ------------
 * uint64_t start = getnanotime();
 * // code section to measure
 * trace_performance_since(start, "foobar");
 * ------------
 */
#define trace_performance_since(start, ...)				    \
	do {								    \
		if (trace_pass_fl(&trace_perf_key))			    \
			trace_performance_fl(TRACE_CONTEXT, __LINE__,       \
					     getnanotime() - (start),	    \
					     __VA_ARGS__);		    \
	} while (0)

/**
 * trace_performance_leave(), accepts "const char *format, ...".
 */
#define trace_performance_leave(...)					    \
	do {								    \
		if (trace_pass_fl(&trace_perf_key))			    \
			trace_performance_leave_fl(TRACE_CONTEXT, __LINE__, \
						   getnanotime(),	    \
						   __VA_ARGS__);	    \
	} while (0)

/* backend functions, use non-*fl macros instead */
__attribute__((format (printf, 4, 5)))
void trace_printf_key_fl(const char *file, int line, struct trace_key *key,
			 const char *format, ...);
__attribute__((format (printf, 4, 5)))
void trace_argv_printf_fl(const char *file, int line, const char **argv,
			  const char *format, ...);
void trace_strbuf_fl(const char *file, int line, struct trace_key *key,
		     const struct strbuf *data);
__attribute__((format (printf, 4, 5)))
void trace_performance_fl(const char *file, int line,
			  uint64_t nanos, const char *fmt, ...);
__attribute__((format (printf, 4, 5)))
void trace_performance_leave_fl(const char *file, int line,
				uint64_t nanos, const char *fmt, ...);
static inline int trace_pass_fl(struct trace_key *key)
{
	return key->fd || !key->initialized;
}

#endif /* TRACE_H */
