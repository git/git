/*
 * Test backend for the long-running diff process protocol
 * (see diff-process.c and Documentation/gitattributes.adoc).
 *
 * Usage: test-tool diff-process-backend --mode=<mode> [--log=<path>]
 *
 * Implements the server side of the pkt-line handshake and a per-file
 * response loop.  The --mode= switch selects the response shape
 * (success, error, abort, crash, malformed hunks).
 *
 * Per-file request from Git:
 *
 *   packet:          git> command=hunks
 *   packet:          git> pathname=<path>
 *   packet:          git> 0000
 *   packet:          git> OLD_CONTENT
 *   packet:          git> 0000
 *   packet:          git> NEW_CONTENT
 *   packet:          git> 0000
 *
 * Response varies by --mode (default: whole-file):
 *
 *   whole-file   packet: git< hunk 1 <old_lines> 1 <new_lines>
 *   fixed-hunk   packet: git< hunk 5 2 5 2
 *   no-hunks     (no hunk packets)
 *   bad-hunk     packet: git< hunk 999 1 999 1
 *   bad-parse    packet: git< garbage not a hunk
 *   bad-sync     packet: git< hunk 1 2 1 1
 *   overlap      packet: git< hunk 1 5 1 5
 *                packet: git< hunk 3 2 3 2
 *   no-cap       (omits capability=hunks during handshake)
 *   error        (status=error instead of status=success)
 *   abort        (status=abort instead of status=success)
 *   crash        exit(1) before sending any response
 *
 * All non-error/abort modes end with:
 *
 *   packet:          git< 0000
 *   packet:          git< status=success
 *   packet:          git< 0000
 *
 * Each request is logged to --log as:
 *
 *   command=<cmd> pathname=<path> old=<first line> new=<first line>
 */

#include "test-tool.h"
#include "pkt-line.h"
#include "parse-options.h"
#include "strbuf.h"

static FILE *logfile;

enum mode {
	MODE_WHOLE_FILE,
	MODE_FIXED_HUNK,
	MODE_NO_HUNKS,
	MODE_BAD_HUNK,
	MODE_BAD_PARSE,
	MODE_BAD_SYNC,
	MODE_OVERLAP,
	MODE_NO_CAP,
	MODE_ERROR,
	MODE_ABORT,
	MODE_CRASH,
};

static enum mode parse_mode(const char *s)
{
	if (!strcmp(s, "whole-file"))
		return MODE_WHOLE_FILE;
	if (!strcmp(s, "fixed-hunk"))
		return MODE_FIXED_HUNK;
	if (!strcmp(s, "no-hunks"))
		return MODE_NO_HUNKS;
	if (!strcmp(s, "bad-hunk"))
		return MODE_BAD_HUNK;
	if (!strcmp(s, "bad-parse"))
		return MODE_BAD_PARSE;
	if (!strcmp(s, "bad-sync"))
		return MODE_BAD_SYNC;
	if (!strcmp(s, "overlap"))
		return MODE_OVERLAP;
	if (!strcmp(s, "no-cap"))
		return MODE_NO_CAP;
	if (!strcmp(s, "error"))
		return MODE_ERROR;
	if (!strcmp(s, "abort"))
		return MODE_ABORT;
	if (!strcmp(s, "crash"))
		return MODE_CRASH;
	die("unknown --mode=%s", s);
}

/*
 * Read "key=value" packets up to a flush, capturing "command" and
 * "pathname".  Returns 1 if a request was read, 0 on EOF.
 *
 * The first packet uses the gentle variant so that a clean shutdown
 * by Git (EOF) does not produce a spurious "the remote end hung up
 * unexpectedly" on stderr.  Subsequent packets use the non-gentle
 * variant: once inside a request, truncation is a protocol violation
 * and dying loudly is the correct response.
 */
static int read_request_header(char **command, char **pathname)
{
	int first = 1;
	char *line;

	*command = *pathname = NULL;
	for (;;) {
		const char *value;

		if (first) {
			if (packet_read_line_gently(0, NULL, &line) < 0)
				return 0;
			first = 0;
		} else {
			line = packet_read_line(0, NULL);
		}
		if (!line)
			break;
		if (skip_prefix(line, "command=", &value))
			*command = xstrdup(value);
		else if (skip_prefix(line, "pathname=", &value))
			*pathname = xstrdup(value);
	}
	return 1;
}

static size_t count_lines(const struct strbuf *buf)
{
	size_t lines = 0;

	for (size_t i = 0; i < buf->len; i++)
		if (buf->buf[i] == '\n')
			lines++;

	return lines + (buf->len > 0 && buf->buf[buf->len - 1] != '\n');
}

static void send_status(const char *status)
{
	packet_flush(1);
	packet_write_fmt(1, "%s\n", status);
	packet_flush(1);
}

static void respond(enum mode mode,
		    const struct strbuf *old_buf,
		    const struct strbuf *new_buf)
{
	switch (mode) {
	case MODE_ERROR:
		send_status("status=error");
		return;
	case MODE_ABORT:
		send_status("status=abort");
		return;
	case MODE_CRASH:
		exit(1);
	case MODE_FIXED_HUNK:
		packet_write_fmt(1, "hunk 5 2 5 2\n");
		break;
	case MODE_BAD_HUNK:
		packet_write_fmt(1, "hunk 999 1 999 1\n");
		break;
	case MODE_BAD_PARSE:
		packet_write_fmt(1, "garbage not a hunk\n");
		break;
	case MODE_BAD_SYNC:
		packet_write_fmt(1, "hunk 1 2 1 1\n");
		break;
	case MODE_OVERLAP:
		packet_write_fmt(1, "hunk 1 5 1 5\n");
		packet_write_fmt(1, "hunk 3 2 3 2\n");
		break;
	case MODE_NO_HUNKS:
		break;
	case MODE_NO_CAP:
	case MODE_WHOLE_FILE: {
		size_t old_lines = count_lines(old_buf);
		size_t new_lines = count_lines(new_buf);
		/*
		 * Match git diff output: start=0 when count=0
		 * (empty file side), 1 otherwise.
		 */
		packet_write_fmt(1, "hunk %"PRIuMAX" %"PRIuMAX
				 " %"PRIuMAX" %"PRIuMAX"\n",
				 (uintmax_t)(old_lines ? 1 : 0),
				 (uintmax_t)old_lines,
				 (uintmax_t)(new_lines ? 1 : 0),
				 (uintmax_t)new_lines);
		break;
	}
	}
	send_status("status=success");
}

static void command_loop(enum mode mode)
{
	for (;;) {
		char *command = NULL, *pathname = NULL;
		struct strbuf obuf = STRBUF_INIT;
		struct strbuf nbuf = STRBUF_INIT;

		if (!read_request_header(&command, &pathname))
			break; /* EOF: Git closed its end */

		read_packetized_to_strbuf(0, &obuf, 0);
		read_packetized_to_strbuf(0, &nbuf, 0);

		if (logfile) {
			fprintf(logfile,
				"command=%s pathname=%s old=%.*s new=%.*s\n",
				command ? command : "(none)",
				pathname ? pathname : "(none)",
				(int)(strchrnul(obuf.buf, '\n') - obuf.buf),
				obuf.buf,
				(int)(strchrnul(nbuf.buf, '\n') - nbuf.buf),
				nbuf.buf);
			fflush(logfile);
		}

		respond(mode, &obuf, &nbuf);

		free(command);
		free(pathname);
		strbuf_release(&obuf);
		strbuf_release(&nbuf);
	}
}

static void handshake(enum mode mode)
{
	char *line;

	line = packet_read_line(0, NULL);
	if (!line || strcmp(line, "git-diff-client"))
		die("bad welcome: '%s'", line ? line : "(eof)");
	line = packet_read_line(0, NULL);
	if (!line || strcmp(line, "version=1"))
		die("bad version: '%s'", line ? line : "(eof)");
	if (packet_read_line(0, NULL))
		die("expected flush after version");

	packet_write_fmt(1, "git-diff-server\n");
	packet_write_fmt(1, "version=1\n");
	packet_flush(1);

	/* Drain capabilities advertised by Git */
	while ((line = packet_read_line(0, NULL)))
		; /* drain */

	/* Respond with our capabilities (or none for no-cap mode) */
	if (mode != MODE_NO_CAP)
		packet_write_fmt(1, "capability=hunks\n");
	packet_flush(1);
}

static const char *const usage_str[] = {
	"test-tool diff-process-backend --mode=<mode> [--log=<path>]",
	NULL
};

int cmd__diff_process_backend(int argc, const char **argv)
{
	const char *mode_str = NULL, *log_path = NULL;
	enum mode mode = MODE_WHOLE_FILE;
	struct option options[] = {
		OPT_STRING(0, "mode", &mode_str, "mode",
			   "response shape: whole-file (default), fixed-hunk,"
			   " no-hunks, bad-hunk, bad-sync, overlap, error,"
			   " abort, crash"),
		OPT_STRING(0, "log", &log_path, "path",
			   "append per-request summary to this file"),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, options, usage_str, 0);
	if (argc)
		usage_with_options(usage_str, options);

	if (mode_str)
		mode = parse_mode(mode_str);

	if (log_path) {
		logfile = fopen(log_path, "a");
		if (!logfile)
			die_errno("failed to open log '%s'", log_path);
	}

	handshake(mode);
	command_loop(mode);

	if (logfile && fclose(logfile))
		die_errno("error closing log");
	return 0;
}
