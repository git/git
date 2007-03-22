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

/* Stolen from "imap-send.c". */
int nfvasprintf(char **strp, const char *fmt, va_list ap)
{
	int len;
	char tmp[1024];

	if ((len = vsnprintf(tmp, sizeof(tmp), fmt, ap)) < 0 ||
	    !(*strp = xmalloc(len + 1)))
		die("Fatal: Out of memory\n");
	if (len >= (int)sizeof(tmp))
		vsprintf(*strp, fmt, ap);
	else
		memcpy(*strp, tmp, len + 1);
	return len;
}

int nfasprintf(char **str, const char *fmt, ...)
{
	int rc;
	va_list args;

	va_start(args, fmt);
	rc = nfvasprintf(str, fmt, args);
	va_end(args);
	return rc;
}

/* Get a trace file descriptor from GIT_TRACE env variable. */
static int get_trace_fd(int *need_close)
{
	char *trace = getenv("GIT_TRACE");

	if (!trace || !strcmp(trace, "") ||
	    !strcmp(trace, "0") || !strcasecmp(trace, "false"))
		return 0;
	if (!strcmp(trace, "1") || !strcasecmp(trace, "true"))
		return STDERR_FILENO;
	if (strlen(trace) == 1 && isdigit(*trace))
		return atoi(trace);
	if (*trace == '/') {
		int fd = open(trace, O_WRONLY | O_APPEND | O_CREAT, 0666);
		if (fd == -1) {
			fprintf(stderr,
				"Could not open '%s' for tracing: %s\n"
				"Defaulting to tracing on stderr...\n",
				trace, strerror(errno));
			return STDERR_FILENO;
		}
		*need_close = 1;
		return fd;
	}

	fprintf(stderr, "What does '%s' for GIT_TRACE means ?\n", trace);
	fprintf(stderr, "If you want to trace into a file, "
		"then please set GIT_TRACE to an absolute pathname "
		"(starting with /).\n");
	fprintf(stderr, "Defaulting to tracing on stderr...\n");

	return STDERR_FILENO;
}

static const char err_msg[] = "Could not trace into fd given by "
	"GIT_TRACE environment variable";

void trace_printf(const char *format, ...)
{
	char *trace_str;
	va_list rest;
	int need_close = 0;
	int fd = get_trace_fd(&need_close);

	if (!fd)
		return;

	va_start(rest, format);
	nfvasprintf(&trace_str, format, rest);
	va_end(rest);

	write_or_whine_pipe(fd, trace_str, strlen(trace_str), err_msg);

	free(trace_str);

	if (need_close)
		close(fd);
}

void trace_argv_printf(const char **argv, int count, const char *format, ...)
{
	char *argv_str, *format_str, *trace_str;
	size_t argv_len, format_len, trace_len;
	va_list rest;
	int need_close = 0;
	int fd = get_trace_fd(&need_close);

	if (!fd)
		return;

	/* Get the argv string. */
	argv_str = sq_quote_argv(argv, count);
	argv_len = strlen(argv_str);

	/* Get the formated string. */
	va_start(rest, format);
	nfvasprintf(&format_str, format, rest);
	va_end(rest);

	/* Allocate buffer for trace string. */
	format_len = strlen(format_str);
	trace_len = argv_len + format_len + 1; /* + 1 for \n */
	trace_str = xmalloc(trace_len + 1);

	/* Copy everything into the trace string. */
	strncpy(trace_str, format_str, format_len);
	strncpy(trace_str + format_len, argv_str, argv_len);
	strcpy(trace_str + trace_len - 1, "\n");

	write_or_whine_pipe(fd, trace_str, trace_len, err_msg);

	free(argv_str);
	free(format_str);
	free(trace_str);

	if (need_close)
		close(fd);
}
