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
#include "obj_pool.h"
#include "string_pool.h"

#define NODEACT_REPLACE 4
#define NODEACT_DELETE 3
#define NODEACT_ADD 2
#define NODEACT_CHANGE 1
#define NODEACT_UNKNOWN 0

#define DUMP_CTX 0
#define REV_CTX  1
#define NODE_CTX 2

#define LENGTH_UNKNOWN (~0)
#define DATE_RFC2822_LEN 31

/* Create memory pool for log messages */
obj_pool_gen(log, char, 4096)

static char* log_copy(uint32_t length, char *log)
{
	char *buffer;
	log_free(log_pool.size);
	buffer = log_pointer(log_alloc(length));
	strncpy(buffer, log, length);
	return buffer;
}

static struct {
	uint32_t action, propLength, textLength, srcRev, srcMode, mark, type;
	uint32_t src[REPO_MAX_PATH_DEPTH], dst[REPO_MAX_PATH_DEPTH];
} node_ctx;

static struct {
	uint32_t revision, author;
	unsigned long timestamp;
	char *log;
} rev_ctx;

static struct {
	uint32_t version, uuid, url;
} dump_ctx;

static struct {
	uint32_t svn_log, svn_author, svn_date, svn_executable, svn_special, uuid,
		revision_number, node_path, node_kind, node_action,
		node_copyfrom_path, node_copyfrom_rev, text_content_length,
		prop_content_length, content_length, svn_fs_dump_format_version;
} keys;

static void reset_node_ctx(char *fname)
{
	node_ctx.type = 0;
	node_ctx.action = NODEACT_UNKNOWN;
	node_ctx.propLength = LENGTH_UNKNOWN;
	node_ctx.textLength = LENGTH_UNKNOWN;
	node_ctx.src[0] = ~0;
	node_ctx.srcRev = 0;
	node_ctx.srcMode = 0;
	pool_tok_seq(REPO_MAX_PATH_DEPTH, node_ctx.dst, "/", fname);
	node_ctx.mark = 0;
}

static void reset_rev_ctx(uint32_t revision)
{
	rev_ctx.revision = revision;
	rev_ctx.timestamp = 0;
	rev_ctx.log = NULL;
	rev_ctx.author = ~0;
}

static void reset_dump_ctx(uint32_t url)
{
	dump_ctx.url = url;
	dump_ctx.version = 1;
	dump_ctx.uuid = ~0;
}

static void init_keys(void)
{
	keys.svn_log = pool_intern("svn:log");
	keys.svn_author = pool_intern("svn:author");
	keys.svn_date = pool_intern("svn:date");
	keys.svn_executable = pool_intern("svn:executable");
	keys.svn_special = pool_intern("svn:special");
	keys.uuid = pool_intern("UUID");
	keys.revision_number = pool_intern("Revision-number");
	keys.node_path = pool_intern("Node-path");
	keys.node_kind = pool_intern("Node-kind");
	keys.node_action = pool_intern("Node-action");
	keys.node_copyfrom_path = pool_intern("Node-copyfrom-path");
	keys.node_copyfrom_rev = pool_intern("Node-copyfrom-rev");
	keys.text_content_length = pool_intern("Text-content-length");
	keys.prop_content_length = pool_intern("Prop-content-length");
	keys.content_length = pool_intern("Content-length");
	keys.svn_fs_dump_format_version = pool_intern("SVN-fs-dump-format-version");
}

static void read_props(void)
{
	uint32_t len;
	uint32_t key = ~0;
	char *val = NULL;
	char *t;
	while ((t = buffer_read_line()) && strcmp(t, "PROPS-END")) {
		if (!strncmp(t, "K ", 2)) {
			len = atoi(&t[2]);
			key = pool_intern(buffer_read_string(len));
			buffer_read_line();
		} else if (!strncmp(t, "V ", 2)) {
			len = atoi(&t[2]);
			val = buffer_read_string(len);
			if (key == keys.svn_log) {
				/* Value length excludes terminating nul. */
				rev_ctx.log = log_copy(len + 1, val);
			} else if (key == keys.svn_author) {
				rev_ctx.author = pool_intern(val);
			} else if (key == keys.svn_date) {
				if (parse_date_basic(val, &rev_ctx.timestamp, NULL))
					fprintf(stderr, "Invalid timestamp: %s\n", val);
			} else if (key == keys.svn_executable) {
				node_ctx.type = REPO_MODE_EXE;
			} else if (key == keys.svn_special) {
				node_ctx.type = REPO_MODE_LNK;
			}
			key = ~0;
			buffer_read_line();
		}
	}
}

static void handle_node(void)
{
	if (node_ctx.propLength != LENGTH_UNKNOWN && node_ctx.propLength)
		read_props();

	if (node_ctx.srcRev)
		node_ctx.srcMode = repo_copy(node_ctx.srcRev, node_ctx.src, node_ctx.dst);

	if (node_ctx.textLength != LENGTH_UNKNOWN &&
	    node_ctx.type != REPO_MODE_DIR)
		node_ctx.mark = next_blob_mark();

	if (node_ctx.action == NODEACT_DELETE) {
		repo_delete(node_ctx.dst);
	} else if (node_ctx.action == NODEACT_CHANGE ||
			   node_ctx.action == NODEACT_REPLACE) {
		if (node_ctx.action == NODEACT_REPLACE &&
		    node_ctx.type == REPO_MODE_DIR)
			repo_replace(node_ctx.dst, node_ctx.mark);
		else if (node_ctx.propLength != LENGTH_UNKNOWN)
			repo_modify(node_ctx.dst, node_ctx.type, node_ctx.mark);
		else if (node_ctx.textLength != LENGTH_UNKNOWN)
			node_ctx.srcMode = repo_replace(node_ctx.dst, node_ctx.mark);
	} else if (node_ctx.action == NODEACT_ADD) {
		if (node_ctx.srcRev && node_ctx.propLength != LENGTH_UNKNOWN)
			repo_modify(node_ctx.dst, node_ctx.type, node_ctx.mark);
		else if (node_ctx.srcRev && node_ctx.textLength != LENGTH_UNKNOWN)
			node_ctx.srcMode = repo_replace(node_ctx.dst, node_ctx.mark);
		else if ((node_ctx.type == REPO_MODE_DIR && !node_ctx.srcRev) ||
			 node_ctx.textLength != LENGTH_UNKNOWN)
			repo_add(node_ctx.dst, node_ctx.type, node_ctx.mark);
	}

	if (node_ctx.propLength == LENGTH_UNKNOWN && node_ctx.srcMode)
		node_ctx.type = node_ctx.srcMode;

	if (node_ctx.mark)
		fast_export_blob(node_ctx.type, node_ctx.mark, node_ctx.textLength);
	else if (node_ctx.textLength != LENGTH_UNKNOWN)
		buffer_skip_bytes(node_ctx.textLength);
}

static void handle_revision(void)
{
	if (rev_ctx.revision)
		repo_commit(rev_ctx.revision, rev_ctx.author, rev_ctx.log,
			dump_ctx.uuid, dump_ctx.url, rev_ctx.timestamp);
}

void svndump_read(const char *url)
{
	char *val;
	char *t;
	uint32_t active_ctx = DUMP_CTX;
	uint32_t len;
	uint32_t key;

	reset_dump_ctx(pool_intern(url));
	while ((t = buffer_read_line())) {
		val = strstr(t, ": ");
		if (!val)
			continue;
		*val++ = '\0';
		*val++ = '\0';
		key = pool_intern(t);

		if (key == keys.svn_fs_dump_format_version) {
			dump_ctx.version = atoi(val);
			if (dump_ctx.version > 2)
				die("expected svn dump format version <= 2, found %"PRIu32,
				    dump_ctx.version);
		} else if (key == keys.uuid) {
			dump_ctx.uuid = pool_intern(val);
		} else if (key == keys.revision_number) {
			if (active_ctx == NODE_CTX)
				handle_node();
			if (active_ctx != DUMP_CTX)
				handle_revision();
			active_ctx = REV_CTX;
			reset_rev_ctx(atoi(val));
		} else if (key == keys.node_path) {
			if (active_ctx == NODE_CTX)
				handle_node();
			active_ctx = NODE_CTX;
			reset_node_ctx(val);
		} else if (key == keys.node_kind) {
			if (!strcmp(val, "dir"))
				node_ctx.type = REPO_MODE_DIR;
			else if (!strcmp(val, "file"))
				node_ctx.type = REPO_MODE_BLB;
			else
				fprintf(stderr, "Unknown node-kind: %s\n", val);
		} else if (key == keys.node_action) {
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
		} else if (key == keys.node_copyfrom_path) {
			pool_tok_seq(REPO_MAX_PATH_DEPTH, node_ctx.src, "/", val);
		} else if (key == keys.node_copyfrom_rev) {
			node_ctx.srcRev = atoi(val);
		} else if (key == keys.text_content_length) {
			node_ctx.textLength = atoi(val);
		} else if (key == keys.prop_content_length) {
			node_ctx.propLength = atoi(val);
		} else if (key == keys.content_length) {
			len = atoi(val);
			buffer_read_line();
			if (active_ctx == REV_CTX) {
				read_props();
			} else if (active_ctx == NODE_CTX) {
				handle_node();
				active_ctx = REV_CTX;
			} else {
				fprintf(stderr, "Unexpected content length header: %"PRIu32"\n", len);
				buffer_skip_bytes(len);
			}
		}
	}
	if (active_ctx == NODE_CTX)
		handle_node();
	if (active_ctx != DUMP_CTX)
		handle_revision();
}

void svndump_init(const char *filename)
{
	buffer_init(filename);
	repo_init();
	reset_dump_ctx(~0);
	reset_rev_ctx(0);
	reset_node_ctx(NULL);
	init_keys();
}

void svndump_deinit(void)
{
	log_reset();
	repo_reset();
	reset_dump_ctx(~0);
	reset_rev_ctx(0);
	reset_node_ctx(NULL);
	if (buffer_deinit())
		fprintf(stderr, "Input error\n");
	if (ferror(stdout))
		fprintf(stderr, "Output error\n");
}

void svndump_reset(void)
{
	log_reset();
	buffer_reset();
	repo_reset();
	reset_dump_ctx(~0);
	reset_rev_ctx(0);
	reset_node_ctx(NULL);
}
