/*
 * apply.c
 *
 * Copyright (C) Linus Torvalds, 2005
 *
 * This applies patches on top of some (arbitrary) version of the SCM.
 *
 */
#include "cache.h"
#include "cache-tree.h"
#include "quote.h"
#include "blob.h"
#include "delta.h"
#include "builtin.h"

/*
 *  --check turns on checking that the working tree matches the
 *    files that are being modified, but doesn't apply the patch
 *  --stat does just a diffstat, and doesn't actually apply
 *  --numstat does numeric diffstat, and doesn't actually apply
 *  --index-info shows the old and new index info for paths if available.
 *  --index updates the cache as well.
 *  --cached updates only the cache without ever touching the working tree.
 */
static const char *prefix;
static int prefix_length = -1;
static int newfd = -1;

static int unidiff_zero;
static int p_value = 1;
static int p_value_known;
static int check_index;
static int update_index;
static int cached;
static int diffstat;
static int numstat;
static int summary;
static int check;
static int apply = 1;
static int apply_in_reverse;
static int apply_with_reject;
static int apply_verbosely;
static int no_add;
static int show_index_info;
static int line_termination = '\n';
static unsigned long p_context = ULONG_MAX;
static const char apply_usage[] =
"git-apply [--stat] [--numstat] [--summary] [--check] [--index] [--cached] [--apply] [--no-add] [--index-info] [--allow-binary-replacement] [--reverse] [--reject] [--verbose] [-z] [-pNUM] [-CNUM] [--whitespace=<nowarn|warn|error|error-all|strip>] <patch>...";

static enum whitespace_eol {
	nowarn_whitespace,
	warn_on_whitespace,
	error_on_whitespace,
	strip_whitespace,
} new_whitespace = warn_on_whitespace;
static int whitespace_error;
static int squelch_whitespace_errors = 5;
static int applied_after_stripping;
static const char *patch_input_file;

static void parse_whitespace_option(const char *option)
{
	if (!option) {
		new_whitespace = warn_on_whitespace;
		return;
	}
	if (!strcmp(option, "warn")) {
		new_whitespace = warn_on_whitespace;
		return;
	}
	if (!strcmp(option, "nowarn")) {
		new_whitespace = nowarn_whitespace;
		return;
	}
	if (!strcmp(option, "error")) {
		new_whitespace = error_on_whitespace;
		return;
	}
	if (!strcmp(option, "error-all")) {
		new_whitespace = error_on_whitespace;
		squelch_whitespace_errors = 0;
		return;
	}
	if (!strcmp(option, "strip")) {
		new_whitespace = strip_whitespace;
		return;
	}
	die("unrecognized whitespace option '%s'", option);
}

static void set_default_whitespace_mode(const char *whitespace_option)
{
	if (!whitespace_option && !apply_default_whitespace) {
		new_whitespace = (apply
				  ? warn_on_whitespace
				  : nowarn_whitespace);
	}
}

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

/*
 * This represents one "hunk" from a patch, starting with
 * "@@ -oldpos,oldlines +newpos,newlines @@" marker.  The
 * patch text is pointed at by patch, and its byte length
 * is stored in size.  leading and trailing are the number
 * of context lines.
 */
struct fragment {
	unsigned long leading, trailing;
	unsigned long oldpos, oldlines;
	unsigned long newpos, newlines;
	const char *patch;
	int size;
	int rejected;
	struct fragment *next;
};

/*
 * When dealing with a binary patch, we reuse "leading" field
 * to store the type of the binary hunk, either deflated "delta"
 * or deflated "literal".
 */
#define binary_patch_method leading
#define BINARY_DELTA_DEFLATED	1
#define BINARY_LITERAL_DEFLATED 2

struct patch {
	char *new_name, *old_name, *def_name;
	unsigned int old_mode, new_mode;
	int is_new, is_delete;	/* -1 = unknown, 0 = false, 1 = true */
	int rejected;
	unsigned long deflate_origlen;
	int lines_added, lines_deleted;
	int score;
	unsigned int is_toplevel_relative:1;
	unsigned int inaccurate_eof:1;
	unsigned int is_binary:1;
	unsigned int is_copy:1;
	unsigned int is_rename:1;
	struct fragment *fragments;
	char *result;
	unsigned long resultsize;
	char old_sha1_prefix[41];
	char new_sha1_prefix[41];
	struct patch *next;
};

static void say_patch_name(FILE *output, const char *pre, struct patch *patch, const char *post)
{
	fputs(pre, output);
	if (patch->old_name && patch->new_name &&
	    strcmp(patch->old_name, patch->new_name)) {
		write_name_quoted(NULL, 0, patch->old_name, 1, output);
		fputs(" => ", output);
		write_name_quoted(NULL, 0, patch->new_name, 1, output);
	}
	else {
		const char *n = patch->new_name;
		if (!n)
			n = patch->old_name;
		write_name_quoted(NULL, 0, n, 1, output);
	}
	fputs(post, output);
}

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
		nr = xread(fd, (char *) buffer + size, nr);
		if (!nr)
			break;
		if (nr < 0)
			die("git-apply: read returned %s", strerror(errno));
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
	memset((char *) buffer + size, 0, SLOP);
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

static char *find_name(const char *line, char *def, int p_value, int terminate)
{
	int len;
	const char *start = line;
	char *name;

	if (*line == '"') {
		/* Proposed "new-style" GNU patch/diff format; see
		 * http://marc.theaimsgroup.com/?l=git&m=112927316408690&w=2
		 */
		name = unquote_c_style(line, NULL);
		if (name) {
			char *cp = name;
			while (p_value) {
				cp = strchr(name, '/');
				if (!cp)
					break;
				cp++;
				p_value--;
			}
			if (cp) {
				/* name can later be freed, so we need
				 * to memmove, not just return cp
				 */
				memmove(name, cp, strlen(cp) + 1);
				free(def);
				return name;
			}
			else {
				free(name);
				name = NULL;
			}
		}
	}

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

static int count_slashes(const char *cp)
{
	int cnt = 0;
	char ch;

	while ((ch = *cp++))
		if (ch == '/')
			cnt++;
	return cnt;
}

/*
 * Given the string after "--- " or "+++ ", guess the appropriate
 * p_value for the given patch.
 */
static int guess_p_value(const char *nameline)
{
	char *name, *cp;
	int val = -1;

	if (is_dev_null(nameline))
		return -1;
	name = find_name(nameline, NULL, 0, TERM_SPACE | TERM_TAB);
	if (!name)
		return -1;
	cp = strchr(name, '/');
	if (!cp)
		val = 0;
	else if (prefix) {
		/*
		 * Does it begin with "a/$our-prefix" and such?  Then this is
		 * very likely to apply to our directory.
		 */
		if (!strncmp(name, prefix, prefix_length))
			val = count_slashes(prefix);
		else {
			cp++;
			if (!strncmp(cp, prefix, prefix_length))
				val = count_slashes(prefix) + 1;
		}
	}
	free(name);
	return val;
}

/*
 * Get the name etc info from the --/+++ lines of a traditional patch header
 *
 * FIXME! The end-of-filename heuristics are kind of screwy. For existing
 * files, we can happily check the index for a match, but for creating a
 * new file we should try to match whatever "patch" does. I have no idea.
 */
static void parse_traditional_patch(const char *first, const char *second, struct patch *patch)
{
	char *name;

	first += 4;	/* skip "--- " */
	second += 4;	/* skip "+++ " */
	if (!p_value_known) {
		int p, q;
		p = guess_p_value(first);
		q = guess_p_value(second);
		if (p < 0) p = q;
		if (0 <= p && p == q) {
			p_value = p;
			p_value_known = 1;
		}
	}
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
	if (!orig_name && !isnull)
		return find_name(line, NULL, p_value, TERM_TAB);

	if (orig_name) {
		int len;
		const char *name;
		char *another;
		name = orig_name;
		len = strlen(name);
		if (isnull)
			die("git-apply: bad git-diff - expected /dev/null, got %s on line %d", name, linenr);
		another = find_name(line, NULL, p_value, TERM_TAB);
		if (!another || memcmp(another, name, len))
			die("git-apply: bad git-diff - inconsistent %s filename on line %d", oldnew, linenr);
		free(another);
		return orig_name;
	}
	else {
		/* expect "/dev/null" */
		if (memcmp("/dev/null", line, 9) || line[9] != '\n')
			die("git-apply: bad git-diff - expected /dev/null on line %d", linenr);
		return NULL;
	}
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

static int gitdiff_index(const char *line, struct patch *patch)
{
	/* index line is N hexadecimal, "..", N hexadecimal,
	 * and optional space with octal mode.
	 */
	const char *ptr, *eol;
	int len;

	ptr = strchr(line, '.');
	if (!ptr || ptr[1] != '.' || 40 < ptr - line)
		return 0;
	len = ptr - line;
	memcpy(patch->old_sha1_prefix, line, len);
	patch->old_sha1_prefix[len] = 0;

	line = ptr + 2;
	ptr = strchr(line, ' ');
	eol = strchr(line, '\n');

	if (!ptr || eol < ptr)
		ptr = eol;
	len = ptr - line;

	if (40 < len)
		return 0;
	memcpy(patch->new_sha1_prefix, line, len);
	patch->new_sha1_prefix[len] = 0;
	if (*ptr == ' ')
		patch->new_mode = patch->old_mode = strtoul(ptr+1, NULL, 8);
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

static const char *stop_at_slash(const char *line, int llen)
{
	int i;

	for (i = 0; i < llen; i++) {
		int ch = line[i];
		if (ch == '/')
			return line + i;
	}
	return NULL;
}

/* This is to extract the same name that appears on "diff --git"
 * line.  We do not find and return anything if it is a rename
 * patch, and it is OK because we will find the name elsewhere.
 * We need to reliably find name only when it is mode-change only,
 * creation or deletion of an empty file.  In any of these cases,
 * both sides are the same name under a/ and b/ respectively.
 */
static char *git_header_name(char *line, int llen)
{
	int len;
	const char *name;
	const char *second = NULL;

	line += strlen("diff --git ");
	llen -= strlen("diff --git ");

	if (*line == '"') {
		const char *cp;
		char *first = unquote_c_style(line, &second);
		if (!first)
			return NULL;

		/* advance to the first slash */
		cp = stop_at_slash(first, strlen(first));
		if (!cp || cp == first) {
			/* we do not accept absolute paths */
		free_first_and_fail:
			free(first);
			return NULL;
		}
		len = strlen(cp+1);
		memmove(first, cp+1, len+1); /* including NUL */

		/* second points at one past closing dq of name.
		 * find the second name.
		 */
		while ((second < line + llen) && isspace(*second))
			second++;

		if (line + llen <= second)
			goto free_first_and_fail;
		if (*second == '"') {
			char *sp = unquote_c_style(second, NULL);
			if (!sp)
				goto free_first_and_fail;
			cp = stop_at_slash(sp, strlen(sp));
			if (!cp || cp == sp) {
			free_both_and_fail:
				free(sp);
				goto free_first_and_fail;
			}
			/* They must match, otherwise ignore */
			if (strcmp(cp+1, first))
				goto free_both_and_fail;
			free(sp);
			return first;
		}

		/* unquoted second */
		cp = stop_at_slash(second, line + llen - second);
		if (!cp || cp == second)
			goto free_first_and_fail;
		cp++;
		if (line + llen - cp != len + 1 ||
		    memcmp(first, cp, len))
			goto free_first_and_fail;
		return first;
	}

	/* unquoted first name */
	name = stop_at_slash(line, llen);
	if (!name || name == line)
		return NULL;

	name++;

	/* since the first name is unquoted, a dq if exists must be
	 * the beginning of the second name.
	 */
	for (second = name; second < line + llen; second++) {
		if (*second == '"') {
			const char *cp = second;
			const char *np;
			char *sp = unquote_c_style(second, NULL);

			if (!sp)
				return NULL;
			np = stop_at_slash(sp, strlen(sp));
			if (!np || np == sp) {
			free_second_and_fail:
				free(sp);
				return NULL;
			}
			np++;
			len = strlen(np);
			if (len < cp - name &&
			    !strncmp(np, name, len) &&
			    isspace(name[len])) {
				/* Good */
				memmove(sp, np, len + 1);
				return sp;
			}
			goto free_second_and_fail;
		}
	}

	/*
	 * Accept a name only if it shows up twice, exactly the same
	 * form.
	 */
	for (len = 0 ; ; len++) {
		switch (name[len]) {
		default:
			continue;
		case '\n':
			return NULL;
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
	patch->def_name = git_header_name(line, len);

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
			{ "index ", gitdiff_index },
			{ "", gitdiff_unrecognized },
		};
		int i;

		len = linelen(line, size);
		if (!len || line[len-1] != '\n')
			break;
		for (i = 0; i < ARRAY_SIZE(optable); i++) {
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

	*p2 = 1;
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

	patch->is_toplevel_relative = 0;
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
		 * Make sure we don't find any unconnected patch fragments.
		 * That's a sign that we didn't find a header, and that a
		 * patch has become corrupted/broken up.
		 */
		if (!memcmp("@@ -", line, 4)) {
			struct fragment dummy;
			if (parse_fragment_header(line, len, &dummy) < 0)
				continue;
			die("patch fragment without header at line %d: %.*s",
			    linenr, (int)len-1, line);
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
			patch->is_toplevel_relative = 1;
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

static void check_whitespace(const char *line, int len)
{
	const char *err = "Adds trailing whitespace";
	int seen_space = 0;
	int i;

	/*
	 * We know len is at least two, since we have a '+' and we
	 * checked that the last character was a '\n' before calling
	 * this function.  That is, an addition of an empty line would
	 * check the '+' here.  Sneaky...
	 */
	if (isspace(line[len-2]))
		goto error;

	/*
	 * Make sure that there is no space followed by a tab in
	 * indentation.
	 */
	err = "Space in indent is followed by a tab";
	for (i = 1; i < len; i++) {
		if (line[i] == '\t') {
			if (seen_space)
				goto error;
		}
		else if (line[i] == ' ')
			seen_space = 1;
		else
			break;
	}
	return;

 error:
	whitespace_error++;
	if (squelch_whitespace_errors &&
	    squelch_whitespace_errors < whitespace_error)
		;
	else
		fprintf(stderr, "%s.\n%s:%d:%.*s\n",
			err, patch_input_file, linenr, len-2, line+1);
}


/*
 * Parse a unified diff. Note that this really needs to parse each
 * fragment separately, since the only way to know the difference
 * between a "---" that is part of a patch, and a "---" that starts
 * the next patch is to look at the line counts..
 */
static int parse_fragment(char *line, unsigned long size, struct patch *patch, struct fragment *fragment)
{
	int added, deleted;
	int len = linelen(line, size), offset;
	unsigned long oldlines, newlines;
	unsigned long leading, trailing;

	offset = parse_fragment_header(line, len, fragment);
	if (offset < 0)
		return -1;
	oldlines = fragment->oldlines;
	newlines = fragment->newlines;
	leading = 0;
	trailing = 0;

	/* Parse the thing.. */
	line += len;
	size -= len;
	linenr++;
	added = deleted = 0;
	for (offset = len;
	     0 < size;
	     offset += len, size -= len, line += len, linenr++) {
		if (!oldlines && !newlines)
			break;
		len = linelen(line, size);
		if (!len || line[len-1] != '\n')
			return -1;
		switch (*line) {
		default:
			return -1;
		case '\n': /* newer GNU diff, an empty context line */
		case ' ':
			oldlines--;
			newlines--;
			if (!deleted && !added)
				leading++;
			trailing++;
			break;
		case '-':
			deleted++;
			oldlines--;
			trailing = 0;
			break;
		case '+':
			if (new_whitespace != nowarn_whitespace)
				check_whitespace(line, len);
			added++;
			newlines--;
			trailing = 0;
			break;

                /* We allow "\ No newline at end of file". Depending
                 * on locale settings when the patch was produced we
                 * don't know what this line looks like. The only
                 * thing we do know is that it begins with "\ ".
		 * Checking for 12 is just for sanity check -- any
		 * l10n of "\ No newline..." is at least that long.
		 */
		case '\\':
			if (len < 12 || memcmp(line, "\\ ", 2))
				return -1;
			break;
		}
	}
	if (oldlines || newlines)
		return -1;
	fragment->leading = leading;
	fragment->trailing = trailing;

	/* If a fragment ends with an incomplete line, we failed to include
	 * it in the above loop because we hit oldlines == newlines == 0
	 * before seeing it.
	 */
	if (12 < size && !memcmp(line, "\\ ", 2))
		offset += linelen(line, size);

	patch->lines_added += added;
	patch->lines_deleted += deleted;

	if (0 < patch->is_new && oldlines)
		return error("new file depends on old contents");
	if (0 < patch->is_delete && newlines)
		return error("deleted file still has contents");
	return offset;
}

static int parse_single_patch(char *line, unsigned long size, struct patch *patch)
{
	unsigned long offset = 0;
	unsigned long oldlines = 0, newlines = 0, context = 0;
	struct fragment **fragp = &patch->fragments;

	while (size > 4 && !memcmp(line, "@@ -", 4)) {
		struct fragment *fragment;
		int len;

		fragment = xcalloc(1, sizeof(*fragment));
		len = parse_fragment(line, size, patch, fragment);
		if (len <= 0)
			die("corrupt patch at line %d", linenr);
		fragment->patch = line;
		fragment->size = len;
		oldlines += fragment->oldlines;
		newlines += fragment->newlines;
		context += fragment->leading + fragment->trailing;

		*fragp = fragment;
		fragp = &fragment->next;

		offset += len;
		line += len;
		size -= len;
	}

	/*
	 * If something was removed (i.e. we have old-lines) it cannot
	 * be creation, and if something was added it cannot be
	 * deletion.  However, the reverse is not true; --unified=0
	 * patches that only add are not necessarily creation even
	 * though they do not have any old lines, and ones that only
	 * delete are not necessarily deletion.
	 *
	 * Unfortunately, a real creation/deletion patch do _not_ have
	 * any context line by definition, so we cannot safely tell it
	 * apart with --unified=0 insanity.  At least if the patch has
	 * more than one hunk it is not creation or deletion.
	 */
	if (patch->is_new < 0 &&
	    (oldlines || (patch->fragments && patch->fragments->next)))
		patch->is_new = 0;
	if (patch->is_delete < 0 &&
	    (newlines || (patch->fragments && patch->fragments->next)))
		patch->is_delete = 0;
	if (!unidiff_zero || context) {
		/* If the user says the patch is not generated with
		 * --unified=0, or if we have seen context lines,
		 * then not having oldlines means the patch is creation,
		 * and not having newlines means the patch is deletion.
		 */
		if (patch->is_new < 0 && !oldlines) {
			patch->is_new = 1;
			patch->old_name = NULL;
		}
		if (patch->is_delete < 0 && !newlines) {
			patch->is_delete = 1;
			patch->new_name = NULL;
		}
	}

	if (0 < patch->is_new && oldlines)
		die("new file %s depends on old contents", patch->new_name);
	if (0 < patch->is_delete && newlines)
		die("deleted file %s still has contents", patch->old_name);
	if (!patch->is_delete && !newlines && context)
		fprintf(stderr, "** warning: file %s becomes empty but "
			"is not deleted\n", patch->new_name);

	return offset;
}

static inline int metadata_changes(struct patch *patch)
{
	return	patch->is_rename > 0 ||
		patch->is_copy > 0 ||
		patch->is_new > 0 ||
		patch->is_delete ||
		(patch->old_mode && patch->new_mode &&
		 patch->old_mode != patch->new_mode);
}

static char *inflate_it(const void *data, unsigned long size,
			unsigned long inflated_size)
{
	z_stream stream;
	void *out;
	int st;

	memset(&stream, 0, sizeof(stream));

	stream.next_in = (unsigned char *)data;
	stream.avail_in = size;
	stream.next_out = out = xmalloc(inflated_size);
	stream.avail_out = inflated_size;
	inflateInit(&stream);
	st = inflate(&stream, Z_FINISH);
	if ((st != Z_STREAM_END) || stream.total_out != inflated_size) {
		free(out);
		return NULL;
	}
	return out;
}

static struct fragment *parse_binary_hunk(char **buf_p,
					  unsigned long *sz_p,
					  int *status_p,
					  int *used_p)
{
	/* Expect a line that begins with binary patch method ("literal"
	 * or "delta"), followed by the length of data before deflating.
	 * a sequence of 'length-byte' followed by base-85 encoded data
	 * should follow, terminated by a newline.
	 *
	 * Each 5-byte sequence of base-85 encodes up to 4 bytes,
	 * and we would limit the patch line to 66 characters,
	 * so one line can fit up to 13 groups that would decode
	 * to 52 bytes max.  The length byte 'A'-'Z' corresponds
	 * to 1-26 bytes, and 'a'-'z' corresponds to 27-52 bytes.
	 */
	int llen, used;
	unsigned long size = *sz_p;
	char *buffer = *buf_p;
	int patch_method;
	unsigned long origlen;
	char *data = NULL;
	int hunk_size = 0;
	struct fragment *frag;

	llen = linelen(buffer, size);
	used = llen;

	*status_p = 0;

	if (!prefixcmp(buffer, "delta ")) {
		patch_method = BINARY_DELTA_DEFLATED;
		origlen = strtoul(buffer + 6, NULL, 10);
	}
	else if (!prefixcmp(buffer, "literal ")) {
		patch_method = BINARY_LITERAL_DEFLATED;
		origlen = strtoul(buffer + 8, NULL, 10);
	}
	else
		return NULL;

	linenr++;
	buffer += llen;
	while (1) {
		int byte_length, max_byte_length, newsize;
		llen = linelen(buffer, size);
		used += llen;
		linenr++;
		if (llen == 1) {
			/* consume the blank line */
			buffer++;
			size--;
			break;
		}
		/* Minimum line is "A00000\n" which is 7-byte long,
		 * and the line length must be multiple of 5 plus 2.
		 */
		if ((llen < 7) || (llen-2) % 5)
			goto corrupt;
		max_byte_length = (llen - 2) / 5 * 4;
		byte_length = *buffer;
		if ('A' <= byte_length && byte_length <= 'Z')
			byte_length = byte_length - 'A' + 1;
		else if ('a' <= byte_length && byte_length <= 'z')
			byte_length = byte_length - 'a' + 27;
		else
			goto corrupt;
		/* if the input length was not multiple of 4, we would
		 * have filler at the end but the filler should never
		 * exceed 3 bytes
		 */
		if (max_byte_length < byte_length ||
		    byte_length <= max_byte_length - 4)
			goto corrupt;
		newsize = hunk_size + byte_length;
		data = xrealloc(data, newsize);
		if (decode_85(data + hunk_size, buffer + 1, byte_length))
			goto corrupt;
		hunk_size = newsize;
		buffer += llen;
		size -= llen;
	}

	frag = xcalloc(1, sizeof(*frag));
	frag->patch = inflate_it(data, hunk_size, origlen);
	if (!frag->patch)
		goto corrupt;
	free(data);
	frag->size = origlen;
	*buf_p = buffer;
	*sz_p = size;
	*used_p = used;
	frag->binary_patch_method = patch_method;
	return frag;

 corrupt:
	free(data);
	*status_p = -1;
	error("corrupt binary patch at line %d: %.*s",
	      linenr-1, llen-1, buffer);
	return NULL;
}

static int parse_binary(char *buffer, unsigned long size, struct patch *patch)
{
	/* We have read "GIT binary patch\n"; what follows is a line
	 * that says the patch method (currently, either "literal" or
	 * "delta") and the length of data before deflating; a
	 * sequence of 'length-byte' followed by base-85 encoded data
	 * follows.
	 *
	 * When a binary patch is reversible, there is another binary
	 * hunk in the same format, starting with patch method (either
	 * "literal" or "delta") with the length of data, and a sequence
	 * of length-byte + base-85 encoded data, terminated with another
	 * empty line.  This data, when applied to the postimage, produces
	 * the preimage.
	 */
	struct fragment *forward;
	struct fragment *reverse;
	int status;
	int used, used_1;

	forward = parse_binary_hunk(&buffer, &size, &status, &used);
	if (!forward && !status)
		/* there has to be one hunk (forward hunk) */
		return error("unrecognized binary patch at line %d", linenr-1);
	if (status)
		/* otherwise we already gave an error message */
		return status;

	reverse = parse_binary_hunk(&buffer, &size, &status, &used_1);
	if (reverse)
		used += used_1;
	else if (status) {
		/* not having reverse hunk is not an error, but having
		 * a corrupt reverse hunk is.
		 */
		free((void*) forward->patch);
		free(forward);
		return status;
	}
	forward->next = reverse;
	patch->fragments = forward;
	patch->is_binary = 1;
	return used;
}

static int parse_chunk(char *buffer, unsigned long size, struct patch *patch)
{
	int hdrsize, patchsize;
	int offset = find_header(buffer, size, &hdrsize, patch);

	if (offset < 0)
		return offset;

	patchsize = parse_single_patch(buffer + offset + hdrsize, size - offset - hdrsize, patch);

	if (!patchsize) {
		static const char *binhdr[] = {
			"Binary files ",
			"Files ",
			NULL,
		};
		static const char git_binary[] = "GIT binary patch\n";
		int i;
		int hd = hdrsize + offset;
		unsigned long llen = linelen(buffer + hd, size - hd);

		if (llen == sizeof(git_binary) - 1 &&
		    !memcmp(git_binary, buffer + hd, llen)) {
			int used;
			linenr++;
			used = parse_binary(buffer + hd + llen,
					    size - hd - llen, patch);
			if (used)
				patchsize = used + llen;
			else
				patchsize = 0;
		}
		else if (!memcmp(" differ\n", buffer + hd + llen - 8, 8)) {
			for (i = 0; binhdr[i]; i++) {
				int len = strlen(binhdr[i]);
				if (len < size - hd &&
				    !memcmp(binhdr[i], buffer + hd, len)) {
					linenr++;
					patch->is_binary = 1;
					patchsize = llen;
					break;
				}
			}
		}

		/* Empty patch cannot be applied if it is a text patch
		 * without metadata change.  A binary patch appears
		 * empty to us here.
		 */
		if ((apply || check) &&
		    (!patch->is_binary && !metadata_changes(patch)))
			die("patch with only garbage at line %d", linenr);
	}

	return offset + hdrsize + patchsize;
}

#define swap(a,b) myswap((a),(b),sizeof(a))

#define myswap(a, b, size) do {		\
	unsigned char mytmp[size];	\
	memcpy(mytmp, &a, size);		\
	memcpy(&a, &b, size);		\
	memcpy(&b, mytmp, size);		\
} while (0)

static void reverse_patches(struct patch *p)
{
	for (; p; p = p->next) {
		struct fragment *frag = p->fragments;

		swap(p->new_name, p->old_name);
		swap(p->new_mode, p->old_mode);
		swap(p->is_new, p->is_delete);
		swap(p->lines_added, p->lines_deleted);
		swap(p->old_sha1_prefix, p->new_sha1_prefix);

		for (; frag; frag = frag->next) {
			swap(frag->newpos, frag->oldpos);
			swap(frag->newlines, frag->oldlines);
		}
	}
}

static const char pluses[] = "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++";
static const char minuses[]= "----------------------------------------------------------------------";

static void show_stats(struct patch *patch)
{
	const char *prefix = "";
	char *name = patch->new_name;
	char *qname = NULL;
	int len, max, add, del, total;

	if (!name)
		name = patch->old_name;

	if (0 < (len = quote_c_style(name, NULL, NULL, 0))) {
		qname = xmalloc(len + 1);
		quote_c_style(name, qname, NULL, 0);
		name = qname;
	}

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
	if (patch->is_binary)
		printf(" %s%-*s |  Bin\n", prefix, len, name);
	else
		printf(" %s%-*s |%5d %.*s%.*s\n", prefix,
		       len, name, patch->lines_added + patch->lines_deleted,
		       add, pluses, del, minuses);
	free(qname);
}

static int read_old_data(struct stat *st, const char *path, char **buf_p, unsigned long *alloc_p, unsigned long *size_p)
{
	int fd;
	unsigned long got;
	unsigned long nsize;
	char *nbuf;
	unsigned long size = *size_p;
	char *buf = *buf_p;

	switch (st->st_mode & S_IFMT) {
	case S_IFLNK:
		return readlink(path, buf, size) != size;
	case S_IFREG:
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return error("unable to open %s", path);
		got = 0;
		for (;;) {
			int ret = xread(fd, buf + got, size - got);
			if (ret <= 0)
				break;
			got += ret;
		}
		close(fd);
		nsize = got;
		nbuf = convert_to_git(path, buf, &nsize);
		if (nbuf) {
			free(buf);
			*buf_p = nbuf;
			*alloc_p = nsize;
			*size_p = nsize;
		}
		return got != size;
	default:
		return -1;
	}
}

static int find_offset(const char *buf, unsigned long size, const char *fragment, unsigned long fragsize, int line, int *lines)
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
		*lines = n;
		return try;
	}

	/*
	 * We should start searching forward and backward.
	 */
	return -1;
}

static void remove_first_line(const char **rbuf, int *rsize)
{
	const char *buf = *rbuf;
	int size = *rsize;
	unsigned long offset;
	offset = 0;
	while (offset <= size) {
		if (buf[offset++] == '\n')
			break;
	}
	*rsize = size - offset;
	*rbuf = buf + offset;
}

static void remove_last_line(const char **rbuf, int *rsize)
{
	const char *buf = *rbuf;
	int size = *rsize;
	unsigned long offset;
	offset = size - 1;
	while (offset > 0) {
		if (buf[--offset] == '\n')
			break;
	}
	*rsize = offset + 1;
}

struct buffer_desc {
	char *buffer;
	unsigned long size;
	unsigned long alloc;
};

static int apply_line(char *output, const char *patch, int plen)
{
	/* plen is number of bytes to be copied from patch,
	 * starting at patch+1 (patch[0] is '+').  Typically
	 * patch[plen] is '\n', unless this is the incomplete
	 * last line.
	 */
	int i;
	int add_nl_to_tail = 0;
	int fixed = 0;
	int last_tab_in_indent = -1;
	int last_space_in_indent = -1;
	int need_fix_leading_space = 0;
	char *buf;

	if ((new_whitespace != strip_whitespace) || !whitespace_error ||
	    *patch != '+') {
		memcpy(output, patch + 1, plen);
		return plen;
	}

	if (1 < plen && isspace(patch[plen-1])) {
		if (patch[plen] == '\n')
			add_nl_to_tail = 1;
		plen--;
		while (0 < plen && isspace(patch[plen]))
			plen--;
		fixed = 1;
	}

	for (i = 1; i < plen; i++) {
		char ch = patch[i];
		if (ch == '\t') {
			last_tab_in_indent = i;
			if (0 <= last_space_in_indent)
				need_fix_leading_space = 1;
		}
		else if (ch == ' ')
			last_space_in_indent = i;
		else
			break;
	}

	buf = output;
	if (need_fix_leading_space) {
		/* between patch[1..last_tab_in_indent] strip the
		 * funny spaces, updating them to tab as needed.
		 */
		for (i = 1; i < last_tab_in_indent; i++, plen--) {
			char ch = patch[i];
			if (ch != ' ')
				*output++ = ch;
			else if ((i % 8) == 0)
				*output++ = '\t';
		}
		fixed = 1;
		i = last_tab_in_indent;
	}
	else
		i = 1;

	memcpy(output, patch + i, plen);
	if (add_nl_to_tail)
		output[plen++] = '\n';
	if (fixed)
		applied_after_stripping++;
	return output + plen - buf;
}

static int apply_one_fragment(struct buffer_desc *desc, struct fragment *frag, int inaccurate_eof)
{
	int match_beginning, match_end;
	char *buf = desc->buffer;
	const char *patch = frag->patch;
	int offset, size = frag->size;
	char *old = xmalloc(size);
	char *new = xmalloc(size);
	const char *oldlines, *newlines;
	int oldsize = 0, newsize = 0;
	unsigned long leading, trailing;
	int pos, lines;

	while (size > 0) {
		char first;
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
		first = *patch;
		if (apply_in_reverse) {
			if (first == '-')
				first = '+';
			else if (first == '+')
				first = '-';
		}
		switch (first) {
		case '\n':
			/* Newer GNU diff, empty context line */
			if (plen < 0)
				/* ... followed by '\No newline'; nothing */
				break;
			old[oldsize++] = '\n';
			new[newsize++] = '\n';
			break;
		case ' ':
		case '-':
			memcpy(old + oldsize, patch + 1, plen);
			oldsize += plen;
			if (first == '-')
				break;
		/* Fall-through for ' ' */
		case '+':
			if (first != '+' || !no_add)
				newsize += apply_line(new + newsize, patch,
						      plen);
			break;
		case '@': case '\\':
			/* Ignore it, we already handled it */
			break;
		default:
			if (apply_verbosely)
				error("invalid start of line: '%c'", first);
			return -1;
		}
		patch += len;
		size -= len;
	}

	if (inaccurate_eof && oldsize > 0 && old[oldsize - 1] == '\n' &&
			newsize > 0 && new[newsize - 1] == '\n') {
		oldsize--;
		newsize--;
	}

	oldlines = old;
	newlines = new;
	leading = frag->leading;
	trailing = frag->trailing;

	/*
	 * If we don't have any leading/trailing data in the patch,
	 * we want it to match at the beginning/end of the file.
	 *
	 * But that would break if the patch is generated with
	 * --unified=0; sane people wouldn't do that to cause us
	 * trouble, but we try to please not so sane ones as well.
	 */
	if (unidiff_zero) {
		match_beginning = (!leading && !frag->oldpos);
		match_end = 0;
	}
	else {
		match_beginning = !leading && (frag->oldpos == 1);
		match_end = !trailing;
	}

	lines = 0;
	pos = frag->newpos;
	for (;;) {
		offset = find_offset(buf, desc->size,
				     oldlines, oldsize, pos, &lines);
		if (match_end && offset + oldsize != desc->size)
			offset = -1;
		if (match_beginning && offset)
			offset = -1;
		if (offset >= 0) {
			int diff = newsize - oldsize;
			unsigned long size = desc->size + diff;
			unsigned long alloc = desc->alloc;

			/* Warn if it was necessary to reduce the number
			 * of context lines.
			 */
			if ((leading != frag->leading) ||
			    (trailing != frag->trailing))
				fprintf(stderr, "Context reduced to (%ld/%ld)"
					" to apply fragment at %d\n",
					leading, trailing, pos + lines);

			if (size > alloc) {
				alloc = size + 8192;
				desc->alloc = alloc;
				buf = xrealloc(buf, alloc);
				desc->buffer = buf;
			}
			desc->size = size;
			memmove(buf + offset + newsize,
				buf + offset + oldsize,
				size - offset - newsize);
			memcpy(buf + offset, newlines, newsize);
			offset = 0;

			break;
		}

		/* Am I at my context limits? */
		if ((leading <= p_context) && (trailing <= p_context))
			break;
		if (match_beginning || match_end) {
			match_beginning = match_end = 0;
			continue;
		}
		/* Reduce the number of context lines
		 * Reduce both leading and trailing if they are equal
		 * otherwise just reduce the larger context.
		 */
		if (leading >= trailing) {
			remove_first_line(&oldlines, &oldsize);
			remove_first_line(&newlines, &newsize);
			pos--;
			leading--;
		}
		if (trailing > leading) {
			remove_last_line(&oldlines, &oldsize);
			remove_last_line(&newlines, &newsize);
			trailing--;
		}
	}

	if (offset && apply_verbosely)
		error("while searching for:\n%.*s", oldsize, oldlines);

	free(old);
	free(new);
	return offset;
}

static int apply_binary_fragment(struct buffer_desc *desc, struct patch *patch)
{
	unsigned long dst_size;
	struct fragment *fragment = patch->fragments;
	void *data;
	void *result;

	/* Binary patch is irreversible without the optional second hunk */
	if (apply_in_reverse) {
		if (!fragment->next)
			return error("cannot reverse-apply a binary patch "
				     "without the reverse hunk to '%s'",
				     patch->new_name
				     ? patch->new_name : patch->old_name);
		fragment = fragment->next;
	}
	data = (void*) fragment->patch;
	switch (fragment->binary_patch_method) {
	case BINARY_DELTA_DEFLATED:
		result = patch_delta(desc->buffer, desc->size,
				     data,
				     fragment->size,
				     &dst_size);
		free(desc->buffer);
		desc->buffer = result;
		break;
	case BINARY_LITERAL_DEFLATED:
		free(desc->buffer);
		desc->buffer = data;
		dst_size = fragment->size;
		break;
	}
	if (!desc->buffer)
		return -1;
	desc->size = desc->alloc = dst_size;
	return 0;
}

static int apply_binary(struct buffer_desc *desc, struct patch *patch)
{
	const char *name = patch->old_name ? patch->old_name : patch->new_name;
	unsigned char sha1[20];

	/* For safety, we require patch index line to contain
	 * full 40-byte textual SHA1 for old and new, at least for now.
	 */
	if (strlen(patch->old_sha1_prefix) != 40 ||
	    strlen(patch->new_sha1_prefix) != 40 ||
	    get_sha1_hex(patch->old_sha1_prefix, sha1) ||
	    get_sha1_hex(patch->new_sha1_prefix, sha1))
		return error("cannot apply binary patch to '%s' "
			     "without full index line", name);

	if (patch->old_name) {
		/* See if the old one matches what the patch
		 * applies to.
		 */
		hash_sha1_file(desc->buffer, desc->size, blob_type, sha1);
		if (strcmp(sha1_to_hex(sha1), patch->old_sha1_prefix))
			return error("the patch applies to '%s' (%s), "
				     "which does not match the "
				     "current contents.",
				     name, sha1_to_hex(sha1));
	}
	else {
		/* Otherwise, the old one must be empty. */
		if (desc->size)
			return error("the patch applies to an empty "
				     "'%s' but it is not empty", name);
	}

	get_sha1_hex(patch->new_sha1_prefix, sha1);
	if (is_null_sha1(sha1)) {
		free(desc->buffer);
		desc->alloc = desc->size = 0;
		desc->buffer = NULL;
		return 0; /* deletion patch */
	}

	if (has_sha1_file(sha1)) {
		/* We already have the postimage */
		enum object_type type;
		unsigned long size;

		free(desc->buffer);
		desc->buffer = read_sha1_file(sha1, &type, &size);
		if (!desc->buffer)
			return error("the necessary postimage %s for "
				     "'%s' cannot be read",
				     patch->new_sha1_prefix, name);
		desc->alloc = desc->size = size;
	}
	else {
		/* We have verified desc matches the preimage;
		 * apply the patch data to it, which is stored
		 * in the patch->fragments->{patch,size}.
		 */
		if (apply_binary_fragment(desc, patch))
			return error("binary patch does not apply to '%s'",
				     name);

		/* verify that the result matches */
		hash_sha1_file(desc->buffer, desc->size, blob_type, sha1);
		if (strcmp(sha1_to_hex(sha1), patch->new_sha1_prefix))
			return error("binary patch to '%s' creates incorrect result (expecting %s, got %s)", name, patch->new_sha1_prefix, sha1_to_hex(sha1));
	}

	return 0;
}

static int apply_fragments(struct buffer_desc *desc, struct patch *patch)
{
	struct fragment *frag = patch->fragments;
	const char *name = patch->old_name ? patch->old_name : patch->new_name;

	if (patch->is_binary)
		return apply_binary(desc, patch);

	while (frag) {
		if (apply_one_fragment(desc, frag, patch->inaccurate_eof)) {
			error("patch failed: %s:%ld", name, frag->oldpos);
			if (!apply_with_reject)
				return -1;
			frag->rejected = 1;
		}
		frag = frag->next;
	}
	return 0;
}

static int apply_data(struct patch *patch, struct stat *st, struct cache_entry *ce)
{
	char *buf;
	unsigned long size, alloc;
	struct buffer_desc desc;

	size = 0;
	alloc = 0;
	buf = NULL;
	if (cached) {
		if (ce) {
			enum object_type type;
			buf = read_sha1_file(ce->sha1, &type, &size);
			if (!buf)
				return error("read of %s failed",
					     patch->old_name);
			alloc = size;
		}
	}
	else if (patch->old_name) {
		size = xsize_t(st->st_size);
		alloc = size + 8192;
		buf = xmalloc(alloc);
		if (read_old_data(st, patch->old_name, &buf, &alloc, &size))
			return error("read of %s failed", patch->old_name);
	}

	desc.size = size;
	desc.alloc = alloc;
	desc.buffer = buf;

	if (apply_fragments(&desc, patch) < 0)
		return -1; /* note with --reject this succeeds. */

	/* NUL terminate the result */
	if (desc.alloc <= desc.size)
		desc.buffer = xrealloc(desc.buffer, desc.size + 1);
	desc.buffer[desc.size] = 0;

	patch->result = desc.buffer;
	patch->resultsize = desc.size;

	if (0 < patch->is_delete && patch->resultsize)
		return error("removal patch leaves file contents");

	return 0;
}

static int check_to_create_blob(const char *new_name, int ok_if_exists)
{
	struct stat nst;
	if (!lstat(new_name, &nst)) {
		if (S_ISDIR(nst.st_mode) || ok_if_exists)
			return 0;
		/*
		 * A leading component of new_name might be a symlink
		 * that is going to be removed with this patch, but
		 * still pointing at somewhere that has the path.
		 * In such a case, path "new_name" does not exist as
		 * far as git is concerned.
		 */
		if (has_symlink_leading_path(new_name, NULL))
			return 0;

		return error("%s: already exists in working directory", new_name);
	}
	else if ((errno != ENOENT) && (errno != ENOTDIR))
		return error("%s: %s", new_name, strerror(errno));
	return 0;
}

static int check_patch(struct patch *patch, struct patch *prev_patch)
{
	struct stat st;
	const char *old_name = patch->old_name;
	const char *new_name = patch->new_name;
	const char *name = old_name ? old_name : new_name;
	struct cache_entry *ce = NULL;
	int ok_if_exists;

	patch->rejected = 1; /* we will drop this after we succeed */
	if (old_name) {
		int changed = 0;
		int stat_ret = 0;
		unsigned st_mode = 0;

		if (!cached)
			stat_ret = lstat(old_name, &st);
		if (check_index) {
			int pos = cache_name_pos(old_name, strlen(old_name));
			if (pos < 0)
				return error("%s: does not exist in index",
					     old_name);
			ce = active_cache[pos];
			if (stat_ret < 0) {
				struct checkout costate;
				if (errno != ENOENT)
					return error("%s: %s", old_name,
						     strerror(errno));
				/* checkout */
				costate.base_dir = "";
				costate.base_dir_len = 0;
				costate.force = 0;
				costate.quiet = 0;
				costate.not_new = 0;
				costate.refresh_cache = 1;
				if (checkout_entry(ce,
						   &costate,
						   NULL) ||
				    lstat(old_name, &st))
					return -1;
			}
			if (!cached)
				changed = ce_match_stat(ce, &st, 1);
			if (changed)
				return error("%s: does not match index",
					     old_name);
			if (cached)
				st_mode = ntohl(ce->ce_mode);
		}
		else if (stat_ret < 0)
			return error("%s: %s", old_name, strerror(errno));

		if (!cached)
			st_mode = ntohl(ce_mode_from_stat(ce, st.st_mode));

		if (patch->is_new < 0)
			patch->is_new = 0;
		if (!patch->old_mode)
			patch->old_mode = st_mode;
		if ((st_mode ^ patch->old_mode) & S_IFMT)
			return error("%s: wrong type", old_name);
		if (st_mode != patch->old_mode)
			fprintf(stderr, "warning: %s has type %o, expected %o\n",
				old_name, st_mode, patch->old_mode);
	}

	if (new_name && prev_patch && 0 < prev_patch->is_delete &&
	    !strcmp(prev_patch->old_name, new_name))
		/* A type-change diff is always split into a patch to
		 * delete old, immediately followed by a patch to
		 * create new (see diff.c::run_diff()); in such a case
		 * it is Ok that the entry to be deleted by the
		 * previous patch is still in the working tree and in
		 * the index.
		 */
		ok_if_exists = 1;
	else
		ok_if_exists = 0;

	if (new_name &&
	    ((0 < patch->is_new) | (0 < patch->is_rename) | patch->is_copy)) {
		if (check_index &&
		    cache_name_pos(new_name, strlen(new_name)) >= 0 &&
		    !ok_if_exists)
			return error("%s: already exists in index", new_name);
		if (!cached) {
			int err = check_to_create_blob(new_name, ok_if_exists);
			if (err)
				return err;
		}
		if (!patch->new_mode) {
			if (0 < patch->is_new)
				patch->new_mode = S_IFREG | 0644;
			else
				patch->new_mode = patch->old_mode;
		}
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

	if (apply_data(patch, &st, ce) < 0)
		return error("%s: patch does not apply", name);
	patch->rejected = 0;
	return 0;
}

static int check_patch_list(struct patch *patch)
{
	struct patch *prev_patch = NULL;
	int err = 0;

	for (prev_patch = NULL; patch ; patch = patch->next) {
		if (apply_verbosely)
			say_patch_name(stderr,
				       "Checking patch ", patch, "...\n");
		err |= check_patch(patch, prev_patch);
		prev_patch = patch;
	}
	return err;
}

static void show_index_list(struct patch *list)
{
	struct patch *patch;

	/* Once we start supporting the reverse patch, it may be
	 * worth showing the new sha1 prefix, but until then...
	 */
	for (patch = list; patch; patch = patch->next) {
		const unsigned char *sha1_ptr;
		unsigned char sha1[20];
		const char *name;

		name = patch->old_name ? patch->old_name : patch->new_name;
		if (0 < patch->is_new)
			sha1_ptr = null_sha1;
		else if (get_sha1(patch->old_sha1_prefix, sha1))
			die("sha1 information is lacking or useless (%s).",
			    name);
		else
			sha1_ptr = sha1;

		printf("%06o %s	",patch->old_mode, sha1_to_hex(sha1_ptr));
		if (line_termination && quote_c_style(name, NULL, NULL, 0))
			quote_c_style(name, NULL, stdout, 0);
		else
			fputs(name, stdout);
		putchar(line_termination);
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

static void numstat_patch_list(struct patch *patch)
{
	for ( ; patch; patch = patch->next) {
		const char *name;
		name = patch->new_name ? patch->new_name : patch->old_name;
		if (patch->is_binary)
			printf("-\t-\t");
		else
			printf("%d\t%d\t",
			       patch->lines_added, patch->lines_deleted);
		if (line_termination && quote_c_style(name, NULL, NULL, 0))
			quote_c_style(name, NULL, stdout, 0);
		else
			fputs(name, stdout);
		putchar(line_termination);
	}
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
		int len = quote_c_style(patch->old_name, NULL, NULL, 0);
		if (!len)
			len = strlen(patch->old_name);
		if (len > max_len)
			max_len = len;
	}
	if (patch->new_name) {
		int len = quote_c_style(patch->new_name, NULL, NULL, 0);
		if (!len)
			len = strlen(patch->new_name);
		if (len > max_len)
			max_len = len;
	}
}

static void remove_file(struct patch *patch, int rmdir_empty)
{
	if (update_index) {
		if (remove_file_from_cache(patch->old_name) < 0)
			die("unable to remove %s from index", patch->old_name);
		cache_tree_invalidate_path(active_cache_tree, patch->old_name);
	}
	if (!cached) {
		if (!unlink(patch->old_name) && rmdir_empty) {
			char *name = xstrdup(patch->old_name);
			char *end = strrchr(name, '/');
			while (end) {
				*end = 0;
				if (rmdir(name))
					break;
				end = strrchr(name, '/');
			}
			free(name);
		}
	}
}

static void add_index_file(const char *path, unsigned mode, void *buf, unsigned long size)
{
	struct stat st;
	struct cache_entry *ce;
	int namelen = strlen(path);
	unsigned ce_size = cache_entry_size(namelen);

	if (!update_index)
		return;

	ce = xcalloc(1, ce_size);
	memcpy(ce->name, path, namelen);
	ce->ce_mode = create_ce_mode(mode);
	ce->ce_flags = htons(namelen);
	if (!cached) {
		if (lstat(path, &st) < 0)
			die("unable to stat newly created file %s", path);
		fill_stat_cache_info(ce, &st);
	}
	if (write_sha1_file(buf, size, blob_type, ce->sha1) < 0)
		die("unable to create backing store for newly created file %s", path);
	if (add_cache_entry(ce, ADD_CACHE_OK_TO_ADD) < 0)
		die("unable to add cache entry for %s", path);
}

static int try_create_file(const char *path, unsigned int mode, const char *buf, unsigned long size)
{
	int fd;
	char *nbuf;

	if (has_symlinks && S_ISLNK(mode))
		/* Although buf:size is counted string, it also is NUL
		 * terminated.
		 */
		return symlink(buf, path);

	fd = open(path, O_CREAT | O_EXCL | O_WRONLY, (mode & 0100) ? 0777 : 0666);
	if (fd < 0)
		return -1;

	nbuf = convert_to_working_tree(path, buf, &size);
	if (nbuf)
		buf = nbuf;

	while (size) {
		int written = xwrite(fd, buf, size);
		if (written < 0)
			die("writing file %s: %s", path, strerror(errno));
		if (!written)
			die("out of space writing file %s", path);
		buf += written;
		size -= written;
	}
	if (close(fd) < 0)
		die("closing file %s: %s", path, strerror(errno));
	if (nbuf)
		free(nbuf);
	return 0;
}

/*
 * We optimistically assume that the directories exist,
 * which is true 99% of the time anyway. If they don't,
 * we create them and try again.
 */
static void create_one_file(char *path, unsigned mode, const char *buf, unsigned long size)
{
	if (cached)
		return;
	if (!try_create_file(path, mode, buf, size))
		return;

	if (errno == ENOENT) {
		if (safe_create_leading_directories(path))
			return;
		if (!try_create_file(path, mode, buf, size))
			return;
	}

	if (errno == EEXIST || errno == EACCES) {
		/* We may be trying to create a file where a directory
		 * used to be.
		 */
		struct stat st;
		if (!lstat(path, &st) && (!S_ISDIR(st.st_mode) || !rmdir(path)))
			errno = EEXIST;
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
			++nr;
		}
	}
	die("unable to write file %s mode %o", path, mode);
}

static void create_file(struct patch *patch)
{
	char *path = patch->new_name;
	unsigned mode = patch->new_mode;
	unsigned long size = patch->resultsize;
	char *buf = patch->result;

	if (!mode)
		mode = S_IFREG | 0644;
	create_one_file(path, mode, buf, size);
	add_index_file(path, mode, buf, size);
	cache_tree_invalidate_path(active_cache_tree, path);
}

/* phase zero is to remove, phase one is to create */
static void write_out_one_result(struct patch *patch, int phase)
{
	if (patch->is_delete > 0) {
		if (phase == 0)
			remove_file(patch, 1);
		return;
	}
	if (patch->is_new > 0 || patch->is_copy) {
		if (phase == 1)
			create_file(patch);
		return;
	}
	/*
	 * Rename or modification boils down to the same
	 * thing: remove the old, write the new
	 */
	if (phase == 0)
		remove_file(patch, 0);
	if (phase == 1)
		create_file(patch);
}

static int write_out_one_reject(struct patch *patch)
{
	FILE *rej;
	char namebuf[PATH_MAX];
	struct fragment *frag;
	int cnt = 0;

	for (cnt = 0, frag = patch->fragments; frag; frag = frag->next) {
		if (!frag->rejected)
			continue;
		cnt++;
	}

	if (!cnt) {
		if (apply_verbosely)
			say_patch_name(stderr,
				       "Applied patch ", patch, " cleanly.\n");
		return 0;
	}

	/* This should not happen, because a removal patch that leaves
	 * contents are marked "rejected" at the patch level.
	 */
	if (!patch->new_name)
		die("internal error");

	/* Say this even without --verbose */
	say_patch_name(stderr, "Applying patch ", patch, " with");
	fprintf(stderr, " %d rejects...\n", cnt);

	cnt = strlen(patch->new_name);
	if (ARRAY_SIZE(namebuf) <= cnt + 5) {
		cnt = ARRAY_SIZE(namebuf) - 5;
		fprintf(stderr,
			"warning: truncating .rej filename to %.*s.rej",
			cnt - 1, patch->new_name);
	}
	memcpy(namebuf, patch->new_name, cnt);
	memcpy(namebuf + cnt, ".rej", 5);

	rej = fopen(namebuf, "w");
	if (!rej)
		return error("cannot open %s: %s", namebuf, strerror(errno));

	/* Normal git tools never deal with .rej, so do not pretend
	 * this is a git patch by saying --git nor give extended
	 * headers.  While at it, maybe please "kompare" that wants
	 * the trailing TAB and some garbage at the end of line ;-).
	 */
	fprintf(rej, "diff a/%s b/%s\t(rejected hunks)\n",
		patch->new_name, patch->new_name);
	for (cnt = 1, frag = patch->fragments;
	     frag;
	     cnt++, frag = frag->next) {
		if (!frag->rejected) {
			fprintf(stderr, "Hunk #%d applied cleanly.\n", cnt);
			continue;
		}
		fprintf(stderr, "Rejected hunk #%d.\n", cnt);
		fprintf(rej, "%.*s", frag->size, frag->patch);
		if (frag->patch[frag->size-1] != '\n')
			fputc('\n', rej);
	}
	fclose(rej);
	return -1;
}

static int write_out_results(struct patch *list, int skipped_patch)
{
	int phase;
	int errs = 0;
	struct patch *l;

	if (!list && !skipped_patch)
		return error("No changes");

	for (phase = 0; phase < 2; phase++) {
		l = list;
		while (l) {
			if (l->rejected)
				errs = 1;
			else {
				write_out_one_result(l, phase);
				if (phase == 1 && write_out_one_reject(l))
					errs = 1;
			}
			l = l->next;
		}
	}
	return errs;
}

static struct lock_file lock_file;

static struct excludes {
	struct excludes *next;
	const char *path;
} *excludes;

static int use_patch(struct patch *p)
{
	const char *pathname = p->new_name ? p->new_name : p->old_name;
	struct excludes *x = excludes;
	while (x) {
		if (fnmatch(x->path, pathname, 0) == 0)
			return 0;
		x = x->next;
	}
	if (0 < prefix_length) {
		int pathlen = strlen(pathname);
		if (pathlen <= prefix_length ||
		    memcmp(prefix, pathname, prefix_length))
			return 0;
	}
	return 1;
}

static void prefix_one(char **name)
{
	char *old_name = *name;
	if (!old_name)
		return;
	*name = xstrdup(prefix_filename(prefix, prefix_length, *name));
	free(old_name);
}

static void prefix_patches(struct patch *p)
{
	if (!prefix || p->is_toplevel_relative)
		return;
	for ( ; p; p = p->next) {
		if (p->new_name == p->old_name) {
			char *prefixed = p->new_name;
			prefix_one(&prefixed);
			p->new_name = p->old_name = prefixed;
		}
		else {
			prefix_one(&p->new_name);
			prefix_one(&p->old_name);
		}
	}
}

static int apply_patch(int fd, const char *filename, int inaccurate_eof)
{
	unsigned long offset, size;
	char *buffer = read_patch_file(fd, &size);
	struct patch *list = NULL, **listp = &list;
	int skipped_patch = 0;

	patch_input_file = filename;
	if (!buffer)
		return -1;
	offset = 0;
	while (size > 0) {
		struct patch *patch;
		int nr;

		patch = xcalloc(1, sizeof(*patch));
		patch->inaccurate_eof = inaccurate_eof;
		nr = parse_chunk(buffer + offset, size, patch);
		if (nr < 0)
			break;
		if (apply_in_reverse)
			reverse_patches(patch);
		if (prefix)
			prefix_patches(patch);
		if (use_patch(patch)) {
			patch_stats(patch);
			*listp = patch;
			listp = &patch->next;
		}
		else {
			/* perhaps free it a bit better? */
			free(patch);
			skipped_patch++;
		}
		offset += nr;
		size -= nr;
	}

	if (whitespace_error && (new_whitespace == error_on_whitespace))
		apply = 0;

	update_index = check_index && apply;
	if (update_index && newfd < 0)
		newfd = hold_locked_index(&lock_file, 1);

	if (check_index) {
		if (read_cache() < 0)
			die("unable to read index file");
	}

	if ((check || apply) &&
	    check_patch_list(list) < 0 &&
	    !apply_with_reject)
		exit(1);

	if (apply && write_out_results(list, skipped_patch))
		exit(1);

	if (show_index_info)
		show_index_list(list);

	if (diffstat)
		stat_patch_list(list);

	if (numstat)
		numstat_patch_list(list);

	if (summary)
		summary_patch_list(list);

	free(buffer);
	return 0;
}

static int git_apply_config(const char *var, const char *value)
{
	if (!strcmp(var, "apply.whitespace")) {
		apply_default_whitespace = xstrdup(value);
		return 0;
	}
	return git_default_config(var, value);
}


int cmd_apply(int argc, const char **argv, const char *unused_prefix)
{
	int i;
	int read_stdin = 1;
	int inaccurate_eof = 0;
	int errs = 0;
	int is_not_gitdir = 0;

	const char *whitespace_option = NULL;

	prefix = setup_git_directory_gently(&is_not_gitdir);
	prefix_length = prefix ? strlen(prefix) : 0;
	git_config(git_apply_config);
	if (apply_default_whitespace)
		parse_whitespace_option(apply_default_whitespace);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		char *end;
		int fd;

		if (!strcmp(arg, "-")) {
			errs |= apply_patch(0, "<stdin>", inaccurate_eof);
			read_stdin = 0;
			continue;
		}
		if (!prefixcmp(arg, "--exclude=")) {
			struct excludes *x = xmalloc(sizeof(*x));
			x->path = arg + 10;
			x->next = excludes;
			excludes = x;
			continue;
		}
		if (!prefixcmp(arg, "-p")) {
			p_value = atoi(arg + 2);
			p_value_known = 1;
			continue;
		}
		if (!strcmp(arg, "--no-add")) {
			no_add = 1;
			continue;
		}
		if (!strcmp(arg, "--stat")) {
			apply = 0;
			diffstat = 1;
			continue;
		}
		if (!strcmp(arg, "--allow-binary-replacement") ||
		    !strcmp(arg, "--binary")) {
			continue; /* now no-op */
		}
		if (!strcmp(arg, "--numstat")) {
			apply = 0;
			numstat = 1;
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
			if (is_not_gitdir)
				die("--index outside a repository");
			check_index = 1;
			continue;
		}
		if (!strcmp(arg, "--cached")) {
			if (is_not_gitdir)
				die("--cached outside a repository");
			check_index = 1;
			cached = 1;
			continue;
		}
		if (!strcmp(arg, "--apply")) {
			apply = 1;
			continue;
		}
		if (!strcmp(arg, "--index-info")) {
			apply = 0;
			show_index_info = 1;
			continue;
		}
		if (!strcmp(arg, "-z")) {
			line_termination = 0;
			continue;
		}
		if (!prefixcmp(arg, "-C")) {
			p_context = strtoul(arg + 2, &end, 0);
			if (*end != '\0')
				die("unrecognized context count '%s'", arg + 2);
			continue;
		}
		if (!prefixcmp(arg, "--whitespace=")) {
			whitespace_option = arg + 13;
			parse_whitespace_option(arg + 13);
			continue;
		}
		if (!strcmp(arg, "-R") || !strcmp(arg, "--reverse")) {
			apply_in_reverse = 1;
			continue;
		}
		if (!strcmp(arg, "--unidiff-zero")) {
			unidiff_zero = 1;
			continue;
		}
		if (!strcmp(arg, "--reject")) {
			apply = apply_with_reject = apply_verbosely = 1;
			continue;
		}
		if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose")) {
			apply_verbosely = 1;
			continue;
		}
		if (!strcmp(arg, "--inaccurate-eof")) {
			inaccurate_eof = 1;
			continue;
		}
		if (0 < prefix_length)
			arg = prefix_filename(prefix, prefix_length, arg);

		fd = open(arg, O_RDONLY);
		if (fd < 0)
			usage(apply_usage);
		read_stdin = 0;
		set_default_whitespace_mode(whitespace_option);
		errs |= apply_patch(fd, arg, inaccurate_eof);
		close(fd);
	}
	set_default_whitespace_mode(whitespace_option);
	if (read_stdin)
		errs |= apply_patch(0, "<stdin>", inaccurate_eof);
	if (whitespace_error) {
		if (squelch_whitespace_errors &&
		    squelch_whitespace_errors < whitespace_error) {
			int squelched =
				whitespace_error - squelch_whitespace_errors;
			fprintf(stderr, "warning: squelched %d "
				"whitespace error%s\n",
				squelched,
				squelched == 1 ? "" : "s");
		}
		if (new_whitespace == error_on_whitespace)
			die("%d line%s add%s trailing whitespaces.",
			    whitespace_error,
			    whitespace_error == 1 ? "" : "s",
			    whitespace_error == 1 ? "s" : "");
		if (applied_after_stripping)
			fprintf(stderr, "warning: %d line%s applied after"
				" stripping trailing whitespaces.\n",
				applied_after_stripping,
				applied_after_stripping == 1 ? "" : "s");
		else if (whitespace_error)
			fprintf(stderr, "warning: %d line%s add%s trailing"
				" whitespaces.\n",
				whitespace_error,
				whitespace_error == 1 ? "" : "s",
				whitespace_error == 1 ? "s" : "");
	}

	if (update_index) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    close(newfd) || commit_locked_index(&lock_file))
			die("Unable to write new index file");
	}

	return !!errs;
}
