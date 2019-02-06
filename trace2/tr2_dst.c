#include "cache.h"
#include "trace2/tr2_dst.h"

/*
 * If a Trace2 target cannot be opened for writing, we should issue a
 * warning to stderr, but this is very annoying if the target is a pipe
 * or socket and beyond the user's control -- especially since every
 * git command (and sub-command) will print the message.  So we silently
 * eat these warnings and just discard the trace data.
 *
 * Enable the following environment variable to see these warnings.
 */
#define TR2_ENVVAR_DST_DEBUG "GIT_TR2_DST_DEBUG"

static int tr2_dst_want_warning(void)
{
	static int tr2env_dst_debug = -1;

	if (tr2env_dst_debug == -1) {
		const char *env_value = getenv(TR2_ENVVAR_DST_DEBUG);
		if (!env_value || !*env_value)
			tr2env_dst_debug = 0;
		else
			tr2env_dst_debug = atoi(env_value) > 0;
	}

	return tr2env_dst_debug;
}

void tr2_dst_trace_disable(struct tr2_dst *dst)
{
	if (dst->need_close)
		close(dst->fd);
	dst->fd = 0;
	dst->initialized = 1;
	dst->need_close = 0;
}

static int tr2_dst_try_path(struct tr2_dst *dst, const char *tgt_value)
{
	int fd = open(tgt_value, O_WRONLY | O_APPEND | O_CREAT, 0666);
	if (fd == -1) {
		if (tr2_dst_want_warning())
			warning("trace2: could not open '%s' for '%s' tracing: %s",
				tgt_value, dst->env_var_name, strerror(errno));

		tr2_dst_trace_disable(dst);
		return 0;
	}

	dst->fd = fd;
	dst->need_close = 1;
	dst->initialized = 1;

	return dst->fd;
}

#ifndef NO_UNIX_SOCKETS
#define PREFIX_AF_UNIX "af_unix:"
#define PREFIX_AF_UNIX_LEN (8)

static int tr2_dst_try_unix_domain_socket(struct tr2_dst *dst,
					  const char *tgt_value)
{
	int fd;
	struct sockaddr_un sa;
	const char *path = tgt_value + PREFIX_AF_UNIX_LEN;
	int path_len = strlen(path);

	if (!is_absolute_path(path) || path_len >= sizeof(sa.sun_path)) {
		if (tr2_dst_want_warning())
			warning("trace2: invalid AF_UNIX path '%s' for '%s' tracing",
				path, dst->env_var_name);

		tr2_dst_trace_disable(dst);
		return 0;
	}

	sa.sun_family = AF_UNIX;
	strlcpy(sa.sun_path, path, sizeof(sa.sun_path));
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ||
	    connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		if (tr2_dst_want_warning())
			warning("trace2: could not connect to socket '%s' for '%s' tracing: %s",
				path, dst->env_var_name, strerror(errno));

		tr2_dst_trace_disable(dst);
		return 0;
	}

	dst->fd = fd;
	dst->need_close = 1;
	dst->initialized = 1;

	return dst->fd;
}
#endif

static void tr2_dst_malformed_warning(struct tr2_dst *dst,
				      const char *tgt_value)
{
	struct strbuf buf = STRBUF_INIT;

	strbuf_addf(&buf, "trace2: unknown trace value for '%s': '%s'",
		    dst->env_var_name, tgt_value);
	strbuf_addstr(
		&buf,
		"\n         If you want to trace into a file, then please set it"
		"\n         to an absolute pathname.");
#ifndef NO_UNIX_SOCKETS
	strbuf_addstr(
		&buf,
		"\n         If you want to trace to a unix domain socket, prefix"
		"\n         the absolute pathname with \"af_unix:\".");
#endif

	warning("%s", buf.buf);

	strbuf_release(&buf);
}

int tr2_dst_get_trace_fd(struct tr2_dst *dst)
{
	const char *tgt_value;

	/* don't open twice */
	if (dst->initialized)
		return dst->fd;

	dst->initialized = 1;

	tgt_value = getenv(dst->env_var_name);

	if (!tgt_value || !strcmp(tgt_value, "") || !strcmp(tgt_value, "0") ||
	    !strcasecmp(tgt_value, "false")) {
		dst->fd = 0;
		return dst->fd;
	}

	if (!strcmp(tgt_value, "1") || !strcasecmp(tgt_value, "true")) {
		dst->fd = STDERR_FILENO;
		return dst->fd;
	}

	if (strlen(tgt_value) == 1 && isdigit(*tgt_value)) {
		dst->fd = atoi(tgt_value);
		return dst->fd;
	}

	if (is_absolute_path(tgt_value))
		return tr2_dst_try_path(dst, tgt_value);

#ifndef NO_UNIX_SOCKETS
	if (!strncmp(tgt_value, PREFIX_AF_UNIX, PREFIX_AF_UNIX_LEN))
		return tr2_dst_try_unix_domain_socket(dst, tgt_value);
#endif

	/* Always warn about malformed values. */
	tr2_dst_malformed_warning(dst, tgt_value);
	tr2_dst_trace_disable(dst);
	return 0;
}

int tr2_dst_trace_want(struct tr2_dst *dst)
{
	return !!tr2_dst_get_trace_fd(dst);
}

void tr2_dst_write_line(struct tr2_dst *dst, struct strbuf *buf_line)
{
	int fd = tr2_dst_get_trace_fd(dst);

	strbuf_complete_line(buf_line); /* ensure final NL on buffer */

	/*
	 * We do not use write_in_full() because we do not want
	 * a short-write to try again.  We are using O_APPEND mode
	 * files and the kernel handles the atomic seek+write. If
	 * another thread or git process is concurrently writing to
	 * this fd or file, our remainder-write may not be contiguous
	 * with our initial write of this message.  And that will
	 * confuse readers.  So just don't bother.
	 *
	 * It is assumed that TRACE2 messages are short enough that
	 * the system can write them in 1 attempt and we won't see
	 * a short-write.
	 *
	 * If we get an IO error, just close the trace dst.
	 */
	if (write(fd, buf_line->buf, buf_line->len) >= 0)
		return;

	if (tr2_dst_want_warning())
		warning("unable to write trace to '%s': %s", dst->env_var_name,
			strerror(errno));
	tr2_dst_trace_disable(dst);
}
