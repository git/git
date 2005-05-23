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

static int linenr = 1;

#define CHUNKSIZE (8192)

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

static int match_word(const char *line, const char *match)
{
	for (;;) {
		char c = *match++;
		if (!c)
			break;
		if (*line++ != c)
			return 0;
	}
	return *line == ' ';
}

/* Verify that we recognize the lines following a git header */
static int parse_git_header(char *line, unsigned int size)
{
	unsigned long offset, len;

	for (offset = 0 ; size > 0 ; offset += len, size -= len, line += len, linenr++) {
		len = linelen(line, size);
		if (!len)
			break;
		if (line[len-1] != '\n')
			return -1;
		if (len < 4)
			break;
		if (!memcmp(line, "@@ -", 4))
			return offset;
		if (match_word(line, "new file mode"))
			continue;
		if (match_word(line, "deleted file mode"))
			continue;
		if (match_word(line, "copy"))
			continue;
		if (match_word(line, "rename"))
			continue;
		if (match_word(line, "similarity index"))
			continue;
		break;
	}

	/* We want either a patch _or_ something real */
	return offset ? :-1;
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
			int git_hdr_len = parse_git_header(line + len, size - len);
			if (git_hdr_len < 0)
				continue;

			*hdrsize = len + git_hdr_len;
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

	if (read_cache() < 0)
		die("unable to read index file");

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		int fd;

		if (!strcmp(arg, "-")) {
			apply_patch(0);
			continue;
		}
		if (!strcmp(arg, "--no-merge")) {
			merge_patch = 0;
			continue;
		}
		fd = open(arg, O_RDONLY);
		if (fd < 0)
			usage(apply_usage);
		apply_patch(fd);
		close(fd);
	}
	return 0;
}
