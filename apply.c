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
#include <fnmatch.h>
#include "cache.h"

// We default to the merge behaviour, since that's what most people would
// expect.
//
//  --check turns on checking that the working tree matches the
//    files that are being modified, but doesn't apply the patch
//  --stat does just a diffstat, and doesn't actually apply
//  --show-files shows the directory changes
//
static int merge_patch = 1;
static int check_index = 0;
static int write_index = 0;
static int diffstat = 0;
static int summary = 0;
static int check = 0;
static int apply = 1;
static int show_files = 0;
static const char apply_usage[] =
"git-apply [--no-merge] [--stat] [--summary] [--check] [--index] [--apply] [--show-files] <patch>...";

/*
 * For "diff-stat" like behaviour, we keep track of the biggest change
 * we've seen, and the longest filename. That allows us to do simple
 * scaling.
 */
static int max_change, max_len;

/*
 * Various "current state", notably line numbers and what
 * file (and how) we're patching right now.. The "is_xxxx"
 * things are flags, where -1 means "don't know yet".
 */
static int linenr = 1;

struct fragment {
	unsigned long oldpos, oldlines;
	unsigned long newpos, newlines;
	const char *patch;
	int size;
	struct fragment *next;
};

struct patch {
	char *new_name, *old_name, *def_name;
	unsigned int old_mode, new_mode;
	int is_rename, is_copy, is_new, is_delete;
	int lines_added, lines_deleted;
	int score;
	struct fragment *fragments;
	char *result;
	unsigned long resultsize;
	struct patch *next;
};

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

static unsigned long linelen(const char *buffer, unsigned long size)
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

#define TERM_SPACE	1
#define TERM_TAB	2

static int name_terminate(const char *name, int namelen, int c, int terminate)
{
	if (c == ' ' && !(terminate & TERM_SPACE))
		return 0;
	if (c == '\t' && !(terminate & TERM_TAB))
		return 0;

	return 1;
}

static char * find_name(const char *line, char *def, int p_value, int terminate)
{
	int len;
	const char *start = line;
	char *name;

	for (;;) {
		char c = *line;

		if (isspace(c)) {
			if (c == '\n')
				break;
			if (name_terminate(start, line-start, c, terminate))
				break;
		}
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
 *
 * FIXME! The end-of-filename heuristics are kind of screwy. For existing
 * files, we can happily check the index for a match, but for creating a
 * new file we should try to match whatever "patch" does. I have no idea.
 */
static void parse_traditional_patch(const char *first, const char *second, struct patch *patch)
{
	int p_value = 1;
	char *name;

	first += 4;	// skip "--- "
	second += 4;	// skip "+++ "
	if (is_dev_null(first)) {
		patch->is_new = 1;
		patch->is_delete = 0;
		name = find_name(second, NULL, p_value, TERM_SPACE | TERM_TAB);
		patch->new_name = name;
	} else if (is_dev_null(second)) {
		patch->is_new = 0;
		patch->is_delete = 1;
		name = find_name(first, NULL, p_value, TERM_SPACE | TERM_TAB);
		patch->old_name = name;
	} else {
		name = find_name(first, NULL, p_value, TERM_SPACE | TERM_TAB);
		name = find_name(second, name, p_value, TERM_SPACE | TERM_TAB);
		patch->old_name = patch->new_name = name;
	}
	if (!name)
		die("unable to find filename in patch at line %d", linenr);
}

static int gitdiff_hdrend(const char *line, struct patch *patch)
{
	return -1;
}

/*
 * We're anal about diff header consistency, to make
 * sure that we don't end up having strange ambiguous
 * patches floating around.
 *
 * As a result, gitdiff_{old|new}name() will check
 * their names against any previous information, just
 * to make sure..
 */
static char *gitdiff_verify_name(const char *line, int isnull, char *orig_name, const char *oldnew)
{
	int len;
	const char *name;

	if (!orig_name && !isnull)
		return find_name(line, NULL, 1, 0);

	name = "/dev/null";
	len = 9;
	if (orig_name) {
		name = orig_name;
		len = strlen(name);
		if (isnull)
			die("git-apply: bad git-diff - expected /dev/null, got %s on line %d", name, linenr);
	}

	if (*name == '/')
		goto absolute_path;

	for (;;) {
		char c = *line++;
		if (c == '\n')
			break;
		if (c != '/')
			continue;
absolute_path:
		if (memcmp(line, name, len) || line[len] != '\n')
			break;
		return orig_name;
	}
	die("git-apply: bad git-diff - inconsistent %s filename on line %d", oldnew, linenr);
	return NULL;
}

static int gitdiff_oldname(const char *line, struct patch *patch)
{
	patch->old_name = gitdiff_verify_name(line, patch->is_new, patch->old_name, "old");
	return 0;
}

static int gitdiff_newname(const char *line, struct patch *patch)
{
	patch->new_name = gitdiff_verify_name(line, patch->is_delete, patch->new_name, "new");
	return 0;
}

static int gitdiff_oldmode(const char *line, struct patch *patch)
{
	patch->old_mode = strtoul(line, NULL, 8);
	return 0;
}

static int gitdiff_newmode(const char *line, struct patch *patch)
{
	patch->new_mode = strtoul(line, NULL, 8);
	return 0;
}

static int gitdiff_delete(const char *line, struct patch *patch)
{
	patch->is_delete = 1;
	patch->old_name = patch->def_name;
	return gitdiff_oldmode(line, patch);
}

static int gitdiff_newfile(const char *line, struct patch *patch)
{
	patch->is_new = 1;
	patch->new_name = patch->def_name;
	return gitdiff_newmode(line, patch);
}

static int gitdiff_copysrc(const char *line, struct patch *patch)
{
	patch->is_copy = 1;
	patch->old_name = find_name(line, NULL, 0, 0);
	return 0;
}

static int gitdiff_copydst(const char *line, struct patch *patch)
{
	patch->is_copy = 1;
	patch->new_name = find_name(line, NULL, 0, 0);
	return 0;
}

static int gitdiff_renamesrc(const char *line, struct patch *patch)
{
	patch->is_rename = 1;
	patch->old_name = find_name(line, NULL, 0, 0);
	return 0;
}

static int gitdiff_renamedst(const char *line, struct patch *patch)
{
	patch->is_rename = 1;
	patch->new_name = find_name(line, NULL, 0, 0);
	return 0;
}

static int gitdiff_similarity(const char *line, struct patch *patch)
{
	if ((patch->score = strtoul(line, NULL, 10)) == ULONG_MAX)
		patch->score = 0;
	return 0;
}

static int gitdiff_dissimilarity(const char *line, struct patch *patch)
{
	if ((patch->score = strtoul(line, NULL, 10)) == ULONG_MAX)
		patch->score = 0;
	return 0;
}

/*
 * This is normal for a diff that doesn't change anything: we'll fall through
 * into the next diff. Tell the parser to break out.
 */
static int gitdiff_unrecognized(const char *line, struct patch *patch)
{
	return -1;
}

static char *git_header_name(char *line)
{
	int len;
	char *name, *second;

	/*
	 * Find the first '/'
	 */
	name = line;
	for (;;) {
		char c = *name++;
		if (c == '\n')
			return NULL;
		if (c == '/')
			break;
	}

	/*
	 * We don't accept absolute paths (/dev/null) as possibly valid
	 */
	if (name == line+1)
		return NULL;

	/*
	 * Accept a name only if it shows up twice, exactly the same
	 * form.
	 */
	for (len = 0 ; ; len++) {
		char c = name[len];

		switch (c) {
		default:
			continue;
		case '\n':
			break;
		case '\t': case ' ':
			second = name+len;
			for (;;) {
				char c = *second++;
				if (c == '\n')
					return NULL;
				if (c == '/')
					break;
			}
			if (second[len] == '\n' && !memcmp(name, second, len)) {
				char *ret = xmalloc(len + 1);
				memcpy(ret, name, len);
				ret[len] = 0;
				return ret;
			}
		}
	}
	return NULL;
}

/* Verify that we recognize the lines following a git header */
static int parse_git_header(char *line, int len, unsigned int size, struct patch *patch)
{
	unsigned long offset;

	/* A git diff has explicit new/delete information, so we don't guess */
	patch->is_new = 0;
	patch->is_delete = 0;

	/*
	 * Some things may not have the old name in the
	 * rest of the headers anywhere (pure mode changes,
	 * or removing or adding empty files), so we get
	 * the default name from the header.
	 */
	patch->def_name = git_header_name(line + strlen("diff --git "));

	line += len;
	size -= len;
	linenr++;
	for (offset = len ; size > 0 ; offset += len, size -= len, line += len, linenr++) {
		static const struct opentry {
			const char *str;
			int (*fn)(const char *, struct patch *);
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
			{ "rename old ", gitdiff_renamesrc },
			{ "rename new ", gitdiff_renamedst },
			{ "rename from ", gitdiff_renamesrc },
			{ "rename to ", gitdiff_renamedst },
			{ "similarity index ", gitdiff_similarity },
			{ "dissimilarity index ", gitdiff_dissimilarity },
			{ "", gitdiff_unrecognized },
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
			if (p->fn(line + oplen, patch) < 0)
				return offset;
			break;
		}
	}

	return offset;
}

static int parse_num(const char *line, unsigned long *p)
{
	char *ptr;

	if (!isdigit(*line))
		return 0;
	*p = strtoul(line, &ptr, 10);
	return ptr - line;
}

static int parse_range(const char *line, int len, int offset, const char *expect,
			unsigned long *p1, unsigned long *p2)
{
	int digits, ex;

	if (offset < 0 || offset >= len)
		return -1;
	line += offset;
	len -= offset;

	digits = parse_num(line, p1);
	if (!digits)
		return -1;

	offset += digits;
	line += digits;
	len -= digits;

	*p2 = *p1;
	if (*line == ',') {
		digits = parse_num(line+1, p2);
		if (!digits)
			return -1;

		offset += digits+1;
		line += digits+1;
		len -= digits+1;
	}

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
static int parse_fragment_header(char *line, int len, struct fragment *fragment)
{
	int offset;

	if (!len || line[len-1] != '\n')
		return -1;

	/* Figure out the number of lines in a fragment */
	offset = parse_range(line, len, 4, " +", &fragment->oldpos, &fragment->oldlines);
	offset = parse_range(line, len, offset, " @@", &fragment->newpos, &fragment->newlines);

	return offset;
}

static int find_header(char *line, unsigned long size, int *hdrsize, struct patch *patch)
{
	unsigned long offset, len;

	patch->is_rename = patch->is_copy = 0;
	patch->is_new = patch->is_delete = -1;
	patch->old_mode = patch->new_mode = 0;
	patch->old_name = patch->new_name = NULL;
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
			struct fragment dummy;
			if (parse_fragment_header(line, len, &dummy) < 0)
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
			int git_hdr_len = parse_git_header(line, len, size, patch);
			if (git_hdr_len <= len)
				continue;
			if (!patch->old_name && !patch->new_name) {
				if (!patch->def_name)
					die("git diff header lacks filename information (line %d)", linenr);
				patch->old_name = patch->new_name = patch->def_name;
			}
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
		parse_traditional_patch(line, line+len, patch);
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
static int parse_fragment(char *line, unsigned long size, struct patch *patch, struct fragment *fragment)
{
	int added, deleted;
	int len = linelen(line, size), offset;
	unsigned long oldlines, newlines;

	offset = parse_fragment_header(line, len, fragment);
	if (offset < 0)
		return -1;
	oldlines = fragment->oldlines;
	newlines = fragment->newlines;

	if (patch->is_new < 0) {
		patch->is_new =  !oldlines;
		if (!oldlines)
			patch->old_name = NULL;
	}
	if (patch->is_delete < 0) {
		patch->is_delete = !newlines;
		if (!newlines)
			patch->new_name = NULL;
	}

	if (patch->is_new != !oldlines)
		return error("new file depends on old contents");
	if (patch->is_delete != !newlines) {
		if (newlines)
			return error("deleted file still has contents");
		fprintf(stderr, "** warning: file %s becomes empty but is not deleted\n", patch->new_name);
	}

	/* Parse the thing.. */
	line += len;
	size -= len;
	linenr++;
	added = deleted = 0;
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
			deleted++;
			oldlines--;
			break;
		case '+':
			added++;
			newlines--;
			break;
		/* We allow "\ No newline at end of file" */
		case '\\':
			if (len < 12 || memcmp(line, "\\ No newline", 12))
				return -1;
			break;
		}
	}
	/* If a fragment ends with an incomplete line, we failed to include
	 * it in the above loop because we hit oldlines == newlines == 0
	 * before seeing it.
	 */
	if (12 < size && !memcmp(line, "\\ No newline", 12))
		offset += linelen(line, size);

	patch->lines_added += added;
	patch->lines_deleted += deleted;
	return offset;
}

static int parse_single_patch(char *line, unsigned long size, struct patch *patch)
{
	unsigned long offset = 0;
	struct fragment **fragp = &patch->fragments;

	while (size > 4 && !memcmp(line, "@@ -", 4)) {
		struct fragment *fragment;
		int len;

		fragment = xmalloc(sizeof(*fragment));
		memset(fragment, 0, sizeof(*fragment));
		len = parse_fragment(line, size, patch, fragment);
		if (len <= 0)
			die("corrupt patch at line %d", linenr);

		fragment->patch = line;
		fragment->size = len;

		*fragp = fragment;
		fragp = &fragment->next;

		offset += len;
		line += len;
		size -= len;
	}
	return offset;
}

static int parse_chunk(char *buffer, unsigned long size, struct patch *patch)
{
	int hdrsize, patchsize;
	int offset = find_header(buffer, size, &hdrsize, patch);

	if (offset < 0)
		return offset;

	patchsize = parse_single_patch(buffer + offset + hdrsize, size - offset - hdrsize, patch);

	return offset + hdrsize + patchsize;
}

static const char pluses[] = "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++";
static const char minuses[]= "----------------------------------------------------------------------";

static void show_stats(struct patch *patch)
{
	const char *prefix = "";
	char *name = patch->new_name;
	int len, max, add, del, total;

	if (!name)
		name = patch->old_name;

	/*
	 * "scale" the filename
	 */
	len = strlen(name);
	max = max_len;
	if (max > 50)
		max = 50;
	if (len > max) {
		char *slash;
		prefix = "...";
		max -= 3;
		name += len - max;
		slash = strchr(name, '/');
		if (slash)
			name = slash;
	}
	len = max;

	/*
	 * scale the add/delete
	 */
	max = max_change;
	if (max + len > 70)
		max = 70 - len;

	add = patch->lines_added;
	del = patch->lines_deleted;
	total = add + del;

	if (max_change > 0) {
		total = (total * max + max_change / 2) / max_change;
		add = (add * max + max_change / 2) / max_change;
		del = total - add;
	}
	printf(" %s%-*s |%5d %.*s%.*s\n", prefix,
		len, name, patch->lines_added + patch->lines_deleted,
		add, pluses, del, minuses);
}

static int read_old_data(struct stat *st, const char *path, void *buf, unsigned long size)
{
	int fd;
	unsigned long got;

	switch (st->st_mode & S_IFMT) {
	case S_IFLNK:
		return readlink(path, buf, size);
	case S_IFREG:
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return error("unable to open %s", path);
		got = 0;
		for (;;) {
			int ret = read(fd, buf + got, size - got);
			if (ret < 0) {
				if (errno == EAGAIN)
					continue;
				break;
			}
			if (!ret)
				break;
			got += ret;
		}
		close(fd);
		return got;

	default:
		return -1;
	}
}

static int find_offset(const char *buf, unsigned long size, const char *fragment, unsigned long fragsize, int line)
{
	int i;
	unsigned long start, backwards, forwards;

	if (fragsize > size)
		return -1;

	start = 0;
	if (line > 1) {
		unsigned long offset = 0;
		i = line-1;
		while (offset + fragsize <= size) {
			if (buf[offset++] == '\n') {
				start = offset;
				if (!--i)
					break;
			}
		}
	}

	/* Exact line number? */
	if (!memcmp(buf + start, fragment, fragsize))
		return start;

	/*
	 * There's probably some smart way to do this, but I'll leave
	 * that to the smart and beautiful people. I'm simple and stupid.
	 */
	backwards = start;
	forwards = start;
	for (i = 0; ; i++) {
		unsigned long try;
		int n;

		/* "backward" */
		if (i & 1) {
			if (!backwards) {
				if (forwards + fragsize > size)
					break;
				continue;
			}
			do {
				--backwards;
			} while (backwards && buf[backwards-1] != '\n');
			try = backwards;
		} else {
			while (forwards + fragsize <= size) {
				if (buf[forwards++] == '\n')
					break;
			}
			try = forwards;
		}

		if (try + fragsize > size)
			continue;
		if (memcmp(buf + try, fragment, fragsize))
			continue;
		n = (i >> 1)+1;
		if (i & 1)
			n = -n;
		return try;
	}

	/*
	 * We should start searching forward and backward.
	 */
	return -1;
}

struct buffer_desc {
	char *buffer;
	unsigned long size;
	unsigned long alloc;
};

static int apply_one_fragment(struct buffer_desc *desc, struct fragment *frag)
{
	char *buf = desc->buffer;
	const char *patch = frag->patch;
	int offset, size = frag->size;
	char *old = xmalloc(size);
	char *new = xmalloc(size);
	int oldsize = 0, newsize = 0;

	while (size > 0) {
		int len = linelen(patch, size);
		int plen;

		if (!len)
			break;

		/*
		 * "plen" is how much of the line we should use for
		 * the actual patch data. Normally we just remove the
		 * first character on the line, but if the line is
		 * followed by "\ No newline", then we also remove the
		 * last one (which is the newline, of course).
		 */
		plen = len-1;
		if (len < size && patch[len] == '\\')
			plen--;
		switch (*patch) {
		case ' ':
		case '-':
			memcpy(old + oldsize, patch + 1, plen);
			oldsize += plen;
			if (*patch == '-')
				break;
		/* Fall-through for ' ' */
		case '+':
			memcpy(new + newsize, patch + 1, plen);
			newsize += plen;
			break;
		case '@': case '\\':
			/* Ignore it, we already handled it */
			break;
		default:
			return -1;
		}
		patch += len;
		size -= len;
	}

	offset = find_offset(buf, desc->size, old, oldsize, frag->newpos);
	if (offset >= 0) {
		int diff = newsize - oldsize;
		unsigned long size = desc->size + diff;
		unsigned long alloc = desc->alloc;

		if (size > alloc) {
			alloc = size + 8192;
			desc->alloc = alloc;
			buf = xrealloc(buf, alloc);
			desc->buffer = buf;
		}
		desc->size = size;
		memmove(buf + offset + newsize, buf + offset + oldsize, size - offset - newsize);
		memcpy(buf + offset, new, newsize);
		offset = 0;
	}

	free(old);
	free(new);
	return offset;
}

static int apply_fragments(struct buffer_desc *desc, struct patch *patch)
{
	struct fragment *frag = patch->fragments;

	while (frag) {
		if (apply_one_fragment(desc, frag) < 0)
			return error("patch failed: %s:%d", patch->old_name, frag->oldpos);
		frag = frag->next;
	}
	return 0;
}

static int apply_data(struct patch *patch, struct stat *st)
{
	char *buf;
	unsigned long size, alloc;
	struct buffer_desc desc;

	size = 0;
	alloc = 0;
	buf = NULL;
	if (patch->old_name) {
		size = st->st_size;
		alloc = size + 8192;
		buf = xmalloc(alloc);
		if (read_old_data(st, patch->old_name, buf, alloc) != size)
			return error("read of %s failed", patch->old_name);
	}

	desc.size = size;
	desc.alloc = alloc;
	desc.buffer = buf;
	if (apply_fragments(&desc, patch) < 0)
		return -1;
	patch->result = desc.buffer;
	patch->resultsize = desc.size;

	if (patch->is_delete && patch->resultsize)
		return error("removal patch leaves file contents");

	return 0;
}

static int check_patch(struct patch *patch)
{
	struct stat st;
	const char *old_name = patch->old_name;
	const char *new_name = patch->new_name;

	if (old_name) {
		int changed;

		if (lstat(old_name, &st) < 0)
			return error("%s: %s", old_name, strerror(errno));
		if (check_index) {
			int pos = cache_name_pos(old_name, strlen(old_name));
			if (pos < 0)
				return error("%s: does not exist in index", old_name);
			changed = ce_match_stat(active_cache[pos], &st);
			if (changed)
				return error("%s: does not match index", old_name);
		}
		if (patch->is_new < 0)
			patch->is_new = 0;
		st.st_mode = ntohl(create_ce_mode(st.st_mode));
		if (!patch->old_mode)
			patch->old_mode = st.st_mode;
		if ((st.st_mode ^ patch->old_mode) & S_IFMT)
			return error("%s: wrong type", old_name);
		if (st.st_mode != patch->old_mode)
			fprintf(stderr, "warning: %s has type %o, expected %o\n",
				old_name, st.st_mode, patch->old_mode);
	}

	if (new_name && (patch->is_new | patch->is_rename | patch->is_copy)) {
		if (check_index && cache_name_pos(new_name, strlen(new_name)) >= 0)
			return error("%s: already exists in index", new_name);
		if (!lstat(new_name, &st))
			return error("%s: already exists in working directory", new_name);
		if (errno != ENOENT)
			return error("%s: %s", new_name, strerror(errno));
		if (!patch->new_mode)
			patch->new_mode = S_IFREG | 0644;
	}

	if (new_name && old_name) {
		int same = !strcmp(old_name, new_name);
		if (!patch->new_mode)
			patch->new_mode = patch->old_mode;
		if ((patch->old_mode ^ patch->new_mode) & S_IFMT)
			return error("new mode (%o) of %s does not match old mode (%o)%s%s",
				patch->new_mode, new_name, patch->old_mode,
				same ? "" : " of ", same ? "" : old_name);
	}	

	if (apply_data(patch, &st) < 0)
		return error("%s: patch does not apply", old_name);
	return 0;
}

static int check_patch_list(struct patch *patch)
{
	int error = 0;

	for (;patch ; patch = patch->next)
		error |= check_patch(patch);
	return error;
}

static void show_file(int c, unsigned int mode, const char *name)
{
	printf("%c %o %s\n", c, mode, name);
}

static void show_file_list(struct patch *patch)
{
	for (;patch ; patch = patch->next) {
		if (patch->is_rename) {
			show_file('-', patch->old_mode, patch->old_name);
			show_file('+', patch->new_mode, patch->new_name);
			continue;
		}
		if (patch->is_copy || patch->is_new) {
			show_file('+', patch->new_mode, patch->new_name);
			continue;
		}
		if (patch->is_delete) {
			show_file('-', patch->old_mode, patch->old_name);
			continue;
		}
		if (patch->old_mode && patch->new_mode && patch->old_mode != patch->new_mode) {
			printf("M %o:%o %s\n", patch->old_mode, patch->new_mode, patch->old_name);
			continue;
		}
		printf("M %o %s\n", patch->old_mode, patch->old_name);
	}
}

static void stat_patch_list(struct patch *patch)
{
	int files, adds, dels;

	for (files = adds = dels = 0 ; patch ; patch = patch->next) {
		files++;
		adds += patch->lines_added;
		dels += patch->lines_deleted;
		show_stats(patch);
	}

	printf(" %d files changed, %d insertions(+), %d deletions(-)\n", files, adds, dels);
}

static void show_file_mode_name(const char *newdelete, unsigned int mode, const char *name)
{
	if (mode)
		printf(" %s mode %06o %s\n", newdelete, mode, name);
	else
		printf(" %s %s\n", newdelete, name);
}

static void show_mode_change(struct patch *p, int show_name)
{
	if (p->old_mode && p->new_mode && p->old_mode != p->new_mode) {
		if (show_name)
			printf(" mode change %06o => %06o %s\n",
			       p->old_mode, p->new_mode, p->new_name);
		else
			printf(" mode change %06o => %06o\n",
			       p->old_mode, p->new_mode);
	}
}

static void show_rename_copy(struct patch *p)
{
	const char *renamecopy = p->is_rename ? "rename" : "copy";
	const char *old, *new;

	/* Find common prefix */
	old = p->old_name;
	new = p->new_name;
	while (1) {
		const char *slash_old, *slash_new;
		slash_old = strchr(old, '/');
		slash_new = strchr(new, '/');
		if (!slash_old ||
		    !slash_new ||
		    slash_old - old != slash_new - new ||
		    memcmp(old, new, slash_new - new))
			break;
		old = slash_old + 1;
		new = slash_new + 1;
	}
	/* p->old_name thru old is the common prefix, and old and new
	 * through the end of names are renames
	 */
	if (old != p->old_name)
		printf(" %s %.*s{%s => %s} (%d%%)\n", renamecopy,
		       (int)(old - p->old_name), p->old_name,
		       old, new, p->score);
	else
		printf(" %s %s => %s (%d%%)\n", renamecopy,
		       p->old_name, p->new_name, p->score);
	show_mode_change(p, 0);
}

static void summary_patch_list(struct patch *patch)
{
	struct patch *p;

	for (p = patch; p; p = p->next) {
		if (p->is_new)
			show_file_mode_name("create", p->new_mode, p->new_name);
		else if (p->is_delete)
			show_file_mode_name("delete", p->old_mode, p->old_name);
		else {
			if (p->is_rename || p->is_copy)
				show_rename_copy(p);
			else {
				if (p->score) {
					printf(" rewrite %s (%d%%)\n",
					       p->new_name, p->score);
					show_mode_change(p, 0);
				}
				else
					show_mode_change(p, 1);
			}
		}
	}
}

static void patch_stats(struct patch *patch)
{
	int lines = patch->lines_added + patch->lines_deleted;

	if (lines > max_change)
		max_change = lines;
	if (patch->old_name) {
		int len = strlen(patch->old_name);
		if (len > max_len)
			max_len = len;
	}
	if (patch->new_name) {
		int len = strlen(patch->new_name);
		if (len > max_len)
			max_len = len;
	}
}

static void remove_file(struct patch *patch)
{
	if (write_index) {
		if (remove_file_from_cache(patch->old_name) < 0)
			die("unable to remove %s from index", patch->old_name);
	}
	unlink(patch->old_name);
}

static void add_index_file(const char *path, unsigned mode, void *buf, unsigned long size)
{
	struct stat st;
	struct cache_entry *ce;
	int namelen = strlen(path);
	unsigned ce_size = cache_entry_size(namelen);

	if (!write_index)
		return;

	ce = xmalloc(ce_size);
	memset(ce, 0, ce_size);
	memcpy(ce->name, path, namelen);
	ce->ce_mode = create_ce_mode(mode);
	ce->ce_flags = htons(namelen);
	if (lstat(path, &st) < 0)
		die("unable to stat newly created file %s", path);
	fill_stat_cache_info(ce, &st);
	if (write_sha1_file(buf, size, "blob", ce->sha1) < 0)
		die("unable to create backing store for newly created file %s", path);
	if (add_cache_entry(ce, ADD_CACHE_OK_TO_ADD) < 0)
		die("unable to add cache entry for %s", path);
}

static void create_subdirectories(const char *path)
{
	int len = strlen(path);
	char *buf = xmalloc(len + 1);
	const char *slash = path;

	while ((slash = strchr(slash+1, '/')) != NULL) {
		len = slash - path;
		memcpy(buf, path, len);
		buf[len] = 0;
		if (mkdir(buf, 0777) < 0) {
			if (errno != EEXIST)
				break;
		}
	}
	free(buf);
}

static int try_create_file(const char *path, unsigned int mode, const char *buf, unsigned long size)
{
	int fd;

	if (S_ISLNK(mode))
		return symlink(buf, path);
	fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_TRUNC, (mode & 0100) ? 0777 : 0666);
	if (fd < 0)
		return -1;
	while (size) {
		int written = write(fd, buf, size);
		if (written < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			die("writing file %s: %s", path, strerror(errno));
		}
		if (!written)
			die("out of space writing file %s", path);
		buf += written;
		size -= written;
	}
	if (close(fd) < 0)
		die("closing file %s: %s", path, strerror(errno));
	return 0;
}

/*
 * We optimistically assume that the directories exist,
 * which is true 99% of the time anyway. If they don't,
 * we create them and try again.
 */
static void create_one_file(const char *path, unsigned mode, const char *buf, unsigned long size)
{
	if (!try_create_file(path, mode, buf, size))
		return;

	if (errno == ENOENT) {
		create_subdirectories(path);
		if (!try_create_file(path, mode, buf, size))
			return;
	}

	if (errno == EEXIST) {
		unsigned int nr = getpid();

		for (;;) {
			const char *newpath;
			newpath = mkpath("%s~%u", path, nr);
			if (!try_create_file(newpath, mode, buf, size)) {
				if (!rename(newpath, path))
					return;
				unlink(newpath);
				break;
			}
			if (errno != EEXIST)
				break;
		}			
	}
	die("unable to write file %s mode %o", path, mode);
}

static void create_file(struct patch *patch)
{
	const char *path = patch->new_name;
	unsigned mode = patch->new_mode;
	unsigned long size = patch->resultsize;
	char *buf = patch->result;

	if (!mode)
		mode = S_IFREG | 0644;
	create_one_file(path, mode, buf, size);	
	add_index_file(path, mode, buf, size);
}

static void write_out_one_result(struct patch *patch)
{
	if (patch->is_delete > 0) {
		remove_file(patch);
		return;
	}
	if (patch->is_new > 0 || patch->is_copy) {
		create_file(patch);
		return;
	}
	/*
	 * Rename or modification boils down to the same
	 * thing: remove the old, write the new
	 */
	remove_file(patch);
	create_file(patch);
}

static void write_out_results(struct patch *list, int skipped_patch)
{
	if (!list && !skipped_patch)
		die("No changes");

	while (list) {
		write_out_one_result(list);
		list = list->next;
	}
}

static struct cache_file cache_file;

static struct excludes {
	struct excludes *next;
	const char *path;
} *excludes;

static int use_patch(struct patch *p)
{
	const char *pathname = p->new_name ? : p->old_name;
	struct excludes *x = excludes;
	while (x) {
		if (fnmatch(x->path, pathname, 0) == 0)
			return 0;
		x = x->next;
	}
	return 1;
}

static int apply_patch(int fd)
{
	int newfd;
	unsigned long offset, size;
	char *buffer = read_patch_file(fd, &size);
	struct patch *list = NULL, **listp = &list;
	int skipped_patch = 0;

	if (!buffer)
		return -1;
	offset = 0;
	while (size > 0) {
		struct patch *patch;
		int nr;

		patch = xmalloc(sizeof(*patch));
		memset(patch, 0, sizeof(*patch));
		nr = parse_chunk(buffer + offset, size, patch);
		if (nr < 0)
			break;
		if (use_patch(patch)) {
			patch_stats(patch);
			*listp = patch;
			listp = &patch->next;
		} else {
			/* perhaps free it a bit better? */
			free(patch);
			skipped_patch++;
		}
		offset += nr;
		size -= nr;
	}

	newfd = -1;
	write_index = check_index && apply;
	if (write_index)
		newfd = hold_index_file_for_update(&cache_file, get_index_file());
	if (check_index) {
		if (read_cache() < 0)
			die("unable to read index file");
	}

	if ((check || apply) && check_patch_list(list) < 0)
		exit(1);

	if (apply)
		write_out_results(list, skipped_patch);

	if (write_index) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_index_file(&cache_file))
			die("Unable to write new cachefile");
	}

	if (show_files)
		show_file_list(list);

	if (diffstat)
		stat_patch_list(list);

	if (summary)
		summary_patch_list(list);

	free(buffer);
	return 0;
}

int main(int argc, char **argv)
{
	int i;
	int read_stdin = 1;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		int fd;

		if (!strcmp(arg, "-")) {
			apply_patch(0);
			read_stdin = 0;
			continue;
		}
		if (!strncmp(arg, "--exclude=", 10)) {
			struct excludes *x = xmalloc(sizeof(*x));
			x->path = arg + 10;
			x->next = excludes;
			excludes = x;
			continue;
		}
		/* NEEDSWORK: this does not do anything at this moment. */
		if (!strcmp(arg, "--no-merge")) {
			merge_patch = 0;
			continue;
		}
		if (!strcmp(arg, "--stat")) {
			apply = 0;
			diffstat = 1;
			continue;
		}
		if (!strcmp(arg, "--summary")) {
			apply = 0;
			summary = 1;
			continue;
		}
		if (!strcmp(arg, "--check")) {
			apply = 0;
			check = 1;
			continue;
		}
		if (!strcmp(arg, "--index")) {
			check_index = 1;
			continue;
		}
		if (!strcmp(arg, "--apply")) {
			apply = 1;
			continue;
		}
		if (!strcmp(arg, "--show-files")) {
			show_files = 1;
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
