/*
 * The process provider of the hunk provider interface: consult a
 * long-running external process via the pkt-line protocol for the
 * hunks of a blob pair.  The process answers from the pair's object
 * names alone: it can serve a persistent cache keyed on the pair, or
 * fetch the blobs from the repository itself (e.g. via "git cat-file
 * --batch") and compute its own notion of which lines changed.  The
 * provider sits at the head of its repository's chain and gates
 * itself per request; its state is the repository's pool of running
 * processes, one per configured command, stopped when the provider
 * is released.
 *
 * Protocol: pkt-line over stdin/stdout, following the pattern of
 * the long-running filter process protocol (see convert.c).
 *
 * Handshake:
 *   git> git-diff-client / version=1 / flush
 *   process< git-diff-server / version=1 / flush
 *   git> capability=hunks-by-oid / flush
 *   process< capability=hunks-by-oid / flush
 *
 * Per-pair, when both sides are stored blobs:
 *   git> command=hunks-by-oid / pathname=<path>
 *   git> old-oid=<hex> / new-oid=<hex> / flush
 *   process< hunk <old_start> <old_count> <new_start> <new_count>
 *   process< ... / flush
 *   process< status=success / flush
 *
 * No content is sent.  Because Git holds no content for the exchange,
 * the answer is used as the process sent it: the hunks are not re-run
 * through xdiff's compaction, and a status=success response with zero
 * hunks asserts that the blobs are equivalent, including their
 * trailing newlines.  A process that cannot answer from the object names
 * (or cannot rule out a trailing-newline-only difference) responds
 * status=need-content; the pair then gets the builtin answer, served
 * from the diff-hunks store or computed.  A later
 * protocol extension can define a content-carrying request for such
 * processes and for sides that are not stored blobs.
 */

#include "git-compat-util.h"
#include "diff.h"
#include "diff-provider-internal.h"
#include "gettext.h"
#include "hex.h"
#include "odb.h"
#include "repository.h"
#include "sigchain.h"
#include "userdiff.h"
#include "sub-process.h"
#include "pkt-line.h"
#include "strbuf.h"

#define CAP_OID_HUNKS (1u << 0)

/*
 * The provider's state: the repository's diff processes, keyed by
 * their command string, so drivers that configure the same command
 * share one process.  An entry whose process failed stays in the
 * pool with the failed bit set, so the command is not retried while
 * the entry lives; the pool and its entries last until the provider
 * is released.
 */
struct diff_process_state {
	struct hashmap subprocesses;
};

struct diff_subprocess {
	struct subprocess_entry subprocess;
	/*
	 * Owns the string subprocess.cmd and the hashmap key borrow: the
	 * entry outlives the userdiff config a re-read may replace.
	 */
	char *cmd;
	unsigned int supported_capabilities;
	unsigned failed : 1;
};

static int start_diff_process_fn(struct subprocess_entry *subprocess)
{
	static int versions[] = { 1, 0 };
	static struct subprocess_capability capabilities[] = {
		{ "hunks-by-oid", CAP_OID_HUNKS },
		{ NULL, 0 }
	};
	struct diff_subprocess *entry =
		container_of(subprocess, struct diff_subprocess, subprocess);

	return subprocess_handshake(subprocess, "git-diff",
				    versions, NULL,
				    capabilities,
				    &entry->supported_capabilities);
}

/*
 * The pool entry for a command, or NULL when its process fails to
 * start here: the failure leaves a failed entry in the pool, so only
 * the request that observed it maps it to an error and later
 * requests pass the provider by.
 */
static struct diff_subprocess *get_or_launch_process(
		struct diff_process_state *state,
		struct userdiff_driver *drv)
{
	struct subprocess_entry *running;
	struct diff_subprocess *entry;

	running = subprocess_find_entry(&state->subprocesses, drv->process);
	if (running) {
		entry = container_of(running, struct diff_subprocess,
				     subprocess);
		return entry->failed ? NULL : entry;
	}

	entry = xcalloc(1, sizeof(*entry));
	entry->cmd = xstrdup(drv->process);
	if (subprocess_start_command(&entry->subprocess, entry->cmd,
				     start_diff_process_fn))
		entry->failed = 1;
	hashmap_entry_init(&entry->subprocess.ent, strhash(entry->cmd));
	hashmap_add(&state->subprocesses, &entry->subprocess.ent);
	if (entry->failed) {
		warning(_("diff process '%s' failed to start;"
			  " using the builtin diff"), drv->process);
		return NULL;
	}
	return entry;
}

/*
 * A hunk in the diff process's presentation coordinates: the line
 * numbering it reports over the protocol.  Kept distinct from struct
 * xdl_hunk (xdiff's coordinates) so that only translated hunks ever
 * reach a consumer; diff_process_hunk_to_xdl() is the single
 * crossing point.
 */
struct diff_process_hunk {
	long old_start, old_count;
	long new_start, new_count;
};

/*
 * Parse one non-negative decimal field of a hunk line into *out and
 * advance *line past it.  Fields must be plain decimal with no leading
 * whitespace or sign (isdigit() takes an unsigned char to stay defined
 * for high-bit bytes).  The first three fields are followed by a single
 * space; the last (is_last) is followed by end-of-string or a space.
 * Trailing space-separated tokens after the last field are allowed and
 * ignored, so a future protocol version can append fields (e.g. a
 * "moved" marker) without an older Git rejecting the line, mirroring
 * the request-side rule that processes ignore unknown keys.
 *
 * A value that overflows strtol() is not a parse failure: the line is
 * well-formed, so the stream stays in protocol sync.  It is reported
 * through *out_of_range, and the caller skips the pair the same way
 * it skips any other out-of-range coordinate.
 */
static int parse_hunk_field(const char **line, long *out, int is_last,
			    int *out_of_range)
{
	const char *p = *line;
	char *end;

	if (!isdigit((unsigned char)*p))
		return -1;
	errno = 0;
	*out = strtol(p, &end, 10);
	if (end == p)
		return -1;
	if (errno == ERANGE)
		*out_of_range = 1;
	else if (errno)
		return -1;
	if (is_last) {
		if (*end != '\0' && *end != ' ')
			return -1;
	} else {
		if (*end != ' ')
			return -1;
		end++;
	}
	*line = end;
	return 0;
}

static int parse_hunk_line(const char *line,
			   struct diff_process_hunk *presented,
			   int *out_of_range)
{
	*out_of_range = 0;
	/* Format: "hunk <old_start> <old_count> <new_start> <new_count>" */
	if (!skip_prefix(line, "hunk ", &line))
		return -1;
	if (parse_hunk_field(&line, &presented->old_start, 0, out_of_range) ||
	    parse_hunk_field(&line, &presented->old_count, 0, out_of_range) ||
	    parse_hunk_field(&line, &presented->new_start, 0, out_of_range) ||
	    parse_hunk_field(&line, &presented->new_count, 1, out_of_range))
		return -1;
	return 0;
}

/*
 * Translate a hunk from the diff process's presentation coordinates
 * into xdiff's.
 *
 * Protocol starts are already 1-based positions (the line a change
 * sits before), the same numbering xdiff uses, so the only adjustment
 * is for an empty file side: "git diff" addresses it with a start of 0
 * and a count of 0 (e.g. "0 0 1 5" adds five lines to an empty old
 * side), and since xdiff uses start-1 as an array index that 0 becomes
 * 1 here.  This is NOT the full inverse of xdl_emit_hunk_hdr()
 * (xdiff/xutils.c): that emitter shifts a count-0 range to start-1 for
 * the displayed "@@" header, but the protocol keeps the unshifted
 * 1-based position for a mid-file insert or delete.  This is the single
 * point where presentation coordinates become xdiff coordinates, so
 * any consumer of these coordinates may assume 1-based starts.
 *
 * Returns -1 for a start of 0 paired with a nonzero count, which names
 * no line in either coordinate system.  (parse_hunk_line() already
 * guarantees non-negative starts and counts.)
 */
static int diff_process_hunk_to_xdl(const struct diff_process_hunk *presented,
				    struct xdl_hunk *xdl)
{
	long old_start = presented->old_start;
	long new_start = presented->new_start;

	if ((!old_start && presented->old_count) ||
	    (!new_start && presented->new_count))
		return -1;
	if (!old_start)
		old_start = 1;
	if (!new_start)
		new_start = 1;

	xdl->old_start = old_start;
	xdl->old_count = presented->old_count;
	xdl->new_start = new_start;
	xdl->new_count = presented->new_count;
	return 0;
}

/*
 * Validate the process's hunks (already in xdiff coordinates) before they
 * bypass the diff algorithm.  The content-independent rules (in-order,
 * non-overlapping, lockstep-aligned, int32-bounded coordinates) are the
 * provider interface's shared rule, diff_provider_check_hunk(); this
 * function adds the two checks that need the blobs' line counts (a hunk
 * past the end of a file, the run after the last hunk) and the
 * per-rule diagnostics naming the process.  On a bad response we warn
 * and the caller falls back to the builtin diff.  Returns 0 if valid,
 * -1 (after warning) otherwise.
 *
 * old_lines/new_lines bound the line count of each side, or are
 * negative when no bound is known.  An oid-only answer arrives without
 * content, so its caller passes upper bounds derived from the blobs'
 * byte sizes, which caps coordinate magnitude but cannot support the
 * run-after-the-last-hunk check: that one compares exact line counts,
 * so it runs only when lines_exact is set, which no caller does today.
 * It is kept for a content-carrying request, whose loaded buffers
 * would provide exact counts.
 */
static int validate_external_hunks(const struct xdl_hunk *hunks, size_t nr,
				   long old_lines, long new_lines,
				   int lines_exact,
				   const char *process, const char *path)
{
	struct diff_provider_hunks_check c = { 0 };
	size_t i;

	for (i = 0; i < nr; i++) {
		const struct xdl_hunk *h = &hunks[i];

		if (old_lines >= 0 &&
		    (h->old_count > old_lines - h->old_start + 1 ||
		     h->new_count > new_lines - h->new_start + 1)) {
			warning(_("diff process '%s' returned a hunk past the "
				  "end of '%s'; using the builtin diff"),
				process, path);
			return -1;
		}
		switch (diff_provider_check_hunk(&c, h->old_start,
						  h->old_count, h->new_start,
						  h->new_count)) {
		case DIFF_PROVIDER_HUNKS_OK:
			break;
		case DIFF_PROVIDER_HUNKS_RANGE:
			warning(_("diff process '%s' returned out-of-range "
				  "coordinates for '%s'; using the builtin diff"),
				process, path);
			return -1;
		case DIFF_PROVIDER_HUNKS_OVERLAP:
			warning(_("diff process '%s' returned overlapping hunks "
				  "for '%s'; using the builtin diff"),
				process, path);
			return -1;
		case DIFF_PROVIDER_HUNKS_MISALIGNED:
			warning(_("diff process '%s' returned hunks that leave "
				  "'%s' misaligned; using the builtin diff"),
				process, path);
			return -1;
		}
	}
	if (lines_exact &&
	    old_lines - c.prev_old_end != new_lines - c.prev_new_end) {
		warning(_("diff process '%s' returned hunks that leave '%s' "
			  "misaligned; using the builtin diff"),
			process, path);
		return -1;
	}
	return 0;
}

/*
 * The most lines a blob can hold, from its size alone: every line,
 * even an empty one, costs at least one byte, so a blob of N bytes
 * holds at most N lines.  Returns -1 when the size is unavailable,
 * leaving the response bounded only by the shared int32 rule.  A size
 * beyond INT32_MAX clamps to it, which loses nothing: a coordinate
 * that large fails the shared rule anyway.  In a partial clone the
 * size lookup must not fetch the blob from the promisor remote:
 * validating an answer that exists to avoid loading content must not
 * itself download that content, so a missing blob reads as size
 * unavailable instead.
 */
static long blob_line_cap(struct repository *r, const struct object_id *oid)
{
	size_t size;
	struct object_info oi = OBJECT_INFO_INIT;

	oi.sizep = &size;
	if (odb_read_object_info_extended(r->objects, oid, &oi,
					  OBJECT_INFO_SKIP_FETCH_OBJECT) < 0)
		return -1;
	if (size > INT32_MAX)
		return INT32_MAX;
	return (long)size;
}

/*
 * The driver whose process a consultation for path would ask, or NULL
 * when none applies (no driver, process not allowed, or xpp carries
 * options the process is never told about).  Needs no content, so
 * the driver is picked before any blob is loaded.
 */
static struct userdiff_driver *diff_process_driver(struct diff_options *diffopt,
						   const char *path,
						   const xpparam_t *xpp)
{
	struct userdiff_driver *drv;

	if (!diffopt || !path)
		return NULL;
	if (!diffopt->flags.allow_diff_process || diffopt->ignore_driver_algorithm)
		return NULL;
	/*
	 * Whitespace-ignoring, regex-ignore (-I) and anchored options
	 * change which lines count as different, but the process is never
	 * told about them, so its hunks could not honor them.  A forced
	 * diff algorithm (an option or configured algorithm setting)
	 * requests a specific builtin computation, which an
	 * authoritative answer would override.  Rather than silently
	 * override the user's request, fall back to the builtin diff,
	 * which does honor these flags.  Key this off xpp (the
	 * parameters this diff actually runs with) rather than diffopt,
	 * so a caller like blame, which keeps its algorithm and
	 * whitespace flags outside diffopt, is covered without a
	 * separate guard of its own.
	 */
	if ((xpp->flags & (XDF_WHITESPACE_FLAGS | XDF_IGNORE_BLANK_LINES |
			   XDF_DIFF_ALGORITHM_MASK)) ||
	    xpp->ignore_regex_nr || xpp->anchors_nr)
		return NULL;

	/*
	 * A path the protocol cannot carry never selects a process: an
	 * embedded newline would let the rest of the path forge further
	 * request keys, and the pathname must fit one packet.  Passing
	 * here keeps the cost local to the path; a failed write would
	 * instead cost the whole command its process.
	 */
	if (strchr(path, '\n') ||
	    strlen(path) > LARGE_PACKET_DATA_MAX - strlen("pathname=\n"))
		return NULL;

	drv = userdiff_find_by_path(diffopt->repo->index, path);
	if (!drv || !drv->process)
		return NULL;
	return drv;
}

/*
 * Without content there is no size-derived bound on a response, so cap
 * accumulation at a constant instead.  A response that exceeds the
 * cap is a protocol error: the process is disabled for the rest of
 * the command and the caller falls back to the builtin diff.
 */
#define OID_HUNKS_MAX (1 << 20)

enum diff_process_result {
	DIFF_PROCESS_ERROR = -1, /* failed; caller falls back to builtin */
	DIFF_PROCESS_OK = 0,     /* the process supplied hunks */
	DIFF_PROCESS_SKIP,       /* process did not apply: use builtin */
	DIFF_PROCESS_EQUIVALENT, /* process says files are equivalent */
};

/*
 * Ask drv's diff process to answer the request from the blob pair's
 * object ids alone (the "hunks-by-oid" capability): no content is
 * loaded or sent.  On DIFF_PROCESS_OK the process's hunks are emitted
 * through hunk_cb in 0-based emission coordinates, validated for order,
 * overlap, and lockstep alignment first; because Git holds no content,
 * the answer is used as the process sent it, without xdiff's compaction.
 * DIFF_PROCESS_EQUIVALENT means the process asserts the pair equal.
 * DIFF_PROCESS_SKIP covers everything that should fall through to the
 * builtin computation: a missing capability, a missing object id, a
 * status=need-content answer, or an invalid response.
 */
static enum diff_process_result diff_process_query_hunks(
		struct diff_process_state *state,
		struct userdiff_driver *drv,
		const struct diff_provider_request *req,
		xdl_emit_hunk_consume_func_t hunk_cb,
		void *cb_data)
{
	const char *path = req->path;
	struct diff_subprocess *entry;
	struct child_process *process;
	int fd_in, fd_out;
	struct packet_reader reader;
	struct strbuf status = STRBUF_INIT;
	struct xdl_hunk *hunks = NULL;
	struct diff_process_hunk presented;
	struct xdl_hunk hunk;
	size_t nr_hunks = 0, alloc_hunks = 0, i;
	int bad_coords = 0;
	long old_cap, new_cap;
	enum diff_process_result res;

	if (!req->old_oid || !req->new_oid)
		return DIFF_PROCESS_SKIP;

	entry = get_or_launch_process(state, drv);
	if (!entry)
		return DIFF_PROCESS_ERROR;
	if (!(entry->supported_capabilities & CAP_OID_HUNKS))
		return DIFF_PROCESS_SKIP;

	process = subprocess_get_child_process(&entry->subprocess);
	fd_in = process->in;
	fd_out = process->out;

	sigchain_push(SIGPIPE, SIG_IGN);

	if (packet_write_fmt_gently(fd_in, "command=hunks-by-oid\n") ||
	    packet_write_fmt_gently(fd_in, "pathname=%s\n", path) ||
	    packet_write_fmt_gently(fd_in, "old-oid=%s\n",
				    oid_to_hex(req->old_oid)) ||
	    packet_write_fmt_gently(fd_in, "new-oid=%s\n",
				    oid_to_hex(req->new_oid)) ||
	    packet_flush_gently(fd_in))
		goto comm_error;

	packet_reader_init(&reader, fd_out, NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_GENTLE_ON_EOF |
			   PACKET_READ_GENTLE_ON_READ_ERROR);
	for (;;) {
		enum packet_read_status rs = packet_reader_read(&reader);
		int out_of_range;

		if (rs == PACKET_READ_FLUSH)
			break;
		/*
		 * Only a hunk line may precede the flush.  EOF and a
		 * malformed frame end the session; an empty packet, which
		 * a length-only read cannot tell from a flush, would
		 * truncate the hunk section here and leave the status
		 * section to poison the next request, so it is a protocol
		 * error too.
		 */
		if (rs != PACKET_READ_NORMAL || !reader.pktlen)
			goto comm_error;
		if (parse_hunk_line(reader.line, &presented,
				    &out_of_range) < 0)
			goto comm_error;
		if (bad_coords)
			continue;
		if (out_of_range ||
		    diff_process_hunk_to_xdl(&presented, &hunk) < 0) {
			/*
			 * Semantically invalid coordinates in a well-formed
			 * response: the stream stays in protocol sync, so
			 * drain the rest and fall back for this file while
			 * keeping the process alive, the same treatment
			 * validate_external_hunks() failures receive.
			 */
			bad_coords = 1;
			continue;
		}
		if (nr_hunks >= OID_HUNKS_MAX) {
			warning(_("diff process '%s' sent too many hunks"
				  " for '%s'; disabling it for the"
				  " remainder of this command"),
				drv->process, path);
			goto disable;
		}
		ALLOC_GROW(hunks, nr_hunks + 1, alloc_hunks);
		hunks[nr_hunks++] = hunk;
	}

	if (subprocess_read_status_gently(fd_out, &status))
		goto comm_error;

	if (!strcmp(status.buf, "success")) {
		if (bad_coords) {
			warning(_("diff process '%s' returned out-of-range "
				  "coordinates for '%s'; using the builtin diff"),
				drv->process, path);
			res = DIFF_PROCESS_SKIP;
			goto out;
		}
		if (!nr_hunks) {
			res = DIFF_PROCESS_EQUIVALENT;
			goto out;
		}
		/*
		 * Bound the coordinates by the blobs' sizes, read from the
		 * object database without loading content.  Either both
		 * bounds hold or neither is applied: a partial bound would
		 * misclassify a response that the other side's size would
		 * have caught.
		 */
		old_cap = blob_line_cap(req->repo, req->old_oid);
		new_cap = blob_line_cap(req->repo, req->new_oid);
		if (old_cap < 0 || new_cap < 0)
			old_cap = new_cap = -1;
		if (validate_external_hunks(hunks, nr_hunks, old_cap, new_cap,
					    0, drv->process, path) < 0) {
			res = DIFF_PROCESS_SKIP;
			goto out;
		}
		/*
		 * Replay in the coordinates a hunk consumer receives from
		 * xdiff's emission: 0-based starts.  The answer is used as
		 * the process sent it; with no content in hand it cannot be
		 * re-run through xdiff's compaction.
		 */
		for (i = 0; i < nr_hunks; i++)
			hunk_cb(hunks[i].old_start - 1, hunks[i].old_count,
				hunks[i].new_start - 1, hunks[i].new_count,
				cb_data);
		res = DIFF_PROCESS_OK;
		goto out;
	}
	if (!strcmp(status.buf, "need-content")) {
		/*
		 * The process cannot answer this pair from its object names;
		 * the caller computes the diff itself.
		 */
		res = DIFF_PROCESS_SKIP;
		goto out;
	}
	if (!strcmp(status.buf, "abort")) {
		/* The process withdrew: stop asking it for this session. */
		entry->supported_capabilities &= ~CAP_OID_HUNKS;
		res = DIFF_PROCESS_SKIP;
		goto out;
	}
	/*
	 * An unrecognized status is a protocol error, not a per-pair
	 * failure: this Git did not request anything it does not know,
	 * so the process is answering some other protocol, and asking
	 * it again would warn on every pair of the traversal.
	 */
	warning(_("diff process '%s' sent unrecognized status '%s' for "
		  "'%s'; disabling it for the remainder of this command"),
		drv->process, status.buf, path);
	goto disable;
out:
	free(hunks);
	strbuf_release(&status);
	sigchain_pop(SIGPIPE);
	return res;

comm_error:
	warning(_("diff process '%s' failed for '%s'; disabling it"
		  " for the remainder of this command"),
		drv->process, path);
disable:
	subprocess_stop_command(&entry->subprocess);
	entry->failed = 1;
	free(hunks);
	strbuf_release(&status);
	sigchain_pop(SIGPIPE);
	return DIFF_PROCESS_ERROR;
}

/*
 * The process outranks every later provider through its chain
 * position: when it answers, the walk ends, so no later provider
 * serves the pair, and an answered pair is never recorded.  When it
 * does not answer (it defers with need-content, lacks the
 * capability, or failed), the caller computes the builtin diff for
 * that pair.  The store holds builtin results and nothing else, so
 * an identity answer for such a pair equals what the caller would
 * compute.  Every non-answer is therefore a pass: a refusal would
 * suppress that equal answer, and would keep a warming run from
 * recording the builtin result the caller computes anyway.
 */
static enum diff_provider_disposition
diff_process_consult(struct diff_provider *provider,
		     const struct diff_provider_request *req,
		     diff_provider_fill_fn fill UNUSED, void *fill_data UNUSED,
		     xdl_emit_hunk_consume_func_t hunk_cb, void *cb_data)
{
	struct diff_process_state *state = provider->state;
	struct userdiff_driver *drv;
	struct subprocess_entry *running;

	drv = diff_process_driver(req->diffopt, req->path, req->xpp);
	if (!drv)
		return DIFF_PROVIDER_DISP_PASS;
	running = subprocess_find_entry(&state->subprocesses, drv->process);
	if (running && container_of(running, struct diff_subprocess,
				    subprocess)->failed)
		return DIFF_PROVIDER_DISP_PASS;

	switch (diff_process_query_hunks(state, drv, req,
					 hunk_cb, cb_data)) {
	case DIFF_PROCESS_OK:
	case DIFF_PROCESS_EQUIVALENT:
		return DIFF_PROVIDER_DISP_ANSWERED;
	case DIFF_PROCESS_SKIP:
	case DIFF_PROCESS_ERROR:
		break;
	}
	return DIFF_PROVIDER_DISP_PASS;
}

static void diff_process_release(struct diff_provider *provider)
{
	struct diff_process_state *state = provider->state;
	struct hashmap_iter iter;
	struct diff_subprocess *entry;

	/* A failed entry's process is already stopped or never ran. */
	hashmap_for_each_entry(&state->subprocesses, &iter, entry,
			       subprocess.ent) {
		if (!entry->failed)
			subprocess_stop_command(&entry->subprocess);
		free(entry->cmd);
	}
	hashmap_clear_and_free(&state->subprocesses,
			       struct diff_subprocess, subprocess.ent);
	free(state);
}

struct diff_provider *diff_process_provider_new(void)
{
	struct diff_process_state *state = xcalloc(1, sizeof(*state));
	struct diff_provider *p = xcalloc(1, sizeof(*p));

	hashmap_init(&state->subprocesses, cmd2process_cmp, NULL, 0);
	p->consult = diff_process_consult;
	p->release = diff_process_release;
	p->state = state;
	return p;
}
