/*
 * Test process implementing the diff process protocol (diff.<driver>.process).
 *
 * Speaks the long-running process protocol over stdin/stdout and
 * answers command=hunks-by-oid requests from the blob object names
 * alone; no content is exchanged.  The --mode= switch selects the
 * response shape:
 *
 *   oid-fixed         packet: git< hunk 5 2 5 2
 *   oid-equal         packet: git< status=success (zero hunks: equivalent)
 *   oid-need-content  packet: git< status=need-content
 *   oid-empty         packet: git< hunk 0 0 1 2 (empty old side)
 *
 * and the adversarial shapes the protocol error paths are tested
 * with:
 *
 *   oid-trailing        a hunk line with a trailing token to ignore
 *   oid-malformed       a hunk line that does not parse
 *   oid-huge            coordinates far past the end of any test blob
 *   oid-erange          a count too large for any long
 *   oid-overlap         two hunks out of order
 *   oid-misaligned      two hunks whose unchanged runs differ in length
 *   oid-badstart        a start of 0 paired with a nonzero count
 *   oid-unknown-status  status=frobnicate
 *   oid-abort           status=abort
 *   oid-bare-status     a status packet without the hunk-section flush
 *   oid-empty-packet    an empty packet (0004) inside the hunk section
 *   oid-crash           one hunk line, then exit with no flush or status
 *   oid-garbage         raw non-pkt-line bytes, then exit
 *   cap-none            handshake announcing no capability at all
 *
 * Success responses end with:
 *
 *   packet:          git< 0000
 *   packet:          git< status=success
 *   packet:          git< 0000
 *
 * Each request is logged to --log as:
 *
 *   command=<cmd> pathname=<path> old-oid=<hex> new-oid=<hex>
 */

#include "test-tool.h"
#include "pkt-line.h"
#include "parse-options.h"
#include "strbuf.h"

static FILE *logfile;

enum mode {
	MODE_OID_FIXED,
	MODE_OID_EQUAL,
	MODE_OID_NEED_CONTENT,
	MODE_OID_EMPTY,
	MODE_OID_TRAILING,
	MODE_OID_MALFORMED,
	MODE_OID_HUGE,
	MODE_OID_ERANGE,
	MODE_OID_OVERLAP,
	MODE_OID_MISALIGNED,
	MODE_OID_BADSTART,
	MODE_OID_UNKNOWN_STATUS,
	MODE_OID_ABORT,
	MODE_OID_BARE_STATUS,
	MODE_OID_EMPTY_PACKET,
	MODE_OID_CRASH,
	MODE_OID_GARBAGE,
	MODE_CAP_NONE,
};

static enum mode parse_mode(const char *s)
{
	if (!strcmp(s, "oid-fixed"))
		return MODE_OID_FIXED;
	if (!strcmp(s, "oid-equal"))
		return MODE_OID_EQUAL;
	if (!strcmp(s, "oid-need-content"))
		return MODE_OID_NEED_CONTENT;
	if (!strcmp(s, "oid-empty"))
		return MODE_OID_EMPTY;
	if (!strcmp(s, "oid-trailing"))
		return MODE_OID_TRAILING;
	if (!strcmp(s, "oid-malformed"))
		return MODE_OID_MALFORMED;
	if (!strcmp(s, "oid-huge"))
		return MODE_OID_HUGE;
	if (!strcmp(s, "oid-erange"))
		return MODE_OID_ERANGE;
	if (!strcmp(s, "oid-overlap"))
		return MODE_OID_OVERLAP;
	if (!strcmp(s, "oid-misaligned"))
		return MODE_OID_MISALIGNED;
	if (!strcmp(s, "oid-badstart"))
		return MODE_OID_BADSTART;
	if (!strcmp(s, "oid-unknown-status"))
		return MODE_OID_UNKNOWN_STATUS;
	if (!strcmp(s, "oid-abort"))
		return MODE_OID_ABORT;
	if (!strcmp(s, "oid-bare-status"))
		return MODE_OID_BARE_STATUS;
	if (!strcmp(s, "oid-empty-packet"))
		return MODE_OID_EMPTY_PACKET;
	if (!strcmp(s, "oid-crash"))
		return MODE_OID_CRASH;
	if (!strcmp(s, "oid-garbage"))
		return MODE_OID_GARBAGE;
	if (!strcmp(s, "cap-none"))
		return MODE_CAP_NONE;
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
static int read_request_header(char **command, char **pathname,
			       char **old_oid, char **new_oid)
{
	int first = 1;
	char *line;

	*command = *pathname = *old_oid = *new_oid = NULL;
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
		else if (skip_prefix(line, "old-oid=", &value))
			*old_oid = xstrdup(value);
		else if (skip_prefix(line, "new-oid=", &value))
			*new_oid = xstrdup(value);
	}
	return 1;
}

static void send_status(const char *status)
{
	packet_flush(1);
	packet_write_fmt(1, "%s\n", status);
	packet_flush(1);
}

static void command_loop(enum mode mode)
{
	for (;;) {
		char *command = NULL, *pathname = NULL;
		char *old_oid = NULL, *new_oid = NULL;

		if (!read_request_header(&command, &pathname,
					 &old_oid, &new_oid))
			break; /* EOF: Git closed its end */

		if (!command || strcmp(command, "hunks-by-oid"))
			die("unexpected command: '%s'",
			    command ? command : "(none)");

		if (logfile) {
			fprintf(logfile,
				"command=%s pathname=%s old-oid=%s new-oid=%s\n",
				command,
				pathname ? pathname : "(none)",
				old_oid ? old_oid : "(none)",
				new_oid ? new_oid : "(none)");
			fflush(logfile);
		}

		switch (mode) {
		case MODE_OID_FIXED:
			packet_write_fmt(1, "hunk 5 2 5 2\n");
			send_status("status=success");
			break;
		case MODE_OID_EQUAL:
			send_status("status=success");
			break;
		case MODE_OID_EMPTY:
			/*
			 * An empty old side: the "git diff" convention
			 * addresses it with a start of 0 and a count of 0.
			 * Claims two lines added, fewer than the builtin
			 * would show, so the answer is observable.
			 */
			packet_write_fmt(1, "hunk 0 0 1 2\n");
			send_status("status=success");
			break;
		case MODE_OID_TRAILING:
			/*
			 * Git must ignore trailing space-separated tokens
			 * on a hunk line (the appendability rule), so this
			 * must behave exactly like oid-fixed.
			 */
			packet_write_fmt(1, "hunk 5 2 5 2 moved=yes\n");
			send_status("status=success");
			break;
		case MODE_OID_MALFORMED:
			packet_write_fmt(1, "hunk five two 5 2\n");
			send_status("status=success");
			break;
		case MODE_OID_HUGE:
			/*
			 * In-range for int32 (and for a 32-bit long), so
			 * only the blob-size bound can reject it.
			 */
			packet_write_fmt(1, "hunk 1 1000000000 1 1000000000\n");
			send_status("status=success");
			break;
		case MODE_OID_ERANGE:
			/* Overflows strtol() even where long is 64-bit. */
			packet_write_fmt(1, "hunk 1 99999999999999999999 1 1\n");
			send_status("status=success");
			break;
		case MODE_OID_OVERLAP:
			packet_write_fmt(1, "hunk 3 2 3 2\n");
			packet_write_fmt(1, "hunk 2 2 2 2\n");
			send_status("status=success");
			break;
		case MODE_OID_MISALIGNED:
			packet_write_fmt(1, "hunk 2 1 2 1\n");
			packet_write_fmt(1, "hunk 5 1 6 1\n");
			send_status("status=success");
			break;
		case MODE_OID_BADSTART:
			/*
			 * A start of 0 names an empty side, so a nonzero
			 * count beside it names no line; the coordinate is
			 * rejected per pair while the process stays alive.
			 */
			packet_write_fmt(1, "hunk 0 2 1 2\n");
			send_status("status=success");
			break;
		case MODE_OID_UNKNOWN_STATUS:
			send_status("status=frobnicate");
			break;
		case MODE_OID_ABORT:
			send_status("status=abort");
			break;
		case MODE_OID_BARE_STATUS:
			/* No hunk-section flush: a protocol violation. */
			packet_write_fmt(1, "status=success\n");
			packet_flush(1);
			break;
		case MODE_OID_EMPTY_PACKET:
			/*
			 * An empty packet is not a flush; inside the hunk
			 * section it is a protocol violation.
			 */
			if (write(1, "0004", 4) < 0)
				die_errno("write empty packet");
			send_status("status=success");
			break;
		case MODE_OID_CRASH:
			packet_write_fmt(1, "hunk 5 2 5 2\n");
			exit(0);
		case MODE_OID_GARBAGE:
			if (write(1, "@@@@ not a pkt-line @@@@", 24) < 0)
				die_errno("write garbage");
			exit(0);
		default:
			send_status("status=need-content");
			break;
		}

		free(command);
		free(pathname);
		free(old_oid);
		free(new_oid);
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

	if (mode != MODE_CAP_NONE)
		packet_write_fmt(1, "capability=hunks-by-oid\n");
	packet_flush(1);
}

static const char *const usage_str[] = {
	"test-tool diff-process-backend --mode=<mode> [--log=<path>]",
	NULL
};

int cmd__diff_process_backend(int argc, const char **argv)
{
	const char *mode_str = NULL, *log_path = NULL;
	enum mode mode = MODE_OID_FIXED;
	struct option options[] = {
		OPT_STRING(0, "mode", &mode_str, "mode",
			   "response shape (default oid-fixed);"
			   " see the file header for the full list of modes"),
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
