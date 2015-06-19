/*
 * GIT - The information manager from hell
 *
 * Copyright (C) 2000-2002 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 2002-2004 Oswald Buddenhagen <ossi@users.sf.net>
 * Copyright (C) 2004 Theodore Y. Ts'o <tytso@mit.edu>
 * Copyright (C) 2006 Mike McCormack
 * Copyright (C) 2006 Christian Couder
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "cache.h"
#include "quote.h"

/* Get a trace file descriptor from "key" env variable. */
static int get_trace_fd(struct trace_key *key)
{
	static struct trace_key trace_default = { "GIT_TRACE" };
	const char *trace;

	/* use default "GIT_TRACE" if NULL */
	if (!key)
		key = &trace_default;

	/* don't open twice */
	if (key->initialized)
		return key->fd;

	trace = getenv(key->key);

	if (!trace || !strcmp(trace, "") ||
	    !strcmp(trace, "0") || !strcasecmp(trace, "false"))
		key->fd = 0;
	else if (!strcmp(trace, "1") || !strcasecmp(trace, "true"))
		key->fd = STDERR_FILENO;
	else if (strlen(trace) == 1 && isdigit(*trace))
		key->fd = atoi(trace);
	else if (is_absolute_path(trace)) {
		int fd = open(trace, O_WRONLY | O_APPEND | O_CREAT, 0666);
		if (fd == -1) {
			fprintf(stderr,
				"Could not open '%s' for tracing: %s\n"
				"Defaulting to tracing on stderr...\n",
				trace, strerror(errno));
			key->fd = STDERR_FILENO;
		} else {
			key->fd = fd;
			key->need_close = 1;
		}
	} else {
		fprintf(stderr, "What does '%s' for %s mean?\n"
			"If you want to trace into a file, then please set "
			"%s to an absolute pathname (starting with /).\n"
			"Defaulting to tracing on stderr...\n",
			trace, key->key, key->key);
		key->fd = STDERR_FILENO;
	}

	key->initialized = 1;
	return key->fd;
}

void trace_disable(struct trace_key *key)
{
	if (key->need_close)
		close(key->fd);
	key->fd = 0;
	key->initialized = 1;
	key->need_close = 0;
}

static const char err_msg[] = "Could not trace into fd given by "
	"GIT_TRACE environment variable";

static int prepare_trace_line(const char *file, int line,
			      struct trace_key *key, struct strbuf *buf)
{
	static struct trace_key trace_bare = TRACE_KEY_INIT(BARE);
	struct timeval tv;
	struct tm tm;
	time_t secs;

	if (!trace_want(key))
		return 0;

	set_try_to_free_routine(NULL);	/* is never reset */

	/* unit tests may want to disable additional trace output */
	if (trace_want(&trace_bare))
		return 1;

	/* print current timestamp */
	gettimeofday(&tv, NULL);
	secs = tv.tv_sec;
	localtime_r(&secs, &tm);
	strbuf_addf(buf, "%02d:%02d:%02d.%06ld ", tm.tm_hour, tm.tm_min,
		    tm.tm_sec, (long) tv.tv_usec);

#ifdef HAVE_VARIADIC_MACROS
	/* print file:line */
	strbuf_addf(buf, "%s:%d ", file, line);
	/* align trace output (column 40 catches most files names in git) */
	while (buf->len < 40)
		strbuf_addch(buf, ' ');
#endif

	return 1;
}

void trace_verbatim(struct trace_key *key, const void *buf, unsigned len)
{
	if (!trace_want(key))
		return;
	write_or_whine_pipe(get_trace_fd(key), buf, len, err_msg);
}

static void print_trace_line(struct trace_key *key, struct strbuf *buf)
{
	strbuf_complete_line(buf);

	write_or_whine_pipe(get_trace_fd(key), buf->buf, buf->len, err_msg);
	strbuf_release(buf);
}

static void trace_vprintf_fl(const char *file, int line, struct trace_key *key,
			     const char *format, va_list ap)
{
	struct strbuf buf = STRBUF_INIT;

	if (!prepare_trace_line(file, line, key, &buf))
		return;

	strbuf_vaddf(&buf, format, ap);
	print_trace_line(key, &buf);
}

static void trace_argv_vprintf_fl(const char *file, int line,
				  const char **argv, const char *format,
				  va_list ap)
{
	struct strbuf buf = STRBUF_INIT;

	if (!prepare_trace_line(file, line, NULL, &buf))
		return;

	strbuf_vaddf(&buf, format, ap);

	sq_quote_argv(&buf, argv, 0);
	print_trace_line(NULL, &buf);
}

void trace_strbuf_fl(const char *file, int line, struct trace_key *key,
		     const struct strbuf *data)
{
	struct strbuf buf = STRBUF_INIT;

	if (!prepare_trace_line(file, line, key, &buf))
		return;

	strbuf_addbuf(&buf, data);
	print_trace_line(key, &buf);
}

static struct trace_key trace_perf_key = TRACE_KEY_INIT(PERFORMANCE);

static void trace_performance_vprintf_fl(const char *file, int line,
					 uint64_t nanos, const char *format,
					 va_list ap)
{
	struct strbuf buf = STRBUF_INIT;

	if (!prepare_trace_line(file, line, &trace_perf_key, &buf))
		return;

	strbuf_addf(&buf, "performance: %.9f s", (double) nanos / 1000000000);

	if (format && *format) {
		strbuf_addstr(&buf, ": ");
		strbuf_vaddf(&buf, format, ap);
	}

	print_trace_line(&trace_perf_key, &buf);
}

#ifndef HAVE_VARIADIC_MACROS

void trace_printf(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	trace_vprintf_fl(NULL, 0, NULL, format, ap);
	va_end(ap);
}

void trace_printf_key(struct trace_key *key, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	trace_vprintf_fl(NULL, 0, key, format, ap);
	va_end(ap);
}

void trace_argv_printf(const char **argv, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	trace_argv_vprintf_fl(NULL, 0, argv, format, ap);
	va_end(ap);
}

void trace_strbuf(struct trace_key *key, const struct strbuf *data)
{
	trace_strbuf_fl(NULL, 0, key, data);
}

void trace_performance(uint64_t nanos, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	trace_performance_vprintf_fl(NULL, 0, nanos, format, ap);
	va_end(ap);
}

void trace_performance_since(uint64_t start, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	trace_performance_vprintf_fl(NULL, 0, getnanotime() - start,
				     format, ap);
	va_end(ap);
}

#else

void trace_printf_key_fl(const char *file, int line, struct trace_key *key,
			 const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	trace_vprintf_fl(file, line, key, format, ap);
	va_end(ap);
}

void trace_argv_printf_fl(const char *file, int line, const char **argv,
			  const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	trace_argv_vprintf_fl(file, line, argv, format, ap);
	va_end(ap);
}

void trace_performance_fl(const char *file, int line, uint64_t nanos,
			      const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	trace_performance_vprintf_fl(file, line, nanos, format, ap);
	va_end(ap);
}

#endif /* HAVE_VARIADIC_MACROS */


static const char *quote_crnl(const char *path)
{
	static char new_path[PATH_MAX];
	const char *p2 = path;
	char *p1 = new_path;

	if (!path)
		return NULL;

	while (*p2) {
		switch (*p2) {
		case '\\': *p1++ = '\\'; *p1++ = '\\'; break;
		case '\n': *p1++ = '\\'; *p1++ = 'n'; break;
		case '\r': *p1++ = '\\'; *p1++ = 'r'; break;
		default:
			*p1++ = *p2;
		}
		p2++;
	}
	*p1 = '\0';
	return new_path;
}

/* FIXME: move prefix to startup_info struct and get rid of this arg */
void trace_repo_setup(const char *prefix)
{
	static struct trace_key key = TRACE_KEY_INIT(SETUP);
	const char *git_work_tree;
	char *cwd;

	if (!trace_want(&key))
		return;

	cwd = xgetcwd();

	if (!(git_work_tree = get_git_work_tree()))
		git_work_tree = "(null)";

	if (!prefix)
		prefix = "(null)";

	trace_printf_key(&key, "setup: git_dir: %s\n", quote_crnl(get_git_dir()));
	trace_printf_key(&key, "setup: git_common_dir: %s\n", quote_crnl(get_git_common_dir()));
	trace_printf_key(&key, "setup: worktree: %s\n", quote_crnl(git_work_tree));
	trace_printf_key(&key, "setup: cwd: %s\n", quote_crnl(cwd));
	trace_printf_key(&key, "setup: prefix: %s\n", quote_crnl(prefix));

	free(cwd);
}

int trace_want(struct trace_key *key)
{
	return !!get_trace_fd(key);
}

#if defined(HAVE_CLOCK_GETTIME) && defined(HAVE_CLOCK_MONOTONIC)

static inline uint64_t highres_nanos(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		return 0;
	return (uint64_t) ts.tv_sec * 1000000000 + ts.tv_nsec;
}

#elif defined (GIT_WINDOWS_NATIVE)

static inline uint64_t highres_nanos(void)
{
	static uint64_t high_ns, scaled_low_ns;
	static int scale;
	LARGE_INTEGER cnt;

	if (!scale) {
		if (!QueryPerformanceFrequency(&cnt))
			return 0;

		/* high_ns = number of ns per cnt.HighPart */
		high_ns = (1000000000LL << 32) / (uint64_t) cnt.QuadPart;

		/*
		 * Number of ns per cnt.LowPart is 10^9 / frequency (or
		 * high_ns >> 32). For maximum precision, we scale this factor
		 * so that it just fits within 32 bit (i.e. won't overflow if
		 * multiplied with cnt.LowPart).
		 */
		scaled_low_ns = high_ns;
		scale = 32;
		while (scaled_low_ns >= 0x100000000LL) {
			scaled_low_ns >>= 1;
			scale--;
		}
	}

	/* if QPF worked on initialization, we expect QPC to work as well */
	QueryPerformanceCounter(&cnt);

	return (high_ns * cnt.HighPart) +
	       ((scaled_low_ns * cnt.LowPart) >> scale);
}

#else
# define highres_nanos() 0
#endif

static inline uint64_t gettimeofday_nanos(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t) tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
}

/*
 * Returns nanoseconds since the epoch (01/01/1970), for performance tracing
 * (i.e. favoring high precision over wall clock time accuracy).
 */
uint64_t getnanotime(void)
{
	static uint64_t offset;
	if (offset > 1) {
		/* initialization succeeded, return offset + high res time */
		return offset + highres_nanos();
	} else if (offset == 1) {
		/* initialization failed, fall back to gettimeofday */
		return gettimeofday_nanos();
	} else {
		/* initialize offset if high resolution timer works */
		uint64_t now = gettimeofday_nanos();
		uint64_t highres = highres_nanos();
		if (highres)
			offset = now - highres;
		else
			offset = 1;
		return now;
	}
}

static uint64_t command_start_time;
static struct strbuf command_line = STRBUF_INIT;

static void print_command_performance_atexit(void)
{
	trace_performance_since(command_start_time, "git command:%s",
				command_line.buf);
}

void trace_command_performance(const char **argv)
{
	if (!trace_want(&trace_perf_key))
		return;

	if (!command_start_time)
		atexit(print_command_performance_atexit);

	strbuf_reset(&command_line);
	sq_quote_argv(&command_line, argv, 0);
	command_start_time = getnanotime();
}
