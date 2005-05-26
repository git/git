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
static int diffstat = 0;
static int check = 1;
static const char apply_usage[] = "git-apply <patch>";

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
	struct fragment *fragments;
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

#define TERM_EXIST	1
#define TERM_SPACE	2
#define TERM_TAB	4

static int name_terminate(const char *name, int namelen, int c, int terminate)
{
	if (c == ' ' && !(terminate & TERM_SPACE))
		return 0;
	if (c == '\t' && !(terminate & TERM_TAB))
		return 0;

	/*
	 * Do we want an existing name? Return false and
	 * continue if it's not there.
	 */
	if (terminate & TERM_EXIST)
		return cache_name_pos(name, namelen) >= 0;

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
		name = find_name(first, NULL, p_value, TERM_EXIST | TERM_SPACE | TERM_TAB);
		patch->old_name = name;
	} else {
		name = find_name(first, NULL, p_value, TERM_EXIST | TERM_SPACE | TERM_TAB);
		name = find_name(second, name, p_value, TERM_EXIST | TERM_SPACE | TERM_TAB);
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
			if (!memcmp(name, second, len)) {
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
			{ "rename from ", gitdiff_renamesrc },
			{ "rename to ", gitdiff_renamedst },
			{ "similarity index ", gitdiff_similarity },
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
			if (git_hdr_len < 0)
				continue;
			if (!patch->old_name && !patch->new_name)
				die("git diff header lacks filename information");
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
	unsigned long pos[4], oldlines, newlines;

	offset = parse_fragment_header(line, len, fragment);
	if (offset < 0)
		return -1;
	oldlines = fragment->oldlines;
	newlines = fragment->newlines;

	if (patch->is_new < 0 && (pos[0] || oldlines))
		patch->is_new = 0;
	if (patch->is_delete < 0 && (pos[1] || newlines))
		patch->is_delete = 0;

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
			break;
		}
	}
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

const char pluses[] = "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++";
const char minuses[]= "----------------------------------------------------------------------";

static void show_stats(struct patch *patch)
{
	char *name = patch->old_name;
	int len, max, add, del;

	if (!name)
		name = patch->new_name;

	/*
	 * "scale" the filename
	 */
	len = strlen(name);
	max = max_len;
	if (max > 50)
		max = 50;
	if (len > max)
		name += len - max;
	len = max;

	/*
	 * scale the add/delete
	 */
	max = max_change;
	if (max + len > 70)
		max = 70 - len;
	
	add = (patch->lines_added * max + max_change/2) / max_change;
	del = (patch->lines_deleted * max + max_change/2) / max_change;
	printf(" %-*s |%5d %.*s%.*s\n",
		len, name, patch->lines_added + patch->lines_deleted,
		add, pluses, del, minuses);
}

static void check_patch(struct patch *patch)
{
	const char *old_name = patch->old_name;
	const char *new_name = patch->new_name;

	if (old_name) {
		if (cache_name_pos(old_name, strlen(old_name)) < 0)
			die("file %s does not exist", old_name);
		if (patch->is_new < 0)
			patch->is_new = 0;
	}
	if (new_name && (patch->is_new | patch->is_rename | patch->is_copy)) {
		if (cache_name_pos(new_name, strlen(new_name)) >= 0)
			die("file %s already exists", new_name);
	}
}

static void apply_patch_list(struct patch *patch)
{
	int files, adds, dels;

	files = adds = dels = 0;
	if (!patch)
		die("no patch found");
	do {
		if (check)
			check_patch(patch);

		if (diffstat) {
			files++;
			adds += patch->lines_added;
			dels += patch->lines_deleted;
			show_stats(patch);
		}
	} while ((patch = patch->next) != NULL);

	if (diffstat)
		printf(" %d files changed, %d insertions(+), %d deletions(-)\n", files, adds, dels);
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

static int apply_patch(int fd)
{
	unsigned long offset, size;
	char *buffer = read_patch_file(fd, &size);
	struct patch *list = NULL, **listp = &list;

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
		patch_stats(patch);
		*listp = patch;
		listp = &patch->next;
		offset += nr;
		size -= nr;
	}

	apply_patch_list(list);

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
		if (!strcmp(arg, "--stat")) {
			check = 0;
			diffstat = 1;
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
