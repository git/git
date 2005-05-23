/*
 * apply.c
 *
 * Copyright (C) Linus Torvalds, 2005
 *
 * This applies patches on top of some (arbitrary) version of the SCM.
 *
 * NOTE! It does all its work in the index file, and only cares about
 * the files in the working directory if you tell it to "merge" the
 * patch apply.
 *
 * Even when merging it always takes the source from the index, and
 * uses the working tree as a "branch" for a 3-way merge.
 */
#include <ctype.h>

#include "cache.h"

// We default to the merge behaviour, since that's what most people would
// expect
static int merge_patch = 1;
static const char apply_usage[] = "git-apply <patch>";

/*
 * Various "current state", notably line numbers and what
 * file (and how) we're patching right now.. The "is_xxxx"
 * things are flags, where -1 means "don't know yet".
 */
static int linenr = 1;
static int old_mode, new_mode;
static char *old_name, *new_name, *def_name;
static int is_rename, is_copy, is_new, is_delete;

#define CHUNKSIZE (8192)
#define SLOP (16)

static void *read_patch_file(int fd, unsigned long *sizep)
{
	unsigned long size = 0, alloc = CHUNKSIZE;
	void *buffer = xmalloc(alloc);

	for (;;) {
		int nr = alloc - size;
		if (nr < 1024) {
			alloc += CHUNKSIZE;
			buffer = xrealloc(buffer, alloc);
			nr = alloc - size;
		}
		nr = read(fd, buffer + size, nr);
		if (!nr)
			break;
		if (nr < 0) {
			if (errno == EAGAIN)
				continue;
			die("git-apply: read returned %s", strerror(errno));
		}
		size += nr;
	}
	*sizep = size;

	/*
	 * Make sure that we have some slop in the buffer
	 * so that we can do speculative "memcmp" etc, and
	 * see to it that it is NUL-filled.
	 */
	if (alloc < size + SLOP)
		buffer = xrealloc(buffer, size + SLOP);
	memset(buffer + size, 0, SLOP);
	return buffer;
}

static unsigned long linelen(char *buffer, unsigned long size)
{
	unsigned long len = 0;
	while (size--) {
		len++;
		if (*buffer++ == '\n')
			break;
	}
	return len;
}

static int is_dev_null(const char *str)
{
	return !memcmp("/dev/null", str, 9) && isspace(str[9]);
}

static char * find_name(const char *line, char *def, int p_value)
{
	int len;
	const char *start = line;
	char *name;

	for (;;) {
		char c = *line;
		if (isspace(c))
			break;
		line++;
		if (c == '/' && !--p_value)
			start = line;
	}
	if (!start)
		return def;
	len = line - start;
	if (!len)
		return def;

	/*
	 * Generally we prefer the shorter name, especially
	 * if the other one is just a variation of that with
	 * something else tacked on to the end (ie "file.orig"
	 * or "file~").
	 */
	if (def) {
		int deflen = strlen(def);
		if (deflen < len && !strncmp(start, def, deflen))
			return def;
	}

	name = xmalloc(len + 1);
	memcpy(name, start, len);
	name[len] = 0;
	free(def);
	return name;
}

/*
 * Get the name etc info from the --/+++ lines of a traditional patch header
 *
 * NOTE! This hardcodes "-p1" behaviour in filename detection.
 */
static int parse_traditional_patch(const char *first, const char *second)
{
	int p_value = 1;
	char *name;

	first += 4;	// skip "--- "
	second += 4;	// skip "+++ "
	if (is_dev_null(first)) {
		is_new = 1;
		name = find_name(second, def_name, p_value);
	} else if (is_dev_null(second)) {
		is_delete = 1;
		name = find_name(first, def_name, p_value);
	} else {
		name = find_name(first, def_name, p_value);
		name = find_name(second, name, p_value);
	}
	if (!name)
		die("unable to find filename in patch at line %d", linenr);
	old_name = name;
	new_name = name;
}

static int gitdiff_hdrend(const char *line)
{
	return -1;
}

static int gitdiff_oldname(const char *line)
{
	if (!old_name)
		old_name = find_name(line, NULL, 1);
	return 0;
}

static int gitdiff_newname(const char *line)
{
	if (!new_name)
		new_name = find_name(line, NULL, 1);
	return 0;
}

static int gitdiff_oldmode(const char *line)
{
	old_mode = strtoul(line, NULL, 8);
	return 0;
}

static int gitdiff_newmode(const char *line)
{
	new_mode = strtoul(line, NULL, 8);
	return 0;
}

static int gitdiff_delete(const char *line)
{
	is_delete = 1;
	return gitdiff_oldmode(line);
}

static int gitdiff_newfile(const char *line)
{
	is_new = 1;
	return gitdiff_newmode(line);
}

static int gitdiff_copysrc(const char *line)
{
	is_copy = 1;
	old_name = find_name(line, NULL, 0);
	return 0;
}

static int gitdiff_copydst(const char *line)
{
	is_copy = 1;
	new_name = find_name(line, NULL, 0);
	return 0;
}

static int gitdiff_renamesrc(const char *line)
{
	is_rename = 1;
	old_name = find_name(line, NULL, 0);
	return 0;
}

static int gitdiff_renamedst(const char *line)
{
	is_rename = 1;
	new_name = find_name(line, NULL, 0);
	return 0;
}

static int gitdiff_similarity(const char *line)
{
	return 0;
}

/* Verify that we recognize the lines following a git header */
static int parse_git_header(char *line, int len, unsigned int size)
{
	unsigned long offset;

	/* A git diff has explicit new/delete information, so we don't guess */
	is_new = 0;
	is_delete = 0;

	line += len;
	size -= len;
	linenr++;
	for (offset = len ; size > 0 ; offset += len, size -= len, line += len, linenr++) {
		static const struct opentry {
			const char *str;
			int (*fn)(const char *);
		} optable[] = {
			{ "@@ -", gitdiff_hdrend },
			{ "--- ", gitdiff_oldname },
			{ "+++ ", gitdiff_newname },
			{ "old mode ", gitdiff_oldmode },
			{ "new mode ", gitdiff_newmode },
			{ "deleted file mode ", gitdiff_delete },
			{ "new file mode ", gitdiff_newfile },
			{ "copy from ", gitdiff_copysrc },
			{ "copy to ", gitdiff_copydst },
			{ "rename from ", gitdiff_renamesrc },
			{ "rename to ", gitdiff_renamedst },
			{ "similarity index ", gitdiff_similarity },
		};
		int i;

		len = linelen(line, size);
		if (!len || line[len-1] != '\n')
			break;
		for (i = 0; i < sizeof(optable) / sizeof(optable[0]); i++) {
			const struct opentry *p = optable + i;
			int oplen = strlen(p->str);
			if (len < oplen || memcmp(p->str, line, oplen))
				continue;
			if (p->fn(line + oplen) < 0)
				return offset;
		}
	}

	return offset;
}

static int parse_num(const char *line, int len, int offset, const char *expect, unsigned long *p)
{
	char *ptr;
	int digits, ex;

	if (offset < 0 || offset >= len)
		return -1;
	line += offset;
	len -= offset;

	if (!isdigit(*line))
		return -1;
	*p = strtoul(line, &ptr, 10);

	digits = ptr - line;

	offset += digits;
	line += digits;
	len -= digits;

	ex = strlen(expect);
	if (ex > len)
		return -1;
	if (memcmp(line, expect, ex))
		return -1;

	return offset + ex;
}

/*
 * Parse a unified diff fragment header of the
 * form "@@ -a,b +c,d @@"
 */
static int parse_fragment_header(char *line, int len, unsigned long *pos)
{
	int offset;

	if (!len || line[len-1] != '\n')
		return -1;

	/* Figure out the number of lines in a fragment */
	offset = parse_num(line, len, 4, ",", pos);
	offset = parse_num(line, len, offset, " +", pos+1);
	offset = parse_num(line, len, offset, ",", pos+2);
	offset = parse_num(line, len, offset, " @@", pos+3);

	return offset;
}

static int find_header(char *line, unsigned long size, int *hdrsize)
{
	unsigned long offset, len;

	is_rename = is_copy = 0;
	is_new = is_delete = -1;
	old_mode = new_mode = 0;
	def_name = old_name = new_name = NULL;
	for (offset = 0; size > 0; offset += len, size -= len, line += len, linenr++) {
		unsigned long nextlen;

		len = linelen(line, size);
		if (!len)
			break;

		/* Testing this early allows us to take a few shortcuts.. */
		if (len < 6)
			continue;

		/*
		 * Make sure we don't find any unconnected patch fragmants.
		 * That's a sign that we didn't find a header, and that a
		 * patch has become corrupted/broken up.
		 */
		if (!memcmp("@@ -", line, 4)) {
			unsigned long pos[4];
			if (parse_fragment_header(line, len, pos) < 0)
				continue;
			error("patch fragment without header at line %d: %.*s", linenr, len-1, line);
		}

		if (size < len + 6)
			break;

		/*
		 * Git patch? It might not have a real patch, just a rename
		 * or mode change, so we handle that specially
		 */
		if (!memcmp("diff --git ", line, 11)) {
			int git_hdr_len = parse_git_header(line, len, size);
			if (git_hdr_len < 0)
				continue;

			*hdrsize = git_hdr_len;
			return offset;
		}

		/** --- followed by +++ ? */
		if (memcmp("--- ", line,  4) || memcmp("+++ ", line + len, 4))
			continue;

		/*
		 * We only accept unified patches, so we want it to
		 * at least have "@@ -a,b +c,d @@\n", which is 14 chars
		 * minimum
		 */
		nextlen = linelen(line + len, size - len);
		if (size < nextlen + 14 || memcmp("@@ -", line + len + nextlen, 4))
			continue;

		/* Ok, we'll consider it a patch */
		parse_traditional_patch(line, line+len);
		*hdrsize = len + nextlen;
		linenr += 2;
		return offset;
	}
	return -1;
}

/*
 * Parse a unified diff. Note that this really needs
 * to parse each fragment separately, since the only
 * way to know the difference between a "---" that is
 * part of a patch, and a "---" that starts the next
 * patch is to look at the line counts..
 */
static int apply_fragment(char *line, unsigned long size)
{
	int len = linelen(line, size), offset;
	unsigned long pos[4], oldlines, newlines;

	offset = parse_fragment_header(line, len, pos);
	if (offset < 0)
		return -1;
	oldlines = pos[1];
	newlines = pos[3];

	if (is_new < 0 && (pos[0] || oldlines))
		is_new = 0;
	if (is_delete < 0 && (pos[1] || newlines))
		is_delete = 0;

	/* Parse the thing.. */
	line += len;
	size -= len;
	linenr++;
	for (offset = len; size > 0; offset += len, size -= len, line += len, linenr++) {
		if (!oldlines && !newlines)
			break;
		len = linelen(line, size);
		if (!len || line[len-1] != '\n')
			return -1;
		switch (*line) {
		default:
			return -1;
		case ' ':
			oldlines--;
			newlines--;
			break;
		case '-':
			oldlines--;
			break;
		case '+':
			newlines--;
			break;
		}
	}
	return offset;
}

static int apply_single_patch(char *line, unsigned long size)
{
	unsigned long offset = 0;

	while (size > 4 && !memcmp(line, "@@ -", 4)) {
		int len = apply_fragment(line, size);
		if (len <= 0)
			die("corrupt patch at line %d", linenr);

printf("applying fragment:\n%.*s\n\n", len, line);

		offset += len;
		line += len;
		size -= len;
	}
	return offset;
}

static int apply_chunk(char *buffer, unsigned long size)
{
	int hdrsize, patchsize;
	int offset = find_header(buffer, size, &hdrsize);
	char *header, *patch;

	if (offset < 0)
		return offset;
	header = buffer + offset;

printf("Found header:\n%.*s\n\n", hdrsize, header);
printf("Rename: %d\n", is_rename);
printf("Copy:   %d\n", is_copy);
printf("New:    %d\n", is_new);
printf("Delete: %d\n", is_delete);
printf("Mode:   %o->%o\n", old_mode, new_mode);
printf("Name:   '%s'->'%s'\n", old_name, new_name);

	patch = header + hdrsize;
	patchsize = apply_single_patch(patch, size - offset - hdrsize);

	return offset + hdrsize + patchsize;
}

static int apply_patch(int fd)
{
	unsigned long offset, size;
	char *buffer = read_patch_file(fd, &size);

	if (!buffer)
		return -1;
	offset = 0;
	while (size > 0) {
		int nr = apply_chunk(buffer + offset, size);
		if (nr < 0)
			break;
		offset += nr;
		size -= nr;
	}
	free(buffer);
	return 0;
}

int main(int argc, char **argv)
{
	int i;
	int read_stdin = 1;

	if (read_cache() < 0)
		die("unable to read index file");

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		int fd;

		if (!strcmp(arg, "-")) {
			apply_patch(0);
			read_stdin = 0;
			continue;
		}
		if (!strcmp(arg, "--no-merge")) {
			merge_patch = 0;
			continue;
		}
		fd = open(arg, O_RDONLY);
		if (fd < 0)
			usage(apply_usage);
		read_stdin = 0;
		apply_patch(fd);
		close(fd);
	}
	if (read_stdin)
		apply_patch(0);
	return 0;
}
