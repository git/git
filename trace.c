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

void trace_printf(const char *fmt, ...)
{
	char buf[8192];
	va_list ap;
	int fd, len, need_close = 0;

	fd = get_trace_fd(&need_close);
	if (!fd)
		return;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (len >= sizeof(buf))
		die("unreasonnable trace length");
	write_or_whine_pipe(fd, buf, len, err_msg);

	if (need_close)
		close(fd);
}

void trace_argv_printf(const char **argv, int count, const char *fmt, ...)
{
	char buf[8192];
	va_list ap;
	char *argv_str;
	size_t argv_len;
	int fd, len, need_close = 0;

	fd = get_trace_fd(&need_close);
	if (!fd)
		return;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (len >= sizeof(buf))
		die("unreasonnable trace length");

	/* Get the argv string. */
	argv_str = sq_quote_argv(argv, count);
	argv_len = strlen(argv_str);

	write_or_whine_pipe(fd, buf, len, err_msg);
	write_or_whine_pipe(fd, argv_str, argv_len, err_msg);
	write_or_whine_pipe(fd, "\n", 1, err_msg);

	free(argv_str);

	if (need_close)
		close(fd);
}
