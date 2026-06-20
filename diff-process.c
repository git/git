/*
 * Diff process backend: communicates with a long-running external
 * tool via the pkt-line protocol to obtain custom line-matching
 * results.  The tool controls which lines are marked as changed
 * while the display shows the file content (after any textconv
 * transformation, if configured).
 *
 * Protocol: pkt-line over stdin/stdout, following the pattern of
 * the long-running filter process protocol (see convert.c).
 *
 * Handshake:
 *   git> git-diff-client / version=1 / flush
 *   tool< git-diff-server / version=1 / flush
 *   git> capability=hunks / flush
 *   tool< capability=hunks / flush
 *
 * Per-file:
 *   git> command=hunks / pathname=<path> / flush
 *   git> <old content packetized> / flush
 *   git> <new content packetized> / flush
 *   tool< hunk <old_start> <old_count> <new_start> <new_count>
 *   tool< ... / flush
 *   tool< status=success / flush
 *
 * When the tool returns no hunks with status=success, it considers
 * the files equivalent.  Git will skip the diff for that file.
 */

#include "git-compat-util.h"
#include "diff-process.h"
#include "diff.h"
#include "gettext.h"
#include "repository.h"
#include "sigchain.h"
#include "userdiff.h"
#include "sub-process.h"
#include "pkt-line.h"
#include "strbuf.h"
#include "xdiff/xdiff.h"

#define CAP_HUNKS (1u << 0)

struct diff_subprocess {
	struct subprocess_entry subprocess;
	unsigned int supported_capabilities;
};

static int start_diff_process_fn(struct subprocess_entry *subprocess)
{
	static int versions[] = { 1, 0 };
	static struct subprocess_capability capabilities[] = {
		{ "hunks", CAP_HUNKS },
		{ NULL, 0 }
	};
	struct diff_subprocess *entry =
		container_of(subprocess, struct diff_subprocess, subprocess);

	return subprocess_handshake(subprocess, "git-diff",
				    versions, NULL,
				    capabilities,
				    &entry->supported_capabilities);
}

static struct diff_subprocess *get_or_launch_process(
		struct userdiff_driver *drv)
{
	struct diff_subprocess *entry;

	if (drv->diff_subprocess)
		return drv->diff_subprocess;

	entry = xcalloc(1, sizeof(*entry));
	if (subprocess_start_command(&entry->subprocess, drv->process,
				     start_diff_process_fn)) {
		free(entry);
		drv->diff_process_failed = 1;
		return NULL;
	}

	drv->diff_subprocess = entry;
	return entry;
}

static int send_file_content(int fd, const char *buf, long size)
{
	int ret = 0;

	if (size < 0)
		return -1;
	if (size > 0)
		ret = write_packetized_from_buf_no_flush(buf, size, fd);
	if (ret)
		return ret;
	return packet_flush_gently(fd);
}

static int parse_hunk_line(const char *line, struct xdl_hunk *hunk)
{
	char *end;

	/*
	 * Format: "hunk <old_start> <old_count> <new_start> <new_count>"
	 * All numbers must be non-negative decimal with no leading
	 * whitespace or sign characters.
	 */
	if (!skip_prefix(line, "hunk ", &line))
		return -1;

	if (!isdigit(*line))
		return -1;
	errno = 0;
	hunk->old_start = strtol(line, &end, 10);
	if (errno || end == line || *end++ != ' ')
		return -1;
	line = end;

	if (!isdigit(*line))
		return -1;
	errno = 0;
	hunk->old_count = strtol(line, &end, 10);
	if (errno || end == line || *end++ != ' ')
		return -1;
	line = end;

	if (!isdigit(*line))
		return -1;
	errno = 0;
	hunk->new_start = strtol(line, &end, 10);
	if (errno || end == line || *end++ != ' ')
		return -1;
	line = end;

	if (!isdigit(*line))
		return -1;
	errno = 0;
	hunk->new_count = strtol(line, &end, 10);
	if (errno || end == line || *end != '\0')
		return -1;

	/*
	 * git diff emits start=0 when count=0 (empty file side).
	 * Normalize to 1-based so downstream validation can assume start >= 1.
	 */
	if (!hunk->old_count && !hunk->old_start)
		hunk->old_start = 1;
	if (!hunk->new_count && !hunk->new_start)
		hunk->new_start = 1;

	return 0;
}

static enum diff_process_result get_hunks(
		struct userdiff_driver *drv,
		const char *path,
		const char *old_buf, long old_size,
		const char *new_buf, long new_size,
		struct xdl_hunk **hunks_out,
		size_t *nr_hunks_out)
{
	struct diff_subprocess *backend;
	struct child_process *process;
	int fd_in, fd_out;
	struct strbuf status = STRBUF_INIT;
	struct xdl_hunk *hunks = NULL;
	struct xdl_hunk hunk;
	size_t nr_hunks = 0, alloc_hunks = 0;
	int len;
	char *line;

	backend = get_or_launch_process(drv);
	if (!backend)
		return DIFF_PROCESS_ERROR;

	if (!(backend->supported_capabilities & CAP_HUNKS))
		return DIFF_PROCESS_SKIP;

	process = subprocess_get_child_process(&backend->subprocess);
	fd_in = process->in;
	fd_out = process->out;

	sigchain_push(SIGPIPE, SIG_IGN);

	/* Send request */
	if (packet_write_fmt_gently(fd_in, "command=hunks\n") ||
	    packet_write_fmt_gently(fd_in, "pathname=%s\n", path) ||
	    packet_flush_gently(fd_in))
		goto comm_error;

	/* Send old file content */
	if (send_file_content(fd_in, old_buf, old_size))
		goto comm_error;

	/* Send new file content */
	if (send_file_content(fd_in, new_buf, new_size))
		goto comm_error;

	/* Read hunks until flush packet */
	while ((len = packet_read_line_gently(fd_out, NULL, &line)) >= 0 &&
	       line) {
		if (parse_hunk_line(line, &hunk) < 0)
			goto comm_error;
		ALLOC_GROW(hunks, nr_hunks + 1, alloc_hunks);
		hunks[nr_hunks++] = hunk;
	}
	if (len < 0)
		goto comm_error;

	/* Read status */
	if (subprocess_read_status(fd_out, &status))
		goto comm_error;

	if (!strcmp(status.buf, "success")) {
		*hunks_out = hunks;
		*nr_hunks_out = nr_hunks;
		strbuf_release(&status);
		sigchain_pop(SIGPIPE);
		return DIFF_PROCESS_OK;
	}

	if (!strcmp(status.buf, "abort")) {
		/*
		 * The tool voluntarily withdrew: stop sending requests
		 * but do not warn (this is not a failure).
		 */
		backend->supported_capabilities &= ~CAP_HUNKS;
		free(hunks);
		strbuf_release(&status);
		sigchain_pop(SIGPIPE);
		return DIFF_PROCESS_SKIP;
	}

	/* status=error or unknown status */
	free(hunks);
	strbuf_release(&status);
	sigchain_pop(SIGPIPE);
	return DIFF_PROCESS_ERROR;

comm_error:
	/*
	 * Communication failure (broken pipe, malformed response).
	 * Tear down the process and mark as failed so we do not
	 * retry on every subsequent file.
	 */
	drv->diff_process_failed = 1;
	drv->diff_subprocess = NULL;
	subprocess_stop_command(&backend->subprocess);
	free(backend);
	free(hunks);
	strbuf_release(&status);
	sigchain_pop(SIGPIPE);
	return DIFF_PROCESS_ERROR;
}

enum diff_process_result diff_process_fill_hunks(
		struct diff_options *diffopt,
		const char *path,
		const mmfile_t *file_a,
		const mmfile_t *file_b,
		xpparam_t *xpp)
{
	struct userdiff_driver *drv;
	struct xdl_hunk *ext_hunks = NULL;
	size_t nr = 0;
	enum diff_process_result res;

	if (!diffopt || !path)
		return DIFF_PROCESS_SKIP;
	if (diffopt->flags.no_diff_process || diffopt->ignore_driver_algorithm)
		return DIFF_PROCESS_SKIP;

	drv = userdiff_find_by_path(diffopt->repo->index, path);
	if (!drv || !drv->process)
		return DIFF_PROCESS_SKIP;
	if (drv->diff_process_failed)
		return DIFF_PROCESS_SKIP;

	res = get_hunks(drv, path,
			file_a->ptr, file_a->size,
			file_b->ptr, file_b->size,
			&ext_hunks, &nr);
	if (res == DIFF_PROCESS_OK) {
		if (!nr) {
			free(ext_hunks);
			return DIFF_PROCESS_EQUIVALENT;
		}
		xpp->external_hunks = ext_hunks;
		xpp->external_hunks_nr = nr;
		return DIFF_PROCESS_OK;
	}
	if (res == DIFF_PROCESS_ERROR) {
		warning(_("diff process '%s' failed for '%s',"
			  " falling back to builtin diff"),
			drv->process, path);
		return DIFF_PROCESS_ERROR;
	}
	return DIFF_PROCESS_SKIP;
}
