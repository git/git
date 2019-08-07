#ifndef TRACE_H
#define TRACE_H

#include "git-compat-util.h"
#include "strbuf.h"

struct trace_key {
	const char * const key;
	int fd;
	unsigned int initialized : 1;
	unsigned int  need_close : 1;
};

extern struct trace_key trace_default_key;

#define TRACE_KEY_INIT(name) { "GIT_TRACE_" #name, 0, 0, 0 }
extern struct trace_key trace_perf_key;
extern struct trace_key trace_setup_key;

void trace_repo_setup(const char *prefix);
int trace_want(struct trace_key *key);
void trace_disable(struct trace_key *key);
uint64_t getnanotime(void);
void trace_command_performance(const char **argv);
void trace_verbatim(struct trace_key *key, const void *buf, unsigned len);
uint64_t trace_performance_enter(void);

#ifndef HAVE_VARIADIC_MACROS

__attribute__((format (printf, 1, 2)))
void trace_printf(const char *format, ...);

__attribute__((format (printf, 2, 3)))
void trace_printf_key(struct trace_key *key, const char *format, ...);

__attribute__((format (printf, 2, 3)))
void trace_argv_printf(const char **argv, const char *format, ...);

void trace_strbuf(struct trace_key *key, const struct strbuf *data);

/* Prints elapsed time (in nanoseconds) if GIT_TRACE_PERFORMANCE is enabled. */
__attribute__((format (printf, 2, 3)))
void trace_performance(uint64_t nanos, const char *format, ...);

/* Prints elapsed time since 'start' if GIT_TRACE_PERFORMANCE is enabled. */
__attribute__((format (printf, 2, 3)))
void trace_performance_since(uint64_t start, const char *format, ...);

__attribute__((format (printf, 1, 2)))
void trace_performance_leave(const char *format, ...);

#else

/*
 * Macros to add file:line - see above for C-style declarations of how these
 * should be used.
 */

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

/*
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

#define trace_printf_key(key, ...)					    \
	do {								    \
		if (trace_pass_fl(key))					    \
			trace_printf_key_fl(TRACE_CONTEXT, __LINE__, key,   \
					    __VA_ARGS__);		    \
	} while (0)

#define trace_printf(...) trace_printf_key(&trace_default_key, __VA_ARGS__)

#define trace_argv_printf(argv, ...)					    \
	do {								    \
		if (trace_pass_fl(&trace_default_key))			    \
			trace_argv_printf_fl(TRACE_CONTEXT, __LINE__,	    \
					    argv, __VA_ARGS__);		    \
	} while (0)

#define trace_strbuf(key, data)						    \
	do {								    \
		if (trace_pass_fl(key))					    \
			trace_strbuf_fl(TRACE_CONTEXT, __LINE__, key, data);\
	} while (0)

#define trace_performance(nanos, ...)					    \
	do {								    \
		if (trace_pass_fl(&trace_perf_key))			    \
			trace_performance_fl(TRACE_CONTEXT, __LINE__, nanos,\
					     __VA_ARGS__);		    \
	} while (0)

#define trace_performance_since(start, ...)				    \
	do {								    \
		if (trace_pass_fl(&trace_perf_key))			    \
			trace_performance_fl(TRACE_CONTEXT, __LINE__,       \
					     getnanotime() - (start),	    \
					     __VA_ARGS__);		    \
	} while (0)

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

#endif /* HAVE_VARIADIC_MACROS */

#endif /* TRACE_H */
