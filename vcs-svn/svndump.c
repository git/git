/*
 * Parse and rearrange a svnadmin dump.
 * Create the dump with:
 * svnadmin dump --incremental -r<startrev>:<endrev> <repository> >outfile
 *
 * Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "cache.h"
#include "repo_tree.h"
#include "fast_export.h"
#include "line_buffer.h"
#include "strbuf.h"
#include "svndump.h"

/*
 * Compare start of string to literal of equal length;
 * must be guarded by length test.
 */
#define constcmp(s, ref) memcmp(s, ref, sizeof(ref) - 1)

#define REPORT_FILENO 3

#define NODEACT_REPLACE 4
#define NODEACT_DELETE 3
#define NODEACT_ADD 2
#define NODEACT_CHANGE 1
#define NODEACT_UNKNOWN 0

/* States: */
#define DUMP_CTX 0	/* dump metadata */
#define REV_CTX  1	/* revision metadata */
#define NODE_CTX 2	/* node metadata */
#define INTERNODE_CTX 3	/* between nodes */

#define DATE_RFC2822_LEN 31

static struct line_buffer input = LINE_BUFFER_INIT;

static struct {
	uint32_t action, srcRev, type;
	off_t prop_length, text_length;
	struct strbuf src, dst;
	uint32_t text_delta, prop_delta;
} node_ctx;

static struct {
	uint32_t revision;
	unsigned long timestamp;
	struct strbuf log, author, note;
} rev_ctx;

static struct {
	uint32_t version;
	struct strbuf uuid, url;
} dump_ctx;

static void reset_node_ctx(char *fname)
{
	node_ctx.type = 0;
	node_ctx.action = NODEACT_UNKNOWN;
	node_ctx.prop_length = -1;
	node_ctx.text_length = -1;
	strbuf_reset(&node_ctx.src);
	node_ctx.srcRev = 0;
	strbuf_reset(&node_ctx.dst);
	if (fname)
		strbuf_addstr(&node_ctx.dst, fname);
	node_ctx.text_delta = 0;
	node_ctx.prop_delta = 0;
}

static void reset_rev_ctx(uint32_t revision)
{
	rev_ctx.revision = revision;
	rev_ctx.timestamp = 0;
	strbuf_reset(&rev_ctx.log);
	strbuf_reset(&rev_ctx.author);
	strbuf_reset(&rev_ctx.note);
}

static void reset_dump_ctx(const char *url)
{
	strbuf_reset(&dump_ctx.url);
	if (url)
		strbuf_addstr(&dump_ctx.url, url);
	dump_ctx.version = 1;
	strbuf_reset(&dump_ctx.uuid);
}

static void handle_property(const struct strbuf *key_buf,
				struct strbuf *val,
				uint32_t *type_set)
{
	const char *key = key_buf->buf;
	size_t keylen = key_buf->len;

	switch (keylen + 1) {
	case sizeof("svn:log"):
		if (constcmp(key, "svn:log"))
			break;
		if (!val)
			die("invalid dump: unsets svn:log");
		strbuf_swap(&rev_ctx.log, val);
		break;
	case sizeof("svn:author"):
		if (constcmp(key, "svn:author"))
			break;
		if (!val)
			strbuf_reset(&rev_ctx.author);
		else
			strbuf_swap(&rev_ctx.author, val);
		break;
	case sizeof("svn:date"):
		if (constcmp(key, "svn:date"))
			break;
		if (!val)
			die("invalid dump: unsets svn:date");
		if (parse_date_basic(val->buf, &rev_ctx.timestamp, NULL))
			warning("invalid timestamp: %s", val->buf);
		break;
	case sizeof("svn:executable"):
	case sizeof("svn:special"):
		if (keylen == strlen("svn:executable") &&
		    constcmp(key, "svn:executable"))
			break;
		if (keylen == strlen("svn:special") &&
		    constcmp(key, "svn:special"))
			break;
		if (*type_set) {
			if (!val)
				return;
			die("invalid dump: sets type twice");
		}
		if (!val) {
			node_ctx.type = REPO_MODE_BLB;
			return;
		}
		*type_set = 1;
		node_ctx.type = keylen == strlen("svn:executable") ?
				REPO_MODE_EXE :
				REPO_MODE_LNK;
	}
}

static void die_short_read(void)
{
	if (buffer_ferror(&input))
		die_errno("error reading dump file");
	die("invalid dump: unexpected end of file");
}

static void read_props(void)
{
	static struct strbuf key = STRBUF_INIT;
	static struct strbuf val = STRBUF_INIT;
	const char *t;
	/*
	 * NEEDSWORK: to support simple mode changes like
	 *	K 11
	 *	svn:special
	 *	V 1
	 *	*
	 *	D 14
	 *	svn:executable
	 * we keep track of whether a mode has been set and reset to
	 * plain file only if not.  We should be keeping track of the
	 * symlink and executable bits separately instead.
	 */
	uint32_t type_set = 0;
	while ((t = buffer_read_line(&input)) && strcmp(t, "PROPS-END")) {
		uint32_t len;
		const char type = t[0];
		int ch;

		if (!type || t[1] != ' ')
			die("invalid property line: %s", t);
		len = atoi(&t[2]);
		strbuf_reset(&val);
		buffer_read_binary(&input, &val, len);
		if (val.len < len)
			die_short_read();

		/* Discard trailing newline. */
		ch = buffer_read_char(&input);
		if (ch == EOF)
			die_short_read();
		if (ch != '\n')
			die("invalid dump: expected newline after %s", val.buf);

		switch (type) {
		case 'K':
			strbuf_swap(&key, &val);
			continue;
		case 'D':
			handle_property(&val, NULL, &type_set);
			continue;
		case 'V':
			handle_property(&key, &val, &type_set);
			strbuf_reset(&key);
			continue;
		default:
			die("invalid property line: %s", t);
		}
	}
}

static void handle_node(void)
{
	const uint32_t type = node_ctx.type;
	const int have_props = node_ctx.prop_length != -1;
	const int have_text = node_ctx.text_length != -1;
	/*
	 * Old text for this node:
	 *  NULL	- directory or bug
	 *  empty_blob	- empty
	 *  "<dataref>"	- data retrievable from fast-import
	 */
	static const char *const empty_blob = "::empty::";
	const char *old_data = NULL;
	uint32_t old_mode = REPO_MODE_BLB;

	if (node_ctx.action == NODEACT_DELETE) {
		if (have_text || have_props || node_ctx.srcRev)
			die("invalid dump: deletion node has "
				"copyfrom info, text, or properties");
		repo_delete(node_ctx.dst.buf);
		return;
	}
	if (node_ctx.action == NODEACT_REPLACE) {
		repo_delete(node_ctx.dst.buf);
		node_ctx.action = NODEACT_ADD;
	}
	if (node_ctx.srcRev) {
		repo_copy(node_ctx.srcRev, node_ctx.src.buf, node_ctx.dst.buf);
		if (node_ctx.action == NODEACT_ADD)
			node_ctx.action = NODEACT_CHANGE;
	}
	if (have_text && type == REPO_MODE_DIR)
		die("invalid dump: directories cannot have text attached");

	/*
	 * Find old content (old_data) and decide on the new mode.
	 */
	if (node_ctx.action == NODEACT_CHANGE && !*node_ctx.dst.buf) {
		if (type != REPO_MODE_DIR)
			die("invalid dump: root of tree is not a regular file");
		old_data = NULL;
	} else if (node_ctx.action == NODEACT_CHANGE) {
		uint32_t mode;
		old_data = repo_read_path(node_ctx.dst.buf, &mode);
		if (mode == REPO_MODE_DIR && type != REPO_MODE_DIR)
			die("invalid dump: cannot modify a directory into a file");
		if (mode != REPO_MODE_DIR && type == REPO_MODE_DIR)
			die("invalid dump: cannot modify a file into a directory");
		node_ctx.type = mode;
		old_mode = mode;
	} else if (node_ctx.action == NODEACT_ADD) {
		if (type == REPO_MODE_DIR)
			old_data = NULL;
		else if (have_text)
			old_data = empty_blob;
		else
			die("invalid dump: adds node without text");
	} else {
		die("invalid dump: Node-path block lacks Node-action");
	}

	/*
	 * Adjust mode to reflect properties.
	 */
	if (have_props) {
		if (!node_ctx.prop_delta)
			node_ctx.type = type;
		if (node_ctx.prop_length)
			read_props();
	}

	/*
	 * Save the result.
	 */
	if (type == REPO_MODE_DIR)	/* directories are not tracked. */
		return;
	assert(old_data);
	if (old_data == empty_blob)
		/* For the fast_export_* functions, NULL means empty. */
		old_data = NULL;
	if (!have_text) {
		fast_export_modify(node_ctx.dst.buf, node_ctx.type, old_data);
		return;
	}
	if (!node_ctx.text_delta) {
		fast_export_modify(node_ctx.dst.buf, node_ctx.type, "inline");
		fast_export_data(node_ctx.type, node_ctx.text_length, &input);
		return;
	}
	fast_export_modify(node_ctx.dst.buf, node_ctx.type, "inline");
	fast_export_blob_delta(node_ctx.type, old_mode, old_data,
				node_ctx.text_length, &input);
}

static void begin_revision(const char *remote_ref)
{
	if (!rev_ctx.revision)	/* revision 0 gets no git commit. */
		return;
	fast_export_begin_commit(rev_ctx.revision, rev_ctx.author.buf,
		&rev_ctx.log, dump_ctx.uuid.buf, dump_ctx.url.buf,
		rev_ctx.timestamp, remote_ref);
}

static void end_revision(const char *note_ref)
{
	struct strbuf mark = STRBUF_INIT;
	if (rev_ctx.revision) {
		fast_export_end_commit(rev_ctx.revision);
		fast_export_begin_note(rev_ctx.revision, "remote-svn",
				"Note created by remote-svn.", rev_ctx.timestamp, note_ref);
		strbuf_addf(&mark, ":%"PRIu32, rev_ctx.revision);
		fast_export_note(mark.buf, "inline");
		fast_export_buf_to_data(&rev_ctx.note);
	}
}

void svndump_read(const char *url, const char *local_ref, const char *notes_ref)
{
	char *val;
	char *t;
	uint32_t active_ctx = DUMP_CTX;
	uint32_t len;

	reset_dump_ctx(url);
	while ((t = buffer_read_line(&input))) {
		val = strchr(t, ':');
		if (!val)
			continue;
		val++;
		if (*val != ' ')
			continue;
		val++;

		/* strlen(key) + 1 */
		switch (val - t - 1) {
		case sizeof("SVN-fs-dump-format-version"):
			if (constcmp(t, "SVN-fs-dump-format-version"))
				continue;
			dump_ctx.version = atoi(val);
			if (dump_ctx.version > 3)
				die("expected svn dump format version <= 3, found %"PRIu32,
				    dump_ctx.version);
			break;
		case sizeof("UUID"):
			if (constcmp(t, "UUID"))
				continue;
			strbuf_reset(&dump_ctx.uuid);
			strbuf_addstr(&dump_ctx.uuid, val);
			break;
		case sizeof("Revision-number"):
			if (constcmp(t, "Revision-number"))
				continue;
			if (active_ctx == NODE_CTX)
				handle_node();
			if (active_ctx == REV_CTX)
				begin_revision(local_ref);
			if (active_ctx != DUMP_CTX)
				end_revision(notes_ref);
			active_ctx = REV_CTX;
			reset_rev_ctx(atoi(val));
			strbuf_addf(&rev_ctx.note, "%s\n", t);
			break;
		case sizeof("Node-path"):
			if (constcmp(t, "Node-"))
				continue;
			if (!constcmp(t + strlen("Node-"), "path")) {
				if (active_ctx == NODE_CTX)
					handle_node();
				if (active_ctx == REV_CTX)
					begin_revision(local_ref);
				active_ctx = NODE_CTX;
				reset_node_ctx(val);
				strbuf_addf(&rev_ctx.note, "%s\n", t);
				break;
			}
			if (constcmp(t + strlen("Node-"), "kind"))
				continue;
			strbuf_addf(&rev_ctx.note, "%s\n", t);
			if (!strcmp(val, "dir"))
				node_ctx.type = REPO_MODE_DIR;
			else if (!strcmp(val, "file"))
				node_ctx.type = REPO_MODE_BLB;
			else
				fprintf(stderr, "Unknown node-kind: %s\n", val);
			break;
		case sizeof("Node-action"):
			if (constcmp(t, "Node-action"))
				continue;
			strbuf_addf(&rev_ctx.note, "%s\n", t);
			if (!strcmp(val, "delete")) {
				node_ctx.action = NODEACT_DELETE;
			} else if (!strcmp(val, "add")) {
				node_ctx.action = NODEACT_ADD;
			} else if (!strcmp(val, "change")) {
				node_ctx.action = NODEACT_CHANGE;
			} else if (!strcmp(val, "replace")) {
				node_ctx.action = NODEACT_REPLACE;
			} else {
				fprintf(stderr, "Unknown node-action: %s\n", val);
				node_ctx.action = NODEACT_UNKNOWN;
			}
			break;
		case sizeof("Node-copyfrom-path"):
			if (constcmp(t, "Node-copyfrom-path"))
				continue;
			strbuf_reset(&node_ctx.src);
			strbuf_addstr(&node_ctx.src, val);
			strbuf_addf(&rev_ctx.note, "%s\n", t);
			break;
		case sizeof("Node-copyfrom-rev"):
			if (constcmp(t, "Node-copyfrom-rev"))
				continue;
			node_ctx.srcRev = atoi(val);
			strbuf_addf(&rev_ctx.note, "%s\n", t);
			break;
		case sizeof("Text-content-length"):
			if (constcmp(t, "Text") && constcmp(t, "Prop"))
				continue;
			if (constcmp(t + 4, "-content-length"))
				continue;
			{
				char *end;
				uintmax_t len;

				len = strtoumax(val, &end, 10);
				if (!isdigit(*val) || *end)
					die("invalid dump: non-numeric length %s", val);
				if (len > maximum_signed_value_of_type(off_t))
					die("unrepresentable length in dump: %s", val);

				if (*t == 'T')
					node_ctx.text_length = (off_t) len;
				else
					node_ctx.prop_length = (off_t) len;
				break;
			}
		case sizeof("Text-delta"):
			if (!constcmp(t, "Text-delta")) {
				node_ctx.text_delta = !strcmp(val, "true");
				break;
			}
			if (constcmp(t, "Prop-delta"))
				continue;
			node_ctx.prop_delta = !strcmp(val, "true");
			break;
		case sizeof("Content-length"):
			if (constcmp(t, "Content-length"))
				continue;
			len = atoi(val);
			t = buffer_read_line(&input);
			if (!t)
				die_short_read();
			if (*t)
				die("invalid dump: expected blank line after content length header");
			if (active_ctx == REV_CTX) {
				read_props();
			} else if (active_ctx == NODE_CTX) {
				handle_node();
				active_ctx = INTERNODE_CTX;
			} else {
				fprintf(stderr, "Unexpected content length header: %"PRIu32"\n", len);
				if (buffer_skip_bytes(&input, len) != len)
					die_short_read();
			}
		}
	}
	if (buffer_ferror(&input))
		die_short_read();
	if (active_ctx == NODE_CTX)
		handle_node();
	if (active_ctx == REV_CTX)
		begin_revision(local_ref);
	if (active_ctx != DUMP_CTX)
		end_revision(notes_ref);
}

static void init(int report_fd)
{
	fast_export_init(report_fd);
	strbuf_init(&dump_ctx.uuid, 4096);
	strbuf_init(&dump_ctx.url, 4096);
	strbuf_init(&rev_ctx.log, 4096);
	strbuf_init(&rev_ctx.author, 4096);
	strbuf_init(&rev_ctx.note, 4096);
	strbuf_init(&node_ctx.src, 4096);
	strbuf_init(&node_ctx.dst, 4096);
	reset_dump_ctx(NULL);
	reset_rev_ctx(0);
	reset_node_ctx(NULL);
	return;
}

int svndump_init(const char *filename)
{
	if (buffer_init(&input, filename))
		return error("cannot open %s: %s", filename ? filename : "NULL", strerror(errno));
	init(REPORT_FILENO);
	return 0;
}

int svndump_init_fd(int in_fd, int back_fd)
{
	if(buffer_fdinit(&input, xdup(in_fd)))
		return error("cannot open fd %d: %s", in_fd, strerror(errno));
	init(xdup(back_fd));
	return 0;
}

void svndump_deinit(void)
{
	fast_export_deinit();
	reset_dump_ctx(NULL);
	reset_rev_ctx(0);
	reset_node_ctx(NULL);
	strbuf_release(&rev_ctx.log);
	strbuf_release(&rev_ctx.author);
	strbuf_release(&rev_ctx.note);
	strbuf_release(&node_ctx.src);
	strbuf_release(&node_ctx.dst);
	if (buffer_deinit(&input))
		fprintf(stderr, "Input error\n");
	if (ferror(stdout))
		fprintf(stderr, "Output error\n");
}

void svndump_reset(void)
{
	strbuf_release(&dump_ctx.uuid);
	strbuf_release(&dump_ctx.url);
	strbuf_release(&rev_ctx.log);
	strbuf_release(&rev_ctx.author);
}
