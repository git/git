/*
 * Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "git-compat-util.h"
#include "strbuf.h"
#include "quote.h"
#include "fast_export.h"
#include "repo_tree.h"
#include "strbuf.h"
#include "svndiff.h"
#include "sliding_window.h"
#include "line_buffer.h"

#define MAX_GITSVN_LINE_LEN 4096

static uint32_t first_commit_done;
static struct line_buffer postimage = LINE_BUFFER_INIT;
static struct line_buffer report_buffer = LINE_BUFFER_INIT;

/* NEEDSWORK: move to fast_export_init() */
static int init_postimage(void)
{
	static int postimage_initialized;
	if (postimage_initialized)
		return 0;
	postimage_initialized = 1;
	return buffer_tmpfile_init(&postimage);
}

void fast_export_init(int fd)
{
	first_commit_done = 0;
	if (buffer_fdinit(&report_buffer, fd))
		die_errno("cannot read from file descriptor %d", fd);
}

void fast_export_deinit(void)
{
	if (buffer_deinit(&report_buffer))
		die_errno("error closing fast-import feedback stream");
}

void fast_export_delete(const char *path)
{
	putchar('D');
	putchar(' ');
	quote_c_style(path, NULL, stdout, 0);
	putchar('\n');
}

static void fast_export_truncate(const char *path, uint32_t mode)
{
	fast_export_modify(path, mode, "inline");
	printf("data 0\n\n");
}

void fast_export_modify(const char *path, uint32_t mode, const char *dataref)
{
	/* Mode must be 100644, 100755, 120000, or 160000. */
	if (!dataref) {
		fast_export_truncate(path, mode);
		return;
	}
	printf("M %06"PRIo32" %s ", mode, dataref);
	quote_c_style(path, NULL, stdout, 0);
	putchar('\n');
}

static char gitsvnline[MAX_GITSVN_LINE_LEN];
void fast_export_begin_commit(uint32_t revision, const char *author,
			const struct strbuf *log,
			const char *uuid, const char *url,
			unsigned long timestamp)
{
	static const struct strbuf empty = STRBUF_INIT;
	if (!log)
		log = &empty;
	if (*uuid && *url) {
		snprintf(gitsvnline, MAX_GITSVN_LINE_LEN,
				"\n\ngit-svn-id: %s@%"PRIu32" %s\n",
				 url, revision, uuid);
	} else {
		*gitsvnline = '\0';
	}
	printf("commit refs/heads/master\n");
	printf("mark :%"PRIu32"\n", revision);
	printf("committer %s <%s@%s> %ld +0000\n",
		   *author ? author : "nobody",
		   *author ? author : "nobody",
		   *uuid ? uuid : "local", timestamp);
	printf("data %"PRIuMAX"\n",
		(uintmax_t) (log->len + strlen(gitsvnline)));
	fwrite(log->buf, log->len, 1, stdout);
	printf("%s\n", gitsvnline);
	if (!first_commit_done) {
		if (revision > 1)
			printf("from :%"PRIu32"\n", revision - 1);
		first_commit_done = 1;
	}
}

void fast_export_end_commit(uint32_t revision)
{
	printf("progress Imported commit %"PRIu32".\n\n", revision);
}

static void ls_from_rev(uint32_t rev, const char *path)
{
	/* ls :5 path/to/old/file */
	printf("ls :%"PRIu32" ", rev);
	quote_c_style(path, NULL, stdout, 0);
	putchar('\n');
	fflush(stdout);
}

static void ls_from_active_commit(const char *path)
{
	/* ls "path/to/file" */
	printf("ls \"");
	quote_c_style(path, NULL, stdout, 1);
	printf("\"\n");
	fflush(stdout);
}

static const char *get_response_line(void)
{
	const char *line = buffer_read_line(&report_buffer);
	if (line)
		return line;
	if (buffer_ferror(&report_buffer))
		die_errno("error reading from fast-import");
	die("unexpected end of fast-import feedback");
}

static void die_short_read(struct line_buffer *input)
{
	if (buffer_ferror(input))
		die_errno("error reading dump file");
	die("invalid dump: unexpected end of file");
}

static int ends_with(const char *s, size_t len, const char *suffix)
{
	const size_t suffixlen = strlen(suffix);
	if (len < suffixlen)
		return 0;
	return !memcmp(s + len - suffixlen, suffix, suffixlen);
}

static int parse_cat_response_line(const char *header, off_t *len)
{
	size_t headerlen = strlen(header);
	uintmax_t n;
	const char *type;
	const char *end;

	if (ends_with(header, headerlen, " missing"))
		return error("cat-blob reports missing blob: %s", header);
	type = strstr(header, " blob ");
	if (!type)
		return error("cat-blob header has wrong object type: %s", header);
	n = strtoumax(type + strlen(" blob "), (char **) &end, 10);
	if (end == type + strlen(" blob "))
		return error("cat-blob header does not contain length: %s", header);
	if (memchr(type + strlen(" blob "), '-', end - type - strlen(" blob ")))
		return error("cat-blob header contains negative length: %s", header);
	if (n == UINTMAX_MAX || n > maximum_signed_value_of_type(off_t))
		return error("blob too large for current definition of off_t");
	*len = n;
	if (*end)
		return error("cat-blob header contains garbage after length: %s", header);
	return 0;
}

static void check_preimage_overflow(off_t a, off_t b)
{
	if (signed_add_overflows(a, b))
		die("blob too large for current definition of off_t");
}

static long apply_delta(off_t len, struct line_buffer *input,
			const char *old_data, uint32_t old_mode)
{
	long ret;
	struct sliding_view preimage = SLIDING_VIEW_INIT(&report_buffer, 0);
	FILE *out;

	if (init_postimage() || !(out = buffer_tmpfile_rewind(&postimage)))
		die("cannot open temporary file for blob retrieval");
	if (old_data) {
		const char *response;
		printf("cat-blob %s\n", old_data);
		fflush(stdout);
		response = get_response_line();
		if (parse_cat_response_line(response, &preimage.max_off))
			die("invalid cat-blob response: %s", response);
		check_preimage_overflow(preimage.max_off, 1);
	}
	if (old_mode == REPO_MODE_LNK) {
		strbuf_addstr(&preimage.buf, "link ");
		check_preimage_overflow(preimage.max_off, strlen("link "));
		preimage.max_off += strlen("link ");
		check_preimage_overflow(preimage.max_off, 1);
	}
	if (svndiff0_apply(input, len, &preimage, out))
		die("cannot apply delta");
	if (old_data) {
		/* Read the remainder of preimage and trailing newline. */
		assert(!signed_add_overflows(preimage.max_off, 1));
		preimage.max_off++;	/* room for newline */
		if (move_window(&preimage, preimage.max_off - 1, 1))
			die("cannot seek to end of input");
		if (preimage.buf.buf[0] != '\n')
			die("missing newline after cat-blob response");
	}
	ret = buffer_tmpfile_prepare_to_read(&postimage);
	if (ret < 0)
		die("cannot read temporary file for blob retrieval");
	strbuf_release(&preimage.buf);
	return ret;
}

void fast_export_data(uint32_t mode, off_t len, struct line_buffer *input)
{
	assert(len >= 0);
	if (mode == REPO_MODE_LNK) {
		/* svn symlink blobs start with "link " */
		if (len < 5)
			die("invalid dump: symlink too short for \"link\" prefix");
		len -= 5;
		if (buffer_skip_bytes(input, 5) != 5)
			die_short_read(input);
	}
	printf("data %"PRIuMAX"\n", (uintmax_t) len);
	if (buffer_copy_bytes(input, len) != len)
		die_short_read(input);
	fputc('\n', stdout);
}

static int parse_ls_response(const char *response, uint32_t *mode,
					struct strbuf *dataref)
{
	const char *tab;
	const char *response_end;

	assert(response);
	response_end = response + strlen(response);

	if (*response == 'm') {	/* Missing. */
		errno = ENOENT;
		return -1;
	}

	/* Mode. */
	if (response_end - response < (signed) strlen("100644") ||
	    response[strlen("100644")] != ' ')
		die("invalid ls response: missing mode: %s", response);
	*mode = 0;
	for (; *response != ' '; response++) {
		char ch = *response;
		if (ch < '0' || ch > '7')
			die("invalid ls response: mode is not octal: %s", response);
		*mode *= 8;
		*mode += ch - '0';
	}

	/* ' blob ' or ' tree ' */
	if (response_end - response < (signed) strlen(" blob ") ||
	    (response[1] != 'b' && response[1] != 't'))
		die("unexpected ls response: not a tree or blob: %s", response);
	response += strlen(" blob ");

	/* Dataref. */
	tab = memchr(response, '\t', response_end - response);
	if (!tab)
		die("invalid ls response: missing tab: %s", response);
	strbuf_add(dataref, response, tab - response);
	return 0;
}

int fast_export_ls_rev(uint32_t rev, const char *path,
				uint32_t *mode, struct strbuf *dataref)
{
	ls_from_rev(rev, path);
	return parse_ls_response(get_response_line(), mode, dataref);
}

int fast_export_ls(const char *path, uint32_t *mode, struct strbuf *dataref)
{
	ls_from_active_commit(path);
	return parse_ls_response(get_response_line(), mode, dataref);
}

void fast_export_blob_delta(uint32_t mode,
				uint32_t old_mode, const char *old_data,
				off_t len, struct line_buffer *input)
{
	long postimage_len;

	assert(len >= 0);
	postimage_len = apply_delta(len, input, old_data, old_mode);
	if (mode == REPO_MODE_LNK) {
		buffer_skip_bytes(&postimage, strlen("link "));
		postimage_len -= strlen("link ");
	}
	printf("data %ld\n", postimage_len);
	buffer_copy_bytes(&postimage, postimage_len);
	fputc('\n', stdout);
}
