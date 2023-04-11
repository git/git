/*
 * apply.c
 *
 * Copyright (C) Linus Torvalds, 2005
 *
 * This applies patches on top of some (arbitrary) version of the SCM.
 *
 */

#include "cache.h"
#include "abspath.h"
#include "alloc.h"
#include "config.h"
#include "object-store.h"
#include "blob.h"
#include "delta.h"
#include "diff.h"
#include "dir.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "xdiff-interface.h"
#include "ll-merge.h"
#include "lockfile.h"
#include "object-name.h"
#include "object-file.h"
#include "parse-options.h"
#include "quote.h"
#include "rerere.h"
#include "apply.h"
#include "entry.h"
#include "setup.h"
#include "wrapper.h"

struct gitdiff_data {
	struct strbuf *root;
	int linenr;
	int p_value;
};

static void git_apply_config(void)
{
	git_config_get_string("apply.whitespace", &apply_default_whitespace);
	git_config_get_string("apply.ignorewhitespace", &apply_default_ignorewhitespace);
	git_config(git_xmerge_config, NULL);
}

static int parse_whitespace_option(struct apply_state *state, const char *option)
{
	if (!option) {
		state->ws_error_action = warn_on_ws_error;
		return 0;
	}
	if (!strcmp(option, "warn")) {
		state->ws_error_action = warn_on_ws_error;
		return 0;
	}
	if (!strcmp(option, "nowarn")) {
		state->ws_error_action = nowarn_ws_error;
		return 0;
	}
	if (!strcmp(option, "error")) {
		state->ws_error_action = die_on_ws_error;
		return 0;
	}
	if (!strcmp(option, "error-all")) {
		state->ws_error_action = die_on_ws_error;
		state->squelch_whitespace_errors = 0;
		return 0;
	}
	if (!strcmp(option, "strip") || !strcmp(option, "fix")) {
		state->ws_error_action = correct_ws_error;
		return 0;
	}
	/*
	 * Please update $__git_whitespacelist in git-completion.bash
	 * when you add new options.
	 */
	return error(_("unrecognized whitespace option '%s'"), option);
}

static int parse_ignorewhitespace_option(struct apply_state *state,
						 const char *option)
{
	if (!option || !strcmp(option, "no") ||
	    !strcmp(option, "false") || !strcmp(option, "never") ||
	    !strcmp(option, "none")) {
		state->ws_ignore_action = ignore_ws_none;
		return 0;
	}
	if (!strcmp(option, "change")) {
		state->ws_ignore_action = ignore_ws_change;
		return 0;
	}
	return error(_("unrecognized whitespace ignore option '%s'"), option);
}

int init_apply_state(struct apply_state *state,
		     struct repository *repo,
		     const char *prefix)
{
	memset(state, 0, sizeof(*state));
	state->prefix = prefix;
	state->repo = repo;
	state->apply = 1;
	state->line_termination = '\n';
	state->p_value = 1;
	state->p_context = UINT_MAX;
	state->squelch_whitespace_errors = 5;
	state->ws_error_action = warn_on_ws_error;
	state->ws_ignore_action = ignore_ws_none;
	state->linenr = 1;
	string_list_init_nodup(&state->fn_table);
	string_list_init_nodup(&state->limit_by_name);
	strset_init(&state->removed_symlinks);
	strset_init(&state->kept_symlinks);
	strbuf_init(&state->root, 0);

	git_apply_config();
	if (apply_default_whitespace && parse_whitespace_option(state, apply_default_whitespace))
		return -1;
	if (apply_default_ignorewhitespace && parse_ignorewhitespace_option(state, apply_default_ignorewhitespace))
		return -1;
	return 0;
}

void clear_apply_state(struct apply_state *state)
{
	string_list_clear(&state->limit_by_name, 0);
	strset_clear(&state->removed_symlinks);
	strset_clear(&state->kept_symlinks);
	strbuf_release(&state->root);

	/* &state->fn_table is cleared at the end of apply_patch() */
}

static void mute_routine(const char *msg UNUSED, va_list params UNUSED)
{
	/* do nothing */
}

int check_apply_state(struct apply_state *state, int force_apply)
{
	int is_not_gitdir = !startup_info->have_repository;

	if (state->apply_with_reject && state->threeway)
		return error(_("options '%s' and '%s' cannot be used together"), "--reject", "--3way");
	if (state->threeway) {
		if (is_not_gitdir)
			return error(_("'%s' outside a repository"), "--3way");
		state->check_index = 1;
	}
	if (state->apply_with_reject) {
		state->apply = 1;
		if (state->apply_verbosity == verbosity_normal)
			state->apply_verbosity = verbosity_verbose;
	}
	if (!force_apply && (state->diffstat || state->numstat || state->summary || state->check || state->fake_ancestor))
		state->apply = 0;
	if (state->check_index && is_not_gitdir)
		return error(_("'%s' outside a repository"), "--index");
	if (state->cached) {
		if (is_not_gitdir)
			return error(_("'%s' outside a repository"), "--cached");
		state->check_index = 1;
	}
	if (state->ita_only && (state->check_index || is_not_gitdir))
		state->ita_only = 0;
	if (state->check_index)
		state->unsafe_paths = 0;

	if (state->apply_verbosity <= verbosity_silent) {
		state->saved_error_routine = get_error_routine();
		state->saved_warn_routine = get_warn_routine();
		set_error_routine(mute_routine);
		set_warn_routine(mute_routine);
	}

	return 0;
}

static void set_default_whitespace_mode(struct apply_state *state)
{
	if (!state->whitespace_option && !apply_default_whitespace)
		state->ws_error_action = (state->apply ? warn_on_ws_error : nowarn_ws_error);
}

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
	/*
	 * 'patch' is usually borrowed from buf in apply_patch(),
	 * but some codepaths store an allocated buffer.
	 */
	const char *patch;
	unsigned free_patch:1,
		rejected:1;
	int size;
	int linenr;
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

static void free_fragment_list(struct fragment *list)
{
	while (list) {
		struct fragment *next = list->next;
		if (list->free_patch)
			free((char *)list->patch);
		free(list);
		list = next;
	}
}

void release_patch(struct patch *patch)
{
	free_fragment_list(patch->fragments);
	free(patch->def_name);
	free(patch->old_name);
	free(patch->new_name);
	free(patch->result);
}

static void free_patch(struct patch *patch)
{
	release_patch(patch);
	free(patch);
}

static void free_patch_list(struct patch *list)
{
	while (list) {
		struct patch *next = list->next;
		free_patch(list);
		list = next;
	}
}

/*
 * A line in a file, len-bytes long (includes the terminating LF,
 * except for an incomplete line at the end if the file ends with
 * one), and its contents hashes to 'hash'.
 */
struct line {
	size_t len;
	unsigned hash : 24;
	unsigned flag : 8;
#define LINE_COMMON     1
#define LINE_PATCHED	2
};

/*
 * This represents a "file", which is an array of "lines".
 */
struct image {
	char *buf;
	size_t len;
	size_t nr;
	size_t alloc;
	struct line *line_allocated;
	struct line *line;
};

static uint32_t hash_line(const char *cp, size_t len)
{
	size_t i;
	uint32_t h;
	for (i = 0, h = 0; i < len; i++) {
		if (!isspace(cp[i])) {
			h = h * 3 + (cp[i] & 0xff);
		}
	}
	return h;
}

/*
 * Compare lines s1 of length n1 and s2 of length n2, ignoring
 * whitespace difference. Returns 1 if they match, 0 otherwise
 */
static int fuzzy_matchlines(const char *s1, size_t n1,
			    const char *s2, size_t n2)
{
	const char *end1 = s1 + n1;
	const char *end2 = s2 + n2;

	/* ignore line endings */
	while (s1 < end1 && (end1[-1] == '\r' || end1[-1] == '\n'))
		end1--;
	while (s2 < end2 && (end2[-1] == '\r' || end2[-1] == '\n'))
		end2--;

	while (s1 < end1 && s2 < end2) {
		if (isspace(*s1)) {
			/*
			 * Skip whitespace. We check on both buffers
			 * because we don't want "a b" to match "ab".
			 */
			if (!isspace(*s2))
				return 0;
			while (s1 < end1 && isspace(*s1))
				s1++;
			while (s2 < end2 && isspace(*s2))
				s2++;
		} else if (*s1++ != *s2++)
			return 0;
	}

	/* If we reached the end on one side only, lines don't match. */
	return s1 == end1 && s2 == end2;
}

static void add_line_info(struct image *img, const char *bol, size_t len, unsigned flag)
{
	ALLOC_GROW(img->line_allocated, img->nr + 1, img->alloc);
	img->line_allocated[img->nr].len = len;
	img->line_allocated[img->nr].hash = hash_line(bol, len);
	img->line_allocated[img->nr].flag = flag;
	img->nr++;
}

/*
 * "buf" has the file contents to be patched (read from various sources).
 * attach it to "image" and add line-based index to it.
 * "image" now owns the "buf".
 */
static void prepare_image(struct image *image, char *buf, size_t len,
			  int prepare_linetable)
{
	const char *cp, *ep;

	memset(image, 0, sizeof(*image));
	image->buf = buf;
	image->len = len;

	if (!prepare_linetable)
		return;

	ep = image->buf + image->len;
	cp = image->buf;
	while (cp < ep) {
		const char *next;
		for (next = cp; next < ep && *next != '\n'; next++)
			;
		if (next < ep)
			next++;
		add_line_info(image, cp, next - cp, 0);
		cp = next;
	}
	image->line = image->line_allocated;
}

static void clear_image(struct image *image)
{
	free(image->buf);
	free(image->line_allocated);
	memset(image, 0, sizeof(*image));
}

/* fmt must contain _one_ %s and no other substitution */
static void say_patch_name(FILE *output, const char *fmt, struct patch *patch)
{
	struct strbuf sb = STRBUF_INIT;

	if (patch->old_name && patch->new_name &&
	    strcmp(patch->old_name, patch->new_name)) {
		quote_c_style(patch->old_name, &sb, NULL, 0);
		strbuf_addstr(&sb, " => ");
		quote_c_style(patch->new_name, &sb, NULL, 0);
	} else {
		const char *n = patch->new_name;
		if (!n)
			n = patch->old_name;
		quote_c_style(n, &sb, NULL, 0);
	}
	fprintf(output, fmt, sb.buf);
	fputc('\n', output);
	strbuf_release(&sb);
}

#define SLOP (16)

/*
 * apply.c isn't equipped to handle arbitrarily large patches, because
 * it intermingles `unsigned long` with `int` for the type used to store
 * buffer lengths.
 *
 * Only process patches that are just shy of 1 GiB large in order to
 * avoid any truncation or overflow issues.
 */
#define MAX_APPLY_SIZE (1024UL * 1024 * 1023)

static int read_patch_file(struct strbuf *sb, int fd)
{
	if (strbuf_read(sb, fd, 0) < 0 || sb->len >= MAX_APPLY_SIZE)
		return error_errno("git apply: failed to read");

	/*
	 * Make sure that we have some slop in the buffer
	 * so that we can do speculative "memcmp" etc, and
	 * see to it that it is NUL-filled.
	 */
	strbuf_grow(sb, SLOP);
	memset(sb->buf + sb->len, 0, SLOP);
	return 0;
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
	return skip_prefix(str, "/dev/null", &str) && isspace(*str);
}

#define TERM_SPACE	1
#define TERM_TAB	2

static int name_terminate(int c, int terminate)
{
	if (c == ' ' && !(terminate & TERM_SPACE))
		return 0;
	if (c == '\t' && !(terminate & TERM_TAB))
		return 0;

	return 1;
}

/* remove double slashes to make --index work with such filenames */
static char *squash_slash(char *name)
{
	int i = 0, j = 0;

	if (!name)
		return NULL;

	while (name[i]) {
		if ((name[j++] = name[i++]) == '/')
			while (name[i] == '/')
				i++;
	}
	name[j] = '\0';
	return name;
}

static char *find_name_gnu(struct strbuf *root,
			   const char *line,
			   int p_value)
{
	struct strbuf name = STRBUF_INIT;
	char *cp;

	/*
	 * Proposed "new-style" GNU patch/diff format; see
	 * https://lore.kernel.org/git/7vll0wvb2a.fsf@assigned-by-dhcp.cox.net/
	 */
	if (unquote_c_style(&name, line, NULL)) {
		strbuf_release(&name);
		return NULL;
	}

	for (cp = name.buf; p_value; p_value--) {
		cp = strchr(cp, '/');
		if (!cp) {
			strbuf_release(&name);
			return NULL;
		}
		cp++;
	}

	strbuf_remove(&name, 0, cp - name.buf);
	if (root->len)
		strbuf_insert(&name, 0, root->buf, root->len);
	return squash_slash(strbuf_detach(&name, NULL));
}

static size_t sane_tz_len(const char *line, size_t len)
{
	const char *tz, *p;

	if (len < strlen(" +0500") || line[len-strlen(" +0500")] != ' ')
		return 0;
	tz = line + len - strlen(" +0500");

	if (tz[1] != '+' && tz[1] != '-')
		return 0;

	for (p = tz + 2; p != line + len; p++)
		if (!isdigit(*p))
			return 0;

	return line + len - tz;
}

static size_t tz_with_colon_len(const char *line, size_t len)
{
	const char *tz, *p;

	if (len < strlen(" +08:00") || line[len - strlen(":00")] != ':')
		return 0;
	tz = line + len - strlen(" +08:00");

	if (tz[0] != ' ' || (tz[1] != '+' && tz[1] != '-'))
		return 0;
	p = tz + 2;
	if (!isdigit(*p++) || !isdigit(*p++) || *p++ != ':' ||
	    !isdigit(*p++) || !isdigit(*p++))
		return 0;

	return line + len - tz;
}

static size_t date_len(const char *line, size_t len)
{
	const char *date, *p;

	if (len < strlen("72-02-05") || line[len-strlen("-05")] != '-')
		return 0;
	p = date = line + len - strlen("72-02-05");

	if (!isdigit(*p++) || !isdigit(*p++) || *p++ != '-' ||
	    !isdigit(*p++) || !isdigit(*p++) || *p++ != '-' ||
	    !isdigit(*p++) || !isdigit(*p++))	/* Not a date. */
		return 0;

	if (date - line >= strlen("19") &&
	    isdigit(date[-1]) && isdigit(date[-2]))	/* 4-digit year */
		date -= strlen("19");

	return line + len - date;
}

static size_t short_time_len(const char *line, size_t len)
{
	const char *time, *p;

	if (len < strlen(" 07:01:32") || line[len-strlen(":32")] != ':')
		return 0;
	p = time = line + len - strlen(" 07:01:32");

	/* Permit 1-digit hours? */
	if (*p++ != ' ' ||
	    !isdigit(*p++) || !isdigit(*p++) || *p++ != ':' ||
	    !isdigit(*p++) || !isdigit(*p++) || *p++ != ':' ||
	    !isdigit(*p++) || !isdigit(*p++))	/* Not a time. */
		return 0;

	return line + len - time;
}

static size_t fractional_time_len(const char *line, size_t len)
{
	const char *p;
	size_t n;

	/* Expected format: 19:41:17.620000023 */
	if (!len || !isdigit(line[len - 1]))
		return 0;
	p = line + len - 1;

	/* Fractional seconds. */
	while (p > line && isdigit(*p))
		p--;
	if (*p != '.')
		return 0;

	/* Hours, minutes, and whole seconds. */
	n = short_time_len(line, p - line);
	if (!n)
		return 0;

	return line + len - p + n;
}

static size_t trailing_spaces_len(const char *line, size_t len)
{
	const char *p;

	/* Expected format: ' ' x (1 or more)  */
	if (!len || line[len - 1] != ' ')
		return 0;

	p = line + len;
	while (p != line) {
		p--;
		if (*p != ' ')
			return line + len - (p + 1);
	}

	/* All spaces! */
	return len;
}

static size_t diff_timestamp_len(const char *line, size_t len)
{
	const char *end = line + len;
	size_t n;

	/*
	 * Posix: 2010-07-05 19:41:17
	 * GNU: 2010-07-05 19:41:17.620000023 -0500
	 */

	if (!isdigit(end[-1]))
		return 0;

	n = sane_tz_len(line, end - line);
	if (!n)
		n = tz_with_colon_len(line, end - line);
	end -= n;

	n = short_time_len(line, end - line);
	if (!n)
		n = fractional_time_len(line, end - line);
	end -= n;

	n = date_len(line, end - line);
	if (!n)	/* No date.  Too bad. */
		return 0;
	end -= n;

	if (end == line)	/* No space before date. */
		return 0;
	if (end[-1] == '\t') {	/* Success! */
		end--;
		return line + len - end;
	}
	if (end[-1] != ' ')	/* No space before date. */
		return 0;

	/* Whitespace damage. */
	end -= trailing_spaces_len(line, end - line);
	return line + len - end;
}

static char *find_name_common(struct strbuf *root,
			      const char *line,
			      const char *def,
			      int p_value,
			      const char *end,
			      int terminate)
{
	int len;
	const char *start = NULL;

	if (p_value == 0)
		start = line;
	while (line != end) {
		char c = *line;

		if (!end && isspace(c)) {
			if (c == '\n')
				break;
			if (name_terminate(c, terminate))
				break;
		}
		line++;
		if (c == '/' && !--p_value)
			start = line;
	}
	if (!start)
		return squash_slash(xstrdup_or_null(def));
	len = line - start;
	if (!len)
		return squash_slash(xstrdup_or_null(def));

	/*
	 * Generally we prefer the shorter name, especially
	 * if the other one is just a variation of that with
	 * something else tacked on to the end (ie "file.orig"
	 * or "file~").
	 */
	if (def) {
		int deflen = strlen(def);
		if (deflen < len && !strncmp(start, def, deflen))
			return squash_slash(xstrdup(def));
	}

	if (root->len) {
		char *ret = xstrfmt("%s%.*s", root->buf, len, start);
		return squash_slash(ret);
	}

	return squash_slash(xmemdupz(start, len));
}

static char *find_name(struct strbuf *root,
		       const char *line,
		       char *def,
		       int p_value,
		       int terminate)
{
	if (*line == '"') {
		char *name = find_name_gnu(root, line, p_value);
		if (name)
			return name;
	}

	return find_name_common(root, line, def, p_value, NULL, terminate);
}

static char *find_name_traditional(struct strbuf *root,
				   const char *line,
				   char *def,
				   int p_value)
{
	size_t len;
	size_t date_len;

	if (*line == '"') {
		char *name = find_name_gnu(root, line, p_value);
		if (name)
			return name;
	}

	len = strchrnul(line, '\n') - line;
	date_len = diff_timestamp_len(line, len);
	if (!date_len)
		return find_name_common(root, line, def, p_value, NULL, TERM_TAB);
	len -= date_len;

	return find_name_common(root, line, def, p_value, line + len, 0);
}

/*
 * Given the string after "--- " or "+++ ", guess the appropriate
 * p_value for the given patch.
 */
static int guess_p_value(struct apply_state *state, const char *nameline)
{
	char *name, *cp;
	int val = -1;

	if (is_dev_null(nameline))
		return -1;
	name = find_name_traditional(&state->root, nameline, NULL, 0);
	if (!name)
		return -1;
	cp = strchr(name, '/');
	if (!cp)
		val = 0;
	else if (state->prefix) {
		/*
		 * Does it begin with "a/$our-prefix" and such?  Then this is
		 * very likely to apply to our directory.
		 */
		if (starts_with(name, state->prefix))
			val = count_slashes(state->prefix);
		else {
			cp++;
			if (starts_with(cp, state->prefix))
				val = count_slashes(state->prefix) + 1;
		}
	}
	free(name);
	return val;
}

/*
 * Does the ---/+++ line have the POSIX timestamp after the last HT?
 * GNU diff puts epoch there to signal a creation/deletion event.  Is
 * this such a timestamp?
 */
static int has_epoch_timestamp(const char *nameline)
{
	/*
	 * We are only interested in epoch timestamp; any non-zero
	 * fraction cannot be one, hence "(\.0+)?" in the regexp below.
	 * For the same reason, the date must be either 1969-12-31 or
	 * 1970-01-01, and the seconds part must be "00".
	 */
	const char stamp_regexp[] =
		"^[0-2][0-9]:([0-5][0-9]):00(\\.0+)?"
		" "
		"([-+][0-2][0-9]:?[0-5][0-9])\n";
	const char *timestamp = NULL, *cp, *colon;
	static regex_t *stamp;
	regmatch_t m[10];
	int zoneoffset, epoch_hour, hour, minute;
	int status;

	for (cp = nameline; *cp != '\n'; cp++) {
		if (*cp == '\t')
			timestamp = cp + 1;
	}
	if (!timestamp)
		return 0;

	/*
	 * YYYY-MM-DD hh:mm:ss must be from either 1969-12-31
	 * (west of GMT) or 1970-01-01 (east of GMT)
	 */
	if (skip_prefix(timestamp, "1969-12-31 ", &timestamp))
		epoch_hour = 24;
	else if (skip_prefix(timestamp, "1970-01-01 ", &timestamp))
		epoch_hour = 0;
	else
		return 0;

	if (!stamp) {
		stamp = xmalloc(sizeof(*stamp));
		if (regcomp(stamp, stamp_regexp, REG_EXTENDED)) {
			warning(_("Cannot prepare timestamp regexp %s"),
				stamp_regexp);
			return 0;
		}
	}

	status = regexec(stamp, timestamp, ARRAY_SIZE(m), m, 0);
	if (status) {
		if (status != REG_NOMATCH)
			warning(_("regexec returned %d for input: %s"),
				status, timestamp);
		return 0;
	}

	hour = strtol(timestamp, NULL, 10);
	minute = strtol(timestamp + m[1].rm_so, NULL, 10);

	zoneoffset = strtol(timestamp + m[3].rm_so + 1, (char **) &colon, 10);
	if (*colon == ':')
		zoneoffset = zoneoffset * 60 + strtol(colon + 1, NULL, 10);
	else
		zoneoffset = (zoneoffset / 100) * 60 + (zoneoffset % 100);
	if (timestamp[m[3].rm_so] == '-')
		zoneoffset = -zoneoffset;

	return hour * 60 + minute - zoneoffset == epoch_hour * 60;
}

/*
 * Get the name etc info from the ---/+++ lines of a traditional patch header
 *
 * FIXME! The end-of-filename heuristics are kind of screwy. For existing
 * files, we can happily check the index for a match, but for creating a
 * new file we should try to match whatever "patch" does. I have no idea.
 */
static int parse_traditional_patch(struct apply_state *state,
				   const char *first,
				   const char *second,
				   struct patch *patch)
{
	char *name;

	first += 4;	/* skip "--- " */
	second += 4;	/* skip "+++ " */
	if (!state->p_value_known) {
		int p, q;
		p = guess_p_value(state, first);
		q = guess_p_value(state, second);
		if (p < 0) p = q;
		if (0 <= p && p == q) {
			state->p_value = p;
			state->p_value_known = 1;
		}
	}
	if (is_dev_null(first)) {
		patch->is_new = 1;
		patch->is_delete = 0;
		name = find_name_traditional(&state->root, second, NULL, state->p_value);
		patch->new_name = name;
	} else if (is_dev_null(second)) {
		patch->is_new = 0;
		patch->is_delete = 1;
		name = find_name_traditional(&state->root, first, NULL, state->p_value);
		patch->old_name = name;
	} else {
		char *first_name;
		first_name = find_name_traditional(&state->root, first, NULL, state->p_value);
		name = find_name_traditional(&state->root, second, first_name, state->p_value);
		free(first_name);
		if (has_epoch_timestamp(first)) {
			patch->is_new = 1;
			patch->is_delete = 0;
			patch->new_name = name;
		} else if (has_epoch_timestamp(second)) {
			patch->is_new = 0;
			patch->is_delete = 1;
			patch->old_name = name;
		} else {
			patch->old_name = name;
			patch->new_name = xstrdup_or_null(name);
		}
	}
	if (!name)
		return error(_("unable to find filename in patch at line %d"), state->linenr);

	return 0;
}

static int gitdiff_hdrend(struct gitdiff_data *state UNUSED,
			  const char *line UNUSED,
			  struct patch *patch UNUSED)
{
	return 1;
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
#define DIFF_OLD_NAME 0
#define DIFF_NEW_NAME 1

static int gitdiff_verify_name(struct gitdiff_data *state,
			       const char *line,
			       int isnull,
			       char **name,
			       int side)
{
	if (!*name && !isnull) {
		*name = find_name(state->root, line, NULL, state->p_value, TERM_TAB);
		return 0;
	}

	if (*name) {
		char *another;
		if (isnull)
			return error(_("git apply: bad git-diff - expected /dev/null, got %s on line %d"),
				     *name, state->linenr);
		another = find_name(state->root, line, NULL, state->p_value, TERM_TAB);
		if (!another || strcmp(another, *name)) {
			free(another);
			return error((side == DIFF_NEW_NAME) ?
			    _("git apply: bad git-diff - inconsistent new filename on line %d") :
			    _("git apply: bad git-diff - inconsistent old filename on line %d"), state->linenr);
		}
		free(another);
	} else {
		if (!is_dev_null(line))
			return error(_("git apply: bad git-diff - expected /dev/null on line %d"), state->linenr);
	}

	return 0;
}

static int gitdiff_oldname(struct gitdiff_data *state,
			   const char *line,
			   struct patch *patch)
{
	return gitdiff_verify_name(state, line,
				   patch->is_new, &patch->old_name,
				   DIFF_OLD_NAME);
}

static int gitdiff_newname(struct gitdiff_data *state,
			   const char *line,
			   struct patch *patch)
{
	return gitdiff_verify_name(state, line,
				   patch->is_delete, &patch->new_name,
				   DIFF_NEW_NAME);
}

static int parse_mode_line(const char *line, int linenr, unsigned int *mode)
{
	char *end;
	*mode = strtoul(line, &end, 8);
	if (end == line || !isspace(*end))
		return error(_("invalid mode on line %d: %s"), linenr, line);
	return 0;
}

static int gitdiff_oldmode(struct gitdiff_data *state,
			   const char *line,
			   struct patch *patch)
{
	return parse_mode_line(line, state->linenr, &patch->old_mode);
}

static int gitdiff_newmode(struct gitdiff_data *state,
			   const char *line,
			   struct patch *patch)
{
	return parse_mode_line(line, state->linenr, &patch->new_mode);
}

static int gitdiff_delete(struct gitdiff_data *state,
			  const char *line,
			  struct patch *patch)
{
	patch->is_delete = 1;
	free(patch->old_name);
	patch->old_name = xstrdup_or_null(patch->def_name);
	return gitdiff_oldmode(state, line, patch);
}

static int gitdiff_newfile(struct gitdiff_data *state,
			   const char *line,
			   struct patch *patch)
{
	patch->is_new = 1;
	free(patch->new_name);
	patch->new_name = xstrdup_or_null(patch->def_name);
	return gitdiff_newmode(state, line, patch);
}

static int gitdiff_copysrc(struct gitdiff_data *state,
			   const char *line,
			   struct patch *patch)
{
	patch->is_copy = 1;
	free(patch->old_name);
	patch->old_name = find_name(state->root, line, NULL, state->p_value ? state->p_value - 1 : 0, 0);
	return 0;
}

static int gitdiff_copydst(struct gitdiff_data *state,
			   const char *line,
			   struct patch *patch)
{
	patch->is_copy = 1;
	free(patch->new_name);
	patch->new_name = find_name(state->root, line, NULL, state->p_value ? state->p_value - 1 : 0, 0);
	return 0;
}

static int gitdiff_renamesrc(struct gitdiff_data *state,
			     const char *line,
			     struct patch *patch)
{
	patch->is_rename = 1;
	free(patch->old_name);
	patch->old_name = find_name(state->root, line, NULL, state->p_value ? state->p_value - 1 : 0, 0);
	return 0;
}

static int gitdiff_renamedst(struct gitdiff_data *state,
			     const char *line,
			     struct patch *patch)
{
	patch->is_rename = 1;
	free(patch->new_name);
	patch->new_name = find_name(state->root, line, NULL, state->p_value ? state->p_value - 1 : 0, 0);
	return 0;
}

static int gitdiff_similarity(struct gitdiff_data *state UNUSED,
			      const char *line,
			      struct patch *patch)
{
	unsigned long val = strtoul(line, NULL, 10);
	if (val <= 100)
		patch->score = val;
	return 0;
}

static int gitdiff_dissimilarity(struct gitdiff_data *state UNUSED,
				 const char *line,
				 struct patch *patch)
{
	unsigned long val = strtoul(line, NULL, 10);
	if (val <= 100)
		patch->score = val;
	return 0;
}

static int gitdiff_index(struct gitdiff_data *state,
			 const char *line,
			 struct patch *patch)
{
	/*
	 * index line is N hexadecimal, "..", N hexadecimal,
	 * and optional space with octal mode.
	 */
	const char *ptr, *eol;
	int len;
	const unsigned hexsz = the_hash_algo->hexsz;

	ptr = strchr(line, '.');
	if (!ptr || ptr[1] != '.' || hexsz < ptr - line)
		return 0;
	len = ptr - line;
	memcpy(patch->old_oid_prefix, line, len);
	patch->old_oid_prefix[len] = 0;

	line = ptr + 2;
	ptr = strchr(line, ' ');
	eol = strchrnul(line, '\n');

	if (!ptr || eol < ptr)
		ptr = eol;
	len = ptr - line;

	if (hexsz < len)
		return 0;
	memcpy(patch->new_oid_prefix, line, len);
	patch->new_oid_prefix[len] = 0;
	if (*ptr == ' ')
		return gitdiff_oldmode(state, ptr + 1, patch);
	return 0;
}

/*
 * This is normal for a diff that doesn't change anything: we'll fall through
 * into the next diff. Tell the parser to break out.
 */
static int gitdiff_unrecognized(struct gitdiff_data *state UNUSED,
				const char *line UNUSED,
				struct patch *patch UNUSED)
{
	return 1;
}

/*
 * Skip p_value leading components from "line"; as we do not accept
 * absolute paths, return NULL in that case.
 */
static const char *skip_tree_prefix(int p_value,
				    const char *line,
				    int llen)
{
	int nslash;
	int i;

	if (!p_value)
		return (llen && line[0] == '/') ? NULL : line;

	nslash = p_value;
	for (i = 0; i < llen; i++) {
		int ch = line[i];
		if (ch == '/' && --nslash <= 0)
			return (i == 0) ? NULL : &line[i + 1];
	}
	return NULL;
}

/*
 * This is to extract the same name that appears on "diff --git"
 * line.  We do not find and return anything if it is a rename
 * patch, and it is OK because we will find the name elsewhere.
 * We need to reliably find name only when it is mode-change only,
 * creation or deletion of an empty file.  In any of these cases,
 * both sides are the same name under a/ and b/ respectively.
 */
static char *git_header_name(int p_value,
			     const char *line,
			     int llen)
{
	const char *name;
	const char *second = NULL;
	size_t len, line_len;

	line += strlen("diff --git ");
	llen -= strlen("diff --git ");

	if (*line == '"') {
		const char *cp;
		struct strbuf first = STRBUF_INIT;
		struct strbuf sp = STRBUF_INIT;

		if (unquote_c_style(&first, line, &second))
			goto free_and_fail1;

		/* strip the a/b prefix including trailing slash */
		cp = skip_tree_prefix(p_value, first.buf, first.len);
		if (!cp)
			goto free_and_fail1;
		strbuf_remove(&first, 0, cp - first.buf);

		/*
		 * second points at one past closing dq of name.
		 * find the second name.
		 */
		while ((second < line + llen) && isspace(*second))
			second++;

		if (line + llen <= second)
			goto free_and_fail1;
		if (*second == '"') {
			if (unquote_c_style(&sp, second, NULL))
				goto free_and_fail1;
			cp = skip_tree_prefix(p_value, sp.buf, sp.len);
			if (!cp)
				goto free_and_fail1;
			/* They must match, otherwise ignore */
			if (strcmp(cp, first.buf))
				goto free_and_fail1;
			strbuf_release(&sp);
			return strbuf_detach(&first, NULL);
		}

		/* unquoted second */
		cp = skip_tree_prefix(p_value, second, line + llen - second);
		if (!cp)
			goto free_and_fail1;
		if (line + llen - cp != first.len ||
		    memcmp(first.buf, cp, first.len))
			goto free_and_fail1;
		return strbuf_detach(&first, NULL);

	free_and_fail1:
		strbuf_release(&first);
		strbuf_release(&sp);
		return NULL;
	}

	/* unquoted first name */
	name = skip_tree_prefix(p_value, line, llen);
	if (!name)
		return NULL;

	/*
	 * since the first name is unquoted, a dq if exists must be
	 * the beginning of the second name.
	 */
	for (second = name; second < line + llen; second++) {
		if (*second == '"') {
			struct strbuf sp = STRBUF_INIT;
			const char *np;

			if (unquote_c_style(&sp, second, NULL))
				goto free_and_fail2;

			np = skip_tree_prefix(p_value, sp.buf, sp.len);
			if (!np)
				goto free_and_fail2;

			len = sp.buf + sp.len - np;
			if (len < second - name &&
			    !strncmp(np, name, len) &&
			    isspace(name[len])) {
				/* Good */
				strbuf_remove(&sp, 0, np - sp.buf);
				return strbuf_detach(&sp, NULL);
			}

		free_and_fail2:
			strbuf_release(&sp);
			return NULL;
		}
	}

	/*
	 * Accept a name only if it shows up twice, exactly the same
	 * form.
	 */
	second = strchr(name, '\n');
	if (!second)
		return NULL;
	line_len = second - name;
	for (len = 0 ; ; len++) {
		switch (name[len]) {
		default:
			continue;
		case '\n':
			return NULL;
		case '\t': case ' ':
			/*
			 * Is this the separator between the preimage
			 * and the postimage pathname?  Again, we are
			 * only interested in the case where there is
			 * no rename, as this is only to set def_name
			 * and a rename patch has the names elsewhere
			 * in an unambiguous form.
			 */
			if (!name[len + 1])
				return NULL; /* no postimage name */
			second = skip_tree_prefix(p_value, name + len + 1,
						  line_len - (len + 1));
			if (!second)
				return NULL;
			/*
			 * Does len bytes starting at "name" and "second"
			 * (that are separated by one HT or SP we just
			 * found) exactly match?
			 */
			if (second[len] == '\n' && !strncmp(name, second, len))
				return xmemdupz(name, len);
		}
	}
}

static int check_header_line(int linenr, struct patch *patch)
{
	int extensions = (patch->is_delete == 1) + (patch->is_new == 1) +
			 (patch->is_rename == 1) + (patch->is_copy == 1);
	if (extensions > 1)
		return error(_("inconsistent header lines %d and %d"),
			     patch->extension_linenr, linenr);
	if (extensions && !patch->extension_linenr)
		patch->extension_linenr = linenr;
	return 0;
}

int parse_git_diff_header(struct strbuf *root,
			  int *linenr,
			  int p_value,
			  const char *line,
			  int len,
			  unsigned int size,
			  struct patch *patch)
{
	unsigned long offset;
	struct gitdiff_data parse_hdr_state;

	/* A git diff has explicit new/delete information, so we don't guess */
	patch->is_new = 0;
	patch->is_delete = 0;

	/*
	 * Some things may not have the old name in the
	 * rest of the headers anywhere (pure mode changes,
	 * or removing or adding empty files), so we get
	 * the default name from the header.
	 */
	patch->def_name = git_header_name(p_value, line, len);
	if (patch->def_name && root->len) {
		char *s = xstrfmt("%s%s", root->buf, patch->def_name);
		free(patch->def_name);
		patch->def_name = s;
	}

	line += len;
	size -= len;
	(*linenr)++;
	parse_hdr_state.root = root;
	parse_hdr_state.linenr = *linenr;
	parse_hdr_state.p_value = p_value;

	for (offset = len ; size > 0 ; offset += len, size -= len, line += len, (*linenr)++) {
		static const struct opentry {
			const char *str;
			int (*fn)(struct gitdiff_data *, const char *, struct patch *);
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
			int res;
			if (len < oplen || memcmp(p->str, line, oplen))
				continue;
			res = p->fn(&parse_hdr_state, line + oplen, patch);
			if (res < 0)
				return -1;
			if (check_header_line(*linenr, patch))
				return -1;
			if (res > 0)
				goto done;
			break;
		}
	}

done:
	if (!patch->old_name && !patch->new_name) {
		if (!patch->def_name) {
			error(Q_("git diff header lacks filename information when removing "
				 "%d leading pathname component (line %d)",
				 "git diff header lacks filename information when removing "
				 "%d leading pathname components (line %d)",
				 parse_hdr_state.p_value),
			      parse_hdr_state.p_value, *linenr);
			return -128;
		}
		patch->old_name = xstrdup(patch->def_name);
		patch->new_name = xstrdup(patch->def_name);
	}
	if ((!patch->new_name && !patch->is_delete) ||
	    (!patch->old_name && !patch->is_new)) {
		error(_("git diff header lacks filename information "
			"(line %d)"), *linenr);
		return -128;
	}
	patch->is_toplevel_relative = 1;
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

static void recount_diff(const char *line, int size, struct fragment *fragment)
{
	int oldlines = 0, newlines = 0, ret = 0;

	if (size < 1) {
		warning("recount: ignore empty hunk");
		return;
	}

	for (;;) {
		int len = linelen(line, size);
		size -= len;
		line += len;

		if (size < 1)
			break;

		switch (*line) {
		case ' ': case '\n':
			newlines++;
			/* fall through */
		case '-':
			oldlines++;
			continue;
		case '+':
			newlines++;
			continue;
		case '\\':
			continue;
		case '@':
			ret = size < 3 || !starts_with(line, "@@ ");
			break;
		case 'd':
			ret = size < 5 || !starts_with(line, "diff ");
			break;
		default:
			ret = -1;
			break;
		}
		if (ret) {
			warning(_("recount: unexpected line: %.*s"),
				(int)linelen(line, size), line);
			return;
		}
		break;
	}
	fragment->oldlines = oldlines;
	fragment->newlines = newlines;
}

/*
 * Parse a unified diff fragment header of the
 * form "@@ -a,b +c,d @@"
 */
static int parse_fragment_header(const char *line, int len, struct fragment *fragment)
{
	int offset;

	if (!len || line[len-1] != '\n')
		return -1;

	/* Figure out the number of lines in a fragment */
	offset = parse_range(line, len, 4, " +", &fragment->oldpos, &fragment->oldlines);
	offset = parse_range(line, len, offset, " @@", &fragment->newpos, &fragment->newlines);

	return offset;
}

/*
 * Find file diff header
 *
 * Returns:
 *  -1 if no header was found
 *  -128 in case of error
 *   the size of the header in bytes (called "offset") otherwise
 */
static int find_header(struct apply_state *state,
		       const char *line,
		       unsigned long size,
		       int *hdrsize,
		       struct patch *patch)
{
	unsigned long offset, len;

	patch->is_toplevel_relative = 0;
	patch->is_rename = patch->is_copy = 0;
	patch->is_new = patch->is_delete = -1;
	patch->old_mode = patch->new_mode = 0;
	patch->old_name = patch->new_name = NULL;
	for (offset = 0; size > 0; offset += len, size -= len, line += len, state->linenr++) {
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
			error(_("patch fragment without header at line %d: %.*s"),
				     state->linenr, (int)len-1, line);
			return -128;
		}

		if (size < len + 6)
			break;

		/*
		 * Git patch? It might not have a real patch, just a rename
		 * or mode change, so we handle that specially
		 */
		if (!memcmp("diff --git ", line, 11)) {
			int git_hdr_len = parse_git_diff_header(&state->root, &state->linenr,
								state->p_value, line, len,
								size, patch);
			if (git_hdr_len < 0)
				return -128;
			if (git_hdr_len <= len)
				continue;
			*hdrsize = git_hdr_len;
			return offset;
		}

		/* --- followed by +++ ? */
		if (memcmp("--- ", line,  4) || memcmp("+++ ", line + len, 4))
			continue;

		/*
		 * We only accept unified patches, so we want it to
		 * at least have "@@ -a,b +c,d @@\n", which is 14 chars
		 * minimum ("@@ -0,0 +1 @@\n" is the shortest).
		 */
		nextlen = linelen(line + len, size - len);
		if (size < nextlen + 14 || memcmp("@@ -", line + len + nextlen, 4))
			continue;

		/* Ok, we'll consider it a patch */
		if (parse_traditional_patch(state, line, line+len, patch))
			return -128;
		*hdrsize = len + nextlen;
		state->linenr += 2;
		return offset;
	}
	return -1;
}

static void record_ws_error(struct apply_state *state,
			    unsigned result,
			    const char *line,
			    int len,
			    int linenr)
{
	char *err;

	if (!result)
		return;

	state->whitespace_error++;
	if (state->squelch_whitespace_errors &&
	    state->squelch_whitespace_errors < state->whitespace_error)
		return;

	err = whitespace_error_string(result);
	if (state->apply_verbosity > verbosity_silent)
		fprintf(stderr, "%s:%d: %s.\n%.*s\n",
			state->patch_input_file, linenr, err, len, line);
	free(err);
}

static void check_whitespace(struct apply_state *state,
			     const char *line,
			     int len,
			     unsigned ws_rule)
{
	unsigned result = ws_check(line + 1, len - 1, ws_rule);

	record_ws_error(state, result, line + 1, len - 2, state->linenr);
}

/*
 * Check if the patch has context lines with CRLF or
 * the patch wants to remove lines with CRLF.
 */
static void check_old_for_crlf(struct patch *patch, const char *line, int len)
{
	if (len >= 2 && line[len-1] == '\n' && line[len-2] == '\r') {
		patch->ws_rule |= WS_CR_AT_EOL;
		patch->crlf_in_old = 1;
	}
}


/*
 * Parse a unified diff. Note that this really needs to parse each
 * fragment separately, since the only way to know the difference
 * between a "---" that is part of a patch, and a "---" that starts
 * the next patch is to look at the line counts..
 */
static int parse_fragment(struct apply_state *state,
			  const char *line,
			  unsigned long size,
			  struct patch *patch,
			  struct fragment *fragment)
{
	int added, deleted;
	int len = linelen(line, size), offset;
	unsigned long oldlines, newlines;
	unsigned long leading, trailing;

	offset = parse_fragment_header(line, len, fragment);
	if (offset < 0)
		return -1;
	if (offset > 0 && patch->recount)
		recount_diff(line + offset, size - offset, fragment);
	oldlines = fragment->oldlines;
	newlines = fragment->newlines;
	leading = 0;
	trailing = 0;

	/* Parse the thing.. */
	line += len;
	size -= len;
	state->linenr++;
	added = deleted = 0;
	for (offset = len;
	     0 < size;
	     offset += len, size -= len, line += len, state->linenr++) {
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
			check_old_for_crlf(patch, line, len);
			if (!state->apply_in_reverse &&
			    state->ws_error_action == correct_ws_error)
				check_whitespace(state, line, len, patch->ws_rule);
			break;
		case '-':
			if (!state->apply_in_reverse)
				check_old_for_crlf(patch, line, len);
			if (state->apply_in_reverse &&
			    state->ws_error_action != nowarn_ws_error)
				check_whitespace(state, line, len, patch->ws_rule);
			deleted++;
			oldlines--;
			trailing = 0;
			break;
		case '+':
			if (state->apply_in_reverse)
				check_old_for_crlf(patch, line, len);
			if (!state->apply_in_reverse &&
			    state->ws_error_action != nowarn_ws_error)
				check_whitespace(state, line, len, patch->ws_rule);
			added++;
			newlines--;
			trailing = 0;
			break;

		/*
		 * We allow "\ No newline at end of file". Depending
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
	if (!patch->recount && !deleted && !added)
		return -1;

	fragment->leading = leading;
	fragment->trailing = trailing;

	/*
	 * If a fragment ends with an incomplete line, we failed to include
	 * it in the above loop because we hit oldlines == newlines == 0
	 * before seeing it.
	 */
	if (12 < size && !memcmp(line, "\\ ", 2))
		offset += linelen(line, size);

	patch->lines_added += added;
	patch->lines_deleted += deleted;

	if (0 < patch->is_new && oldlines)
		return error(_("new file depends on old contents"));
	if (0 < patch->is_delete && newlines)
		return error(_("deleted file still has contents"));
	return offset;
}

/*
 * We have seen "diff --git a/... b/..." header (or a traditional patch
 * header).  Read hunks that belong to this patch into fragments and hang
 * them to the given patch structure.
 *
 * The (fragment->patch, fragment->size) pair points into the memory given
 * by the caller, not a copy, when we return.
 *
 * Returns:
 *   -1 in case of error,
 *   the number of bytes in the patch otherwise.
 */
static int parse_single_patch(struct apply_state *state,
			      const char *line,
			      unsigned long size,
			      struct patch *patch)
{
	unsigned long offset = 0;
	unsigned long oldlines = 0, newlines = 0, context = 0;
	struct fragment **fragp = &patch->fragments;

	while (size > 4 && !memcmp(line, "@@ -", 4)) {
		struct fragment *fragment;
		int len;

		CALLOC_ARRAY(fragment, 1);
		fragment->linenr = state->linenr;
		len = parse_fragment(state, line, size, patch, fragment);
		if (len <= 0) {
			free(fragment);
			return error(_("corrupt patch at line %d"), state->linenr);
		}
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

	if (0 < patch->is_new && oldlines)
		return error(_("new file %s depends on old contents"), patch->new_name);
	if (0 < patch->is_delete && newlines)
		return error(_("deleted file %s still has contents"), patch->old_name);
	if (!patch->is_delete && !newlines && context && state->apply_verbosity > verbosity_silent)
		fprintf_ln(stderr,
			   _("** warning: "
			     "file %s becomes empty but is not deleted"),
			   patch->new_name);

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
	git_zstream stream;
	void *out;
	int st;

	memset(&stream, 0, sizeof(stream));

	stream.next_in = (unsigned char *)data;
	stream.avail_in = size;
	stream.next_out = out = xmalloc(inflated_size);
	stream.avail_out = inflated_size;
	git_inflate_init(&stream);
	st = git_inflate(&stream, Z_FINISH);
	git_inflate_end(&stream);
	if ((st != Z_STREAM_END) || stream.total_out != inflated_size) {
		free(out);
		return NULL;
	}
	return out;
}

/*
 * Read a binary hunk and return a new fragment; fragment->patch
 * points at an allocated memory that the caller must free, so
 * it is marked as "->free_patch = 1".
 */
static struct fragment *parse_binary_hunk(struct apply_state *state,
					  char **buf_p,
					  unsigned long *sz_p,
					  int *status_p,
					  int *used_p)
{
	/*
	 * Expect a line that begins with binary patch method ("literal"
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

	if (starts_with(buffer, "delta ")) {
		patch_method = BINARY_DELTA_DEFLATED;
		origlen = strtoul(buffer + 6, NULL, 10);
	}
	else if (starts_with(buffer, "literal ")) {
		patch_method = BINARY_LITERAL_DEFLATED;
		origlen = strtoul(buffer + 8, NULL, 10);
	}
	else
		return NULL;

	state->linenr++;
	buffer += llen;
	size -= llen;
	while (1) {
		int byte_length, max_byte_length, newsize;
		llen = linelen(buffer, size);
		used += llen;
		state->linenr++;
		if (llen == 1) {
			/* consume the blank line */
			buffer++;
			size--;
			break;
		}
		/*
		 * Minimum line is "A00000\n" which is 7-byte long,
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

	CALLOC_ARRAY(frag, 1);
	frag->patch = inflate_it(data, hunk_size, origlen);
	frag->free_patch = 1;
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
	error(_("corrupt binary patch at line %d: %.*s"),
	      state->linenr-1, llen-1, buffer);
	return NULL;
}

/*
 * Returns:
 *   -1 in case of error,
 *   the length of the parsed binary patch otherwise
 */
static int parse_binary(struct apply_state *state,
			char *buffer,
			unsigned long size,
			struct patch *patch)
{
	/*
	 * We have read "GIT binary patch\n"; what follows is a line
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

	forward = parse_binary_hunk(state, &buffer, &size, &status, &used);
	if (!forward && !status)
		/* there has to be one hunk (forward hunk) */
		return error(_("unrecognized binary patch at line %d"), state->linenr-1);
	if (status)
		/* otherwise we already gave an error message */
		return status;

	reverse = parse_binary_hunk(state, &buffer, &size, &status, &used_1);
	if (reverse)
		used += used_1;
	else if (status) {
		/*
		 * Not having reverse hunk is not an error, but having
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

static void prefix_one(struct apply_state *state, char **name)
{
	char *old_name = *name;
	if (!old_name)
		return;
	*name = prefix_filename(state->prefix, *name);
	free(old_name);
}

static void prefix_patch(struct apply_state *state, struct patch *p)
{
	if (!state->prefix || p->is_toplevel_relative)
		return;
	prefix_one(state, &p->new_name);
	prefix_one(state, &p->old_name);
}

/*
 * include/exclude
 */

static void add_name_limit(struct apply_state *state,
			   const char *name,
			   int exclude)
{
	struct string_list_item *it;

	it = string_list_append(&state->limit_by_name, name);
	it->util = exclude ? NULL : (void *) 1;
}

static int use_patch(struct apply_state *state, struct patch *p)
{
	const char *pathname = p->new_name ? p->new_name : p->old_name;
	int i;

	/* Paths outside are not touched regardless of "--include" */
	if (state->prefix && *state->prefix) {
		const char *rest;
		if (!skip_prefix(pathname, state->prefix, &rest) || !*rest)
			return 0;
	}

	/* See if it matches any of exclude/include rule */
	for (i = 0; i < state->limit_by_name.nr; i++) {
		struct string_list_item *it = &state->limit_by_name.items[i];
		if (!wildmatch(it->string, pathname, 0))
			return (it->util != NULL);
	}

	/*
	 * If we had any include, a path that does not match any rule is
	 * not used.  Otherwise, we saw bunch of exclude rules (or none)
	 * and such a path is used.
	 */
	return !state->has_include;
}

/*
 * Read the patch text in "buffer" that extends for "size" bytes; stop
 * reading after seeing a single patch (i.e. changes to a single file).
 * Create fragments (i.e. patch hunks) and hang them to the given patch.
 *
 * Returns:
 *   -1 if no header was found or parse_binary() failed,
 *   -128 on another error,
 *   the number of bytes consumed otherwise,
 *     so that the caller can call us again for the next patch.
 */
static int parse_chunk(struct apply_state *state, char *buffer, unsigned long size, struct patch *patch)
{
	int hdrsize, patchsize;
	int offset = find_header(state, buffer, size, &hdrsize, patch);

	if (offset < 0)
		return offset;

	prefix_patch(state, patch);

	if (!use_patch(state, patch))
		patch->ws_rule = 0;
	else if (patch->new_name)
		patch->ws_rule = whitespace_rule(state->repo->index,
						 patch->new_name);
	else
		patch->ws_rule = whitespace_rule(state->repo->index,
						 patch->old_name);

	patchsize = parse_single_patch(state,
				       buffer + offset + hdrsize,
				       size - offset - hdrsize,
				       patch);

	if (patchsize < 0)
		return -128;

	if (!patchsize) {
		static const char git_binary[] = "GIT binary patch\n";
		int hd = hdrsize + offset;
		unsigned long llen = linelen(buffer + hd, size - hd);

		if (llen == sizeof(git_binary) - 1 &&
		    !memcmp(git_binary, buffer + hd, llen)) {
			int used;
			state->linenr++;
			used = parse_binary(state, buffer + hd + llen,
					    size - hd - llen, patch);
			if (used < 0)
				return -1;
			if (used)
				patchsize = used + llen;
			else
				patchsize = 0;
		}
		else if (!memcmp(" differ\n", buffer + hd + llen - 8, 8)) {
			static const char *binhdr[] = {
				"Binary files ",
				"Files ",
				NULL,
			};
			int i;
			for (i = 0; binhdr[i]; i++) {
				int len = strlen(binhdr[i]);
				if (len < size - hd &&
				    !memcmp(binhdr[i], buffer + hd, len)) {
					state->linenr++;
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
		if ((state->apply || state->check) &&
		    (!patch->is_binary && !metadata_changes(patch))) {
			error(_("patch with only garbage at line %d"), state->linenr);
			return -128;
		}
	}

	return offset + hdrsize + patchsize;
}

static void reverse_patches(struct patch *p)
{
	for (; p; p = p->next) {
		struct fragment *frag = p->fragments;

		SWAP(p->new_name, p->old_name);
		SWAP(p->new_mode, p->old_mode);
		SWAP(p->is_new, p->is_delete);
		SWAP(p->lines_added, p->lines_deleted);
		SWAP(p->old_oid_prefix, p->new_oid_prefix);

		for (; frag; frag = frag->next) {
			SWAP(frag->newpos, frag->oldpos);
			SWAP(frag->newlines, frag->oldlines);
		}
	}
}

static const char pluses[] =
"++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++";
static const char minuses[]=
"----------------------------------------------------------------------";

static void show_stats(struct apply_state *state, struct patch *patch)
{
	struct strbuf qname = STRBUF_INIT;
	char *cp = patch->new_name ? patch->new_name : patch->old_name;
	int max, add, del;

	quote_c_style(cp, &qname, NULL, 0);

	/*
	 * "scale" the filename
	 */
	max = state->max_len;
	if (max > 50)
		max = 50;

	if (qname.len > max) {
		cp = strchr(qname.buf + qname.len + 3 - max, '/');
		if (!cp)
			cp = qname.buf + qname.len + 3 - max;
		strbuf_splice(&qname, 0, cp - qname.buf, "...", 3);
	}

	if (patch->is_binary) {
		printf(" %-*s |  Bin\n", max, qname.buf);
		strbuf_release(&qname);
		return;
	}

	printf(" %-*s |", max, qname.buf);
	strbuf_release(&qname);

	/*
	 * scale the add/delete
	 */
	max = max + state->max_change > 70 ? 70 - max : state->max_change;
	add = patch->lines_added;
	del = patch->lines_deleted;

	if (state->max_change > 0) {
		int total = ((add + del) * max + state->max_change / 2) / state->max_change;
		add = (add * max + state->max_change / 2) / state->max_change;
		del = total - add;
	}
	printf("%5d %.*s%.*s\n", patch->lines_added + patch->lines_deleted,
		add, pluses, del, minuses);
}

static int read_old_data(struct stat *st, struct patch *patch,
			 const char *path, struct strbuf *buf)
{
	int conv_flags = patch->crlf_in_old ?
		CONV_EOL_KEEP_CRLF : CONV_EOL_RENORMALIZE;
	switch (st->st_mode & S_IFMT) {
	case S_IFLNK:
		if (strbuf_readlink(buf, path, st->st_size) < 0)
			return error(_("unable to read symlink %s"), path);
		return 0;
	case S_IFREG:
		if (strbuf_read_file(buf, path, st->st_size) != st->st_size)
			return error(_("unable to open or read %s"), path);
		/*
		 * "git apply" without "--index/--cached" should never look
		 * at the index; the target file may not have been added to
		 * the index yet, and we may not even be in any Git repository.
		 * Pass NULL to convert_to_git() to stress this; the function
		 * should never look at the index when explicit crlf option
		 * is given.
		 */
		convert_to_git(NULL, path, buf->buf, buf->len, buf, conv_flags);
		return 0;
	default:
		return -1;
	}
}

/*
 * Update the preimage, and the common lines in postimage,
 * from buffer buf of length len. If postlen is 0 the postimage
 * is updated in place, otherwise it's updated on a new buffer
 * of length postlen
 */

static void update_pre_post_images(struct image *preimage,
				   struct image *postimage,
				   char *buf,
				   size_t len, size_t postlen)
{
	int i, ctx, reduced;
	char *new_buf, *old_buf, *fixed;
	struct image fixed_preimage;

	/*
	 * Update the preimage with whitespace fixes.  Note that we
	 * are not losing preimage->buf -- apply_one_fragment() will
	 * free "oldlines".
	 */
	prepare_image(&fixed_preimage, buf, len, 1);
	assert(postlen
	       ? fixed_preimage.nr == preimage->nr
	       : fixed_preimage.nr <= preimage->nr);
	for (i = 0; i < fixed_preimage.nr; i++)
		fixed_preimage.line[i].flag = preimage->line[i].flag;
	free(preimage->line_allocated);
	*preimage = fixed_preimage;

	/*
	 * Adjust the common context lines in postimage. This can be
	 * done in-place when we are shrinking it with whitespace
	 * fixing, but needs a new buffer when ignoring whitespace or
	 * expanding leading tabs to spaces.
	 *
	 * We trust the caller to tell us if the update can be done
	 * in place (postlen==0) or not.
	 */
	old_buf = postimage->buf;
	if (postlen)
		new_buf = postimage->buf = xmalloc(postlen);
	else
		new_buf = old_buf;
	fixed = preimage->buf;

	for (i = reduced = ctx = 0; i < postimage->nr; i++) {
		size_t l_len = postimage->line[i].len;
		if (!(postimage->line[i].flag & LINE_COMMON)) {
			/* an added line -- no counterparts in preimage */
			memmove(new_buf, old_buf, l_len);
			old_buf += l_len;
			new_buf += l_len;
			continue;
		}

		/* a common context -- skip it in the original postimage */
		old_buf += l_len;

		/* and find the corresponding one in the fixed preimage */
		while (ctx < preimage->nr &&
		       !(preimage->line[ctx].flag & LINE_COMMON)) {
			fixed += preimage->line[ctx].len;
			ctx++;
		}

		/*
		 * preimage is expected to run out, if the caller
		 * fixed addition of trailing blank lines.
		 */
		if (preimage->nr <= ctx) {
			reduced++;
			continue;
		}

		/* and copy it in, while fixing the line length */
		l_len = preimage->line[ctx].len;
		memcpy(new_buf, fixed, l_len);
		new_buf += l_len;
		fixed += l_len;
		postimage->line[i].len = l_len;
		ctx++;
	}

	if (postlen
	    ? postlen < new_buf - postimage->buf
	    : postimage->len < new_buf - postimage->buf)
		BUG("caller miscounted postlen: asked %d, orig = %d, used = %d",
		    (int)postlen, (int) postimage->len, (int)(new_buf - postimage->buf));

	/* Fix the length of the whole thing */
	postimage->len = new_buf - postimage->buf;
	postimage->nr -= reduced;
}

static int line_by_line_fuzzy_match(struct image *img,
				    struct image *preimage,
				    struct image *postimage,
				    unsigned long current,
				    int current_lno,
				    int preimage_limit)
{
	int i;
	size_t imgoff = 0;
	size_t preoff = 0;
	size_t postlen = postimage->len;
	size_t extra_chars;
	char *buf;
	char *preimage_eof;
	char *preimage_end;
	struct strbuf fixed;
	char *fixed_buf;
	size_t fixed_len;

	for (i = 0; i < preimage_limit; i++) {
		size_t prelen = preimage->line[i].len;
		size_t imglen = img->line[current_lno+i].len;

		if (!fuzzy_matchlines(img->buf + current + imgoff, imglen,
				      preimage->buf + preoff, prelen))
			return 0;
		if (preimage->line[i].flag & LINE_COMMON)
			postlen += imglen - prelen;
		imgoff += imglen;
		preoff += prelen;
	}

	/*
	 * Ok, the preimage matches with whitespace fuzz.
	 *
	 * imgoff now holds the true length of the target that
	 * matches the preimage before the end of the file.
	 *
	 * Count the number of characters in the preimage that fall
	 * beyond the end of the file and make sure that all of them
	 * are whitespace characters. (This can only happen if
	 * we are removing blank lines at the end of the file.)
	 */
	buf = preimage_eof = preimage->buf + preoff;
	for ( ; i < preimage->nr; i++)
		preoff += preimage->line[i].len;
	preimage_end = preimage->buf + preoff;
	for ( ; buf < preimage_end; buf++)
		if (!isspace(*buf))
			return 0;

	/*
	 * Update the preimage and the common postimage context
	 * lines to use the same whitespace as the target.
	 * If whitespace is missing in the target (i.e.
	 * if the preimage extends beyond the end of the file),
	 * use the whitespace from the preimage.
	 */
	extra_chars = preimage_end - preimage_eof;
	strbuf_init(&fixed, imgoff + extra_chars);
	strbuf_add(&fixed, img->buf + current, imgoff);
	strbuf_add(&fixed, preimage_eof, extra_chars);
	fixed_buf = strbuf_detach(&fixed, &fixed_len);
	update_pre_post_images(preimage, postimage,
			       fixed_buf, fixed_len, postlen);
	return 1;
}

static int match_fragment(struct apply_state *state,
			  struct image *img,
			  struct image *preimage,
			  struct image *postimage,
			  unsigned long current,
			  int current_lno,
			  unsigned ws_rule,
			  int match_beginning, int match_end)
{
	int i;
	char *fixed_buf, *buf, *orig, *target;
	struct strbuf fixed;
	size_t fixed_len, postlen;
	int preimage_limit;

	if (preimage->nr + current_lno <= img->nr) {
		/*
		 * The hunk falls within the boundaries of img.
		 */
		preimage_limit = preimage->nr;
		if (match_end && (preimage->nr + current_lno != img->nr))
			return 0;
	} else if (state->ws_error_action == correct_ws_error &&
		   (ws_rule & WS_BLANK_AT_EOF)) {
		/*
		 * This hunk extends beyond the end of img, and we are
		 * removing blank lines at the end of the file.  This
		 * many lines from the beginning of the preimage must
		 * match with img, and the remainder of the preimage
		 * must be blank.
		 */
		preimage_limit = img->nr - current_lno;
	} else {
		/*
		 * The hunk extends beyond the end of the img and
		 * we are not removing blanks at the end, so we
		 * should reject the hunk at this position.
		 */
		return 0;
	}

	if (match_beginning && current_lno)
		return 0;

	/* Quick hash check */
	for (i = 0; i < preimage_limit; i++)
		if ((img->line[current_lno + i].flag & LINE_PATCHED) ||
		    (preimage->line[i].hash != img->line[current_lno + i].hash))
			return 0;

	if (preimage_limit == preimage->nr) {
		/*
		 * Do we have an exact match?  If we were told to match
		 * at the end, size must be exactly at current+fragsize,
		 * otherwise current+fragsize must be still within the preimage,
		 * and either case, the old piece should match the preimage
		 * exactly.
		 */
		if ((match_end
		     ? (current + preimage->len == img->len)
		     : (current + preimage->len <= img->len)) &&
		    !memcmp(img->buf + current, preimage->buf, preimage->len))
			return 1;
	} else {
		/*
		 * The preimage extends beyond the end of img, so
		 * there cannot be an exact match.
		 *
		 * There must be one non-blank context line that match
		 * a line before the end of img.
		 */
		char *buf_end;

		buf = preimage->buf;
		buf_end = buf;
		for (i = 0; i < preimage_limit; i++)
			buf_end += preimage->line[i].len;

		for ( ; buf < buf_end; buf++)
			if (!isspace(*buf))
				break;
		if (buf == buf_end)
			return 0;
	}

	/*
	 * No exact match. If we are ignoring whitespace, run a line-by-line
	 * fuzzy matching. We collect all the line length information because
	 * we need it to adjust whitespace if we match.
	 */
	if (state->ws_ignore_action == ignore_ws_change)
		return line_by_line_fuzzy_match(img, preimage, postimage,
						current, current_lno, preimage_limit);

	if (state->ws_error_action != correct_ws_error)
		return 0;

	/*
	 * The hunk does not apply byte-by-byte, but the hash says
	 * it might with whitespace fuzz. We weren't asked to
	 * ignore whitespace, we were asked to correct whitespace
	 * errors, so let's try matching after whitespace correction.
	 *
	 * While checking the preimage against the target, whitespace
	 * errors in both fixed, we count how large the corresponding
	 * postimage needs to be.  The postimage prepared by
	 * apply_one_fragment() has whitespace errors fixed on added
	 * lines already, but the common lines were propagated as-is,
	 * which may become longer when their whitespace errors are
	 * fixed.
	 */

	/* First count added lines in postimage */
	postlen = 0;
	for (i = 0; i < postimage->nr; i++) {
		if (!(postimage->line[i].flag & LINE_COMMON))
			postlen += postimage->line[i].len;
	}

	/*
	 * The preimage may extend beyond the end of the file,
	 * but in this loop we will only handle the part of the
	 * preimage that falls within the file.
	 */
	strbuf_init(&fixed, preimage->len + 1);
	orig = preimage->buf;
	target = img->buf + current;
	for (i = 0; i < preimage_limit; i++) {
		size_t oldlen = preimage->line[i].len;
		size_t tgtlen = img->line[current_lno + i].len;
		size_t fixstart = fixed.len;
		struct strbuf tgtfix;
		int match;

		/* Try fixing the line in the preimage */
		ws_fix_copy(&fixed, orig, oldlen, ws_rule, NULL);

		/* Try fixing the line in the target */
		strbuf_init(&tgtfix, tgtlen);
		ws_fix_copy(&tgtfix, target, tgtlen, ws_rule, NULL);

		/*
		 * If they match, either the preimage was based on
		 * a version before our tree fixed whitespace breakage,
		 * or we are lacking a whitespace-fix patch the tree
		 * the preimage was based on already had (i.e. target
		 * has whitespace breakage, the preimage doesn't).
		 * In either case, we are fixing the whitespace breakages
		 * so we might as well take the fix together with their
		 * real change.
		 */
		match = (tgtfix.len == fixed.len - fixstart &&
			 !memcmp(tgtfix.buf, fixed.buf + fixstart,
					     fixed.len - fixstart));

		/* Add the length if this is common with the postimage */
		if (preimage->line[i].flag & LINE_COMMON)
			postlen += tgtfix.len;

		strbuf_release(&tgtfix);
		if (!match)
			goto unmatch_exit;

		orig += oldlen;
		target += tgtlen;
	}


	/*
	 * Now handle the lines in the preimage that falls beyond the
	 * end of the file (if any). They will only match if they are
	 * empty or only contain whitespace (if WS_BLANK_AT_EOL is
	 * false).
	 */
	for ( ; i < preimage->nr; i++) {
		size_t fixstart = fixed.len; /* start of the fixed preimage */
		size_t oldlen = preimage->line[i].len;
		int j;

		/* Try fixing the line in the preimage */
		ws_fix_copy(&fixed, orig, oldlen, ws_rule, NULL);

		for (j = fixstart; j < fixed.len; j++)
			if (!isspace(fixed.buf[j]))
				goto unmatch_exit;

		orig += oldlen;
	}

	/*
	 * Yes, the preimage is based on an older version that still
	 * has whitespace breakages unfixed, and fixing them makes the
	 * hunk match.  Update the context lines in the postimage.
	 */
	fixed_buf = strbuf_detach(&fixed, &fixed_len);
	if (postlen < postimage->len)
		postlen = 0;
	update_pre_post_images(preimage, postimage,
			       fixed_buf, fixed_len, postlen);
	return 1;

 unmatch_exit:
	strbuf_release(&fixed);
	return 0;
}

static int find_pos(struct apply_state *state,
		    struct image *img,
		    struct image *preimage,
		    struct image *postimage,
		    int line,
		    unsigned ws_rule,
		    int match_beginning, int match_end)
{
	int i;
	unsigned long backwards, forwards, current;
	int backwards_lno, forwards_lno, current_lno;

	/*
	 * When running with --allow-overlap, it is possible that a hunk is
	 * seen that pretends to start at the beginning (but no longer does),
	 * and that *still* needs to match the end. So trust `match_end` more
	 * than `match_beginning`.
	 */
	if (state->allow_overlap && match_beginning && match_end &&
	    img->nr - preimage->nr != 0)
		match_beginning = 0;

	/*
	 * If match_beginning or match_end is specified, there is no
	 * point starting from a wrong line that will never match and
	 * wander around and wait for a match at the specified end.
	 */
	if (match_beginning)
		line = 0;
	else if (match_end)
		line = img->nr - preimage->nr;

	/*
	 * Because the comparison is unsigned, the following test
	 * will also take care of a negative line number that can
	 * result when match_end and preimage is larger than the target.
	 */
	if ((size_t) line > img->nr)
		line = img->nr;

	current = 0;
	for (i = 0; i < line; i++)
		current += img->line[i].len;

	/*
	 * There's probably some smart way to do this, but I'll leave
	 * that to the smart and beautiful people. I'm simple and stupid.
	 */
	backwards = current;
	backwards_lno = line;
	forwards = current;
	forwards_lno = line;
	current_lno = line;

	for (i = 0; ; i++) {
		if (match_fragment(state, img, preimage, postimage,
				   current, current_lno, ws_rule,
				   match_beginning, match_end))
			return current_lno;

	again:
		if (backwards_lno == 0 && forwards_lno == img->nr)
			break;

		if (i & 1) {
			if (backwards_lno == 0) {
				i++;
				goto again;
			}
			backwards_lno--;
			backwards -= img->line[backwards_lno].len;
			current = backwards;
			current_lno = backwards_lno;
		} else {
			if (forwards_lno == img->nr) {
				i++;
				goto again;
			}
			forwards += img->line[forwards_lno].len;
			forwards_lno++;
			current = forwards;
			current_lno = forwards_lno;
		}

	}
	return -1;
}

static void remove_first_line(struct image *img)
{
	img->buf += img->line[0].len;
	img->len -= img->line[0].len;
	img->line++;
	img->nr--;
}

static void remove_last_line(struct image *img)
{
	img->len -= img->line[--img->nr].len;
}

/*
 * The change from "preimage" and "postimage" has been found to
 * apply at applied_pos (counts in line numbers) in "img".
 * Update "img" to remove "preimage" and replace it with "postimage".
 */
static void update_image(struct apply_state *state,
			 struct image *img,
			 int applied_pos,
			 struct image *preimage,
			 struct image *postimage)
{
	/*
	 * remove the copy of preimage at offset in img
	 * and replace it with postimage
	 */
	int i, nr;
	size_t remove_count, insert_count, applied_at = 0;
	char *result;
	int preimage_limit;

	/*
	 * If we are removing blank lines at the end of img,
	 * the preimage may extend beyond the end.
	 * If that is the case, we must be careful only to
	 * remove the part of the preimage that falls within
	 * the boundaries of img. Initialize preimage_limit
	 * to the number of lines in the preimage that falls
	 * within the boundaries.
	 */
	preimage_limit = preimage->nr;
	if (preimage_limit > img->nr - applied_pos)
		preimage_limit = img->nr - applied_pos;

	for (i = 0; i < applied_pos; i++)
		applied_at += img->line[i].len;

	remove_count = 0;
	for (i = 0; i < preimage_limit; i++)
		remove_count += img->line[applied_pos + i].len;
	insert_count = postimage->len;

	/* Adjust the contents */
	result = xmalloc(st_add3(st_sub(img->len, remove_count), insert_count, 1));
	memcpy(result, img->buf, applied_at);
	memcpy(result + applied_at, postimage->buf, postimage->len);
	memcpy(result + applied_at + postimage->len,
	       img->buf + (applied_at + remove_count),
	       img->len - (applied_at + remove_count));
	free(img->buf);
	img->buf = result;
	img->len += insert_count - remove_count;
	result[img->len] = '\0';

	/* Adjust the line table */
	nr = img->nr + postimage->nr - preimage_limit;
	if (preimage_limit < postimage->nr) {
		/*
		 * NOTE: this knows that we never call remove_first_line()
		 * on anything other than pre/post image.
		 */
		REALLOC_ARRAY(img->line, nr);
		img->line_allocated = img->line;
	}
	if (preimage_limit != postimage->nr)
		MOVE_ARRAY(img->line + applied_pos + postimage->nr,
			   img->line + applied_pos + preimage_limit,
			   img->nr - (applied_pos + preimage_limit));
	COPY_ARRAY(img->line + applied_pos, postimage->line, postimage->nr);
	if (!state->allow_overlap)
		for (i = 0; i < postimage->nr; i++)
			img->line[applied_pos + i].flag |= LINE_PATCHED;
	img->nr = nr;
}

/*
 * Use the patch-hunk text in "frag" to prepare two images (preimage and
 * postimage) for the hunk.  Find lines that match "preimage" in "img" and
 * replace the part of "img" with "postimage" text.
 */
static int apply_one_fragment(struct apply_state *state,
			      struct image *img, struct fragment *frag,
			      int inaccurate_eof, unsigned ws_rule,
			      int nth_fragment)
{
	int match_beginning, match_end;
	const char *patch = frag->patch;
	int size = frag->size;
	char *old, *oldlines;
	struct strbuf newlines;
	int new_blank_lines_at_end = 0;
	int found_new_blank_lines_at_end = 0;
	int hunk_linenr = frag->linenr;
	unsigned long leading, trailing;
	int pos, applied_pos;
	struct image preimage;
	struct image postimage;

	memset(&preimage, 0, sizeof(preimage));
	memset(&postimage, 0, sizeof(postimage));
	oldlines = xmalloc(size);
	strbuf_init(&newlines, size);

	old = oldlines;
	while (size > 0) {
		char first;
		int len = linelen(patch, size);
		int plen;
		int added_blank_line = 0;
		int is_blank_context = 0;
		size_t start;

		if (!len)
			break;

		/*
		 * "plen" is how much of the line we should use for
		 * the actual patch data. Normally we just remove the
		 * first character on the line, but if the line is
		 * followed by "\ No newline", then we also remove the
		 * last one (which is the newline, of course).
		 */
		plen = len - 1;
		if (len < size && patch[len] == '\\')
			plen--;
		first = *patch;
		if (state->apply_in_reverse) {
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
			*old++ = '\n';
			strbuf_addch(&newlines, '\n');
			add_line_info(&preimage, "\n", 1, LINE_COMMON);
			add_line_info(&postimage, "\n", 1, LINE_COMMON);
			is_blank_context = 1;
			break;
		case ' ':
			if (plen && (ws_rule & WS_BLANK_AT_EOF) &&
			    ws_blank_line(patch + 1, plen))
				is_blank_context = 1;
			/* fallthrough */
		case '-':
			memcpy(old, patch + 1, plen);
			add_line_info(&preimage, old, plen,
				      (first == ' ' ? LINE_COMMON : 0));
			old += plen;
			if (first == '-')
				break;
			/* fallthrough */
		case '+':
			/* --no-add does not add new lines */
			if (first == '+' && state->no_add)
				break;

			start = newlines.len;
			if (first != '+' ||
			    !state->whitespace_error ||
			    state->ws_error_action != correct_ws_error) {
				strbuf_add(&newlines, patch + 1, plen);
			}
			else {
				ws_fix_copy(&newlines, patch + 1, plen, ws_rule, &state->applied_after_fixing_ws);
			}
			add_line_info(&postimage, newlines.buf + start, newlines.len - start,
				      (first == '+' ? 0 : LINE_COMMON));
			if (first == '+' &&
			    (ws_rule & WS_BLANK_AT_EOF) &&
			    ws_blank_line(patch + 1, plen))
				added_blank_line = 1;
			break;
		case '@': case '\\':
			/* Ignore it, we already handled it */
			break;
		default:
			if (state->apply_verbosity > verbosity_normal)
				error(_("invalid start of line: '%c'"), first);
			applied_pos = -1;
			goto out;
		}
		if (added_blank_line) {
			if (!new_blank_lines_at_end)
				found_new_blank_lines_at_end = hunk_linenr;
			new_blank_lines_at_end++;
		}
		else if (is_blank_context)
			;
		else
			new_blank_lines_at_end = 0;
		patch += len;
		size -= len;
		hunk_linenr++;
	}
	if (inaccurate_eof &&
	    old > oldlines && old[-1] == '\n' &&
	    newlines.len > 0 && newlines.buf[newlines.len - 1] == '\n') {
		old--;
		strbuf_setlen(&newlines, newlines.len - 1);
		preimage.line_allocated[preimage.nr - 1].len--;
		postimage.line_allocated[postimage.nr - 1].len--;
	}

	leading = frag->leading;
	trailing = frag->trailing;

	/*
	 * A hunk to change lines at the beginning would begin with
	 * @@ -1,L +N,M @@
	 * but we need to be careful.  -U0 that inserts before the second
	 * line also has this pattern.
	 *
	 * And a hunk to add to an empty file would begin with
	 * @@ -0,0 +N,M @@
	 *
	 * In other words, a hunk that is (frag->oldpos <= 1) with or
	 * without leading context must match at the beginning.
	 */
	match_beginning = (!frag->oldpos ||
			   (frag->oldpos == 1 && !state->unidiff_zero));

	/*
	 * A hunk without trailing lines must match at the end.
	 * However, we simply cannot tell if a hunk must match end
	 * from the lack of trailing lines if the patch was generated
	 * with unidiff without any context.
	 */
	match_end = !state->unidiff_zero && !trailing;

	pos = frag->newpos ? (frag->newpos - 1) : 0;
	preimage.buf = oldlines;
	preimage.len = old - oldlines;
	postimage.buf = newlines.buf;
	postimage.len = newlines.len;
	preimage.line = preimage.line_allocated;
	postimage.line = postimage.line_allocated;

	for (;;) {

		applied_pos = find_pos(state, img, &preimage, &postimage, pos,
				       ws_rule, match_beginning, match_end);

		if (applied_pos >= 0)
			break;

		/* Am I at my context limits? */
		if ((leading <= state->p_context) && (trailing <= state->p_context))
			break;
		if (match_beginning || match_end) {
			match_beginning = match_end = 0;
			continue;
		}

		/*
		 * Reduce the number of context lines; reduce both
		 * leading and trailing if they are equal otherwise
		 * just reduce the larger context.
		 */
		if (leading >= trailing) {
			remove_first_line(&preimage);
			remove_first_line(&postimage);
			pos--;
			leading--;
		}
		if (trailing > leading) {
			remove_last_line(&preimage);
			remove_last_line(&postimage);
			trailing--;
		}
	}

	if (applied_pos >= 0) {
		if (new_blank_lines_at_end &&
		    preimage.nr + applied_pos >= img->nr &&
		    (ws_rule & WS_BLANK_AT_EOF) &&
		    state->ws_error_action != nowarn_ws_error) {
			record_ws_error(state, WS_BLANK_AT_EOF, "+", 1,
					found_new_blank_lines_at_end);
			if (state->ws_error_action == correct_ws_error) {
				while (new_blank_lines_at_end--)
					remove_last_line(&postimage);
			}
			/*
			 * We would want to prevent write_out_results()
			 * from taking place in apply_patch() that follows
			 * the callchain led us here, which is:
			 * apply_patch->check_patch_list->check_patch->
			 * apply_data->apply_fragments->apply_one_fragment
			 */
			if (state->ws_error_action == die_on_ws_error)
				state->apply = 0;
		}

		if (state->apply_verbosity > verbosity_normal && applied_pos != pos) {
			int offset = applied_pos - pos;
			if (state->apply_in_reverse)
				offset = 0 - offset;
			fprintf_ln(stderr,
				   Q_("Hunk #%d succeeded at %d (offset %d line).",
				      "Hunk #%d succeeded at %d (offset %d lines).",
				      offset),
				   nth_fragment, applied_pos + 1, offset);
		}

		/*
		 * Warn if it was necessary to reduce the number
		 * of context lines.
		 */
		if ((leading != frag->leading ||
		     trailing != frag->trailing) && state->apply_verbosity > verbosity_silent)
			fprintf_ln(stderr, _("Context reduced to (%ld/%ld)"
					     " to apply fragment at %d"),
				   leading, trailing, applied_pos+1);
		update_image(state, img, applied_pos, &preimage, &postimage);
	} else {
		if (state->apply_verbosity > verbosity_normal)
			error(_("while searching for:\n%.*s"),
			      (int)(old - oldlines), oldlines);
	}

out:
	free(oldlines);
	strbuf_release(&newlines);
	free(preimage.line_allocated);
	free(postimage.line_allocated);

	return (applied_pos < 0);
}

static int apply_binary_fragment(struct apply_state *state,
				 struct image *img,
				 struct patch *patch)
{
	struct fragment *fragment = patch->fragments;
	unsigned long len;
	void *dst;

	if (!fragment)
		return error(_("missing binary patch data for '%s'"),
			     patch->new_name ?
			     patch->new_name :
			     patch->old_name);

	/* Binary patch is irreversible without the optional second hunk */
	if (state->apply_in_reverse) {
		if (!fragment->next)
			return error(_("cannot reverse-apply a binary patch "
				       "without the reverse hunk to '%s'"),
				     patch->new_name
				     ? patch->new_name : patch->old_name);
		fragment = fragment->next;
	}
	switch (fragment->binary_patch_method) {
	case BINARY_DELTA_DEFLATED:
		dst = patch_delta(img->buf, img->len, fragment->patch,
				  fragment->size, &len);
		if (!dst)
			return -1;
		clear_image(img);
		img->buf = dst;
		img->len = len;
		return 0;
	case BINARY_LITERAL_DEFLATED:
		clear_image(img);
		img->len = fragment->size;
		img->buf = xmemdupz(fragment->patch, img->len);
		return 0;
	}
	return -1;
}

/*
 * Replace "img" with the result of applying the binary patch.
 * The binary patch data itself in patch->fragment is still kept
 * but the preimage prepared by the caller in "img" is freed here
 * or in the helper function apply_binary_fragment() this calls.
 */
static int apply_binary(struct apply_state *state,
			struct image *img,
			struct patch *patch)
{
	const char *name = patch->old_name ? patch->old_name : patch->new_name;
	struct object_id oid;
	const unsigned hexsz = the_hash_algo->hexsz;

	/*
	 * For safety, we require patch index line to contain
	 * full hex textual object ID for old and new, at least for now.
	 */
	if (strlen(patch->old_oid_prefix) != hexsz ||
	    strlen(patch->new_oid_prefix) != hexsz ||
	    get_oid_hex(patch->old_oid_prefix, &oid) ||
	    get_oid_hex(patch->new_oid_prefix, &oid))
		return error(_("cannot apply binary patch to '%s' "
			       "without full index line"), name);

	if (patch->old_name) {
		/*
		 * See if the old one matches what the patch
		 * applies to.
		 */
		hash_object_file(the_hash_algo, img->buf, img->len, OBJ_BLOB,
				 &oid);
		if (strcmp(oid_to_hex(&oid), patch->old_oid_prefix))
			return error(_("the patch applies to '%s' (%s), "
				       "which does not match the "
				       "current contents."),
				     name, oid_to_hex(&oid));
	}
	else {
		/* Otherwise, the old one must be empty. */
		if (img->len)
			return error(_("the patch applies to an empty "
				       "'%s' but it is not empty"), name);
	}

	get_oid_hex(patch->new_oid_prefix, &oid);
	if (is_null_oid(&oid)) {
		clear_image(img);
		return 0; /* deletion patch */
	}

	if (has_object(the_repository, &oid, 0)) {
		/* We already have the postimage */
		enum object_type type;
		unsigned long size;
		char *result;

		result = repo_read_object_file(the_repository, &oid, &type,
					       &size);
		if (!result)
			return error(_("the necessary postimage %s for "
				       "'%s' cannot be read"),
				     patch->new_oid_prefix, name);
		clear_image(img);
		img->buf = result;
		img->len = size;
	} else {
		/*
		 * We have verified buf matches the preimage;
		 * apply the patch data to it, which is stored
		 * in the patch->fragments->{patch,size}.
		 */
		if (apply_binary_fragment(state, img, patch))
			return error(_("binary patch does not apply to '%s'"),
				     name);

		/* verify that the result matches */
		hash_object_file(the_hash_algo, img->buf, img->len, OBJ_BLOB,
				 &oid);
		if (strcmp(oid_to_hex(&oid), patch->new_oid_prefix))
			return error(_("binary patch to '%s' creates incorrect result (expecting %s, got %s)"),
				name, patch->new_oid_prefix, oid_to_hex(&oid));
	}

	return 0;
}

static int apply_fragments(struct apply_state *state, struct image *img, struct patch *patch)
{
	struct fragment *frag = patch->fragments;
	const char *name = patch->old_name ? patch->old_name : patch->new_name;
	unsigned ws_rule = patch->ws_rule;
	unsigned inaccurate_eof = patch->inaccurate_eof;
	int nth = 0;

	if (patch->is_binary)
		return apply_binary(state, img, patch);

	while (frag) {
		nth++;
		if (apply_one_fragment(state, img, frag, inaccurate_eof, ws_rule, nth)) {
			error(_("patch failed: %s:%ld"), name, frag->oldpos);
			if (!state->apply_with_reject)
				return -1;
			frag->rejected = 1;
		}
		frag = frag->next;
	}
	return 0;
}

static int read_blob_object(struct strbuf *buf, const struct object_id *oid, unsigned mode)
{
	if (S_ISGITLINK(mode)) {
		strbuf_grow(buf, 100);
		strbuf_addf(buf, "Subproject commit %s\n", oid_to_hex(oid));
	} else {
		enum object_type type;
		unsigned long sz;
		char *result;

		result = repo_read_object_file(the_repository, oid, &type,
					       &sz);
		if (!result)
			return -1;
		/* XXX read_sha1_file NUL-terminates */
		strbuf_attach(buf, result, sz, sz + 1);
	}
	return 0;
}

static int read_file_or_gitlink(const struct cache_entry *ce, struct strbuf *buf)
{
	if (!ce)
		return 0;
	return read_blob_object(buf, &ce->oid, ce->ce_mode);
}

static struct patch *in_fn_table(struct apply_state *state, const char *name)
{
	struct string_list_item *item;

	if (!name)
		return NULL;

	item = string_list_lookup(&state->fn_table, name);
	if (item)
		return (struct patch *)item->util;

	return NULL;
}

/*
 * item->util in the filename table records the status of the path.
 * Usually it points at a patch (whose result records the contents
 * of it after applying it), but it could be PATH_WAS_DELETED for a
 * path that a previously applied patch has already removed, or
 * PATH_TO_BE_DELETED for a path that a later patch would remove.
 *
 * The latter is needed to deal with a case where two paths A and B
 * are swapped by first renaming A to B and then renaming B to A;
 * moving A to B should not be prevented due to presence of B as we
 * will remove it in a later patch.
 */
#define PATH_TO_BE_DELETED ((struct patch *) -2)
#define PATH_WAS_DELETED ((struct patch *) -1)

static int to_be_deleted(struct patch *patch)
{
	return patch == PATH_TO_BE_DELETED;
}

static int was_deleted(struct patch *patch)
{
	return patch == PATH_WAS_DELETED;
}

static void add_to_fn_table(struct apply_state *state, struct patch *patch)
{
	struct string_list_item *item;

	/*
	 * Always add new_name unless patch is a deletion
	 * This should cover the cases for normal diffs,
	 * file creations and copies
	 */
	if (patch->new_name) {
		item = string_list_insert(&state->fn_table, patch->new_name);
		item->util = patch;
	}

	/*
	 * store a failure on rename/deletion cases because
	 * later chunks shouldn't patch old names
	 */
	if ((patch->new_name == NULL) || (patch->is_rename)) {
		item = string_list_insert(&state->fn_table, patch->old_name);
		item->util = PATH_WAS_DELETED;
	}
}

static void prepare_fn_table(struct apply_state *state, struct patch *patch)
{
	/*
	 * store information about incoming file deletion
	 */
	while (patch) {
		if ((patch->new_name == NULL) || (patch->is_rename)) {
			struct string_list_item *item;
			item = string_list_insert(&state->fn_table, patch->old_name);
			item->util = PATH_TO_BE_DELETED;
		}
		patch = patch->next;
	}
}

static int checkout_target(struct index_state *istate,
			   struct cache_entry *ce, struct stat *st)
{
	struct checkout costate = CHECKOUT_INIT;

	costate.refresh_cache = 1;
	costate.istate = istate;
	if (checkout_entry(ce, &costate, NULL, NULL) ||
	    lstat(ce->name, st))
		return error(_("cannot checkout %s"), ce->name);
	return 0;
}

static struct patch *previous_patch(struct apply_state *state,
				    struct patch *patch,
				    int *gone)
{
	struct patch *previous;

	*gone = 0;
	if (patch->is_copy || patch->is_rename)
		return NULL; /* "git" patches do not depend on the order */

	previous = in_fn_table(state, patch->old_name);
	if (!previous)
		return NULL;

	if (to_be_deleted(previous))
		return NULL; /* the deletion hasn't happened yet */

	if (was_deleted(previous))
		*gone = 1;

	return previous;
}

static int verify_index_match(struct apply_state *state,
			      const struct cache_entry *ce,
			      struct stat *st)
{
	if (S_ISGITLINK(ce->ce_mode)) {
		if (!S_ISDIR(st->st_mode))
			return -1;
		return 0;
	}
	return ie_match_stat(state->repo->index, ce, st,
			     CE_MATCH_IGNORE_VALID | CE_MATCH_IGNORE_SKIP_WORKTREE);
}

#define SUBMODULE_PATCH_WITHOUT_INDEX 1

static int load_patch_target(struct apply_state *state,
			     struct strbuf *buf,
			     const struct cache_entry *ce,
			     struct stat *st,
			     struct patch *patch,
			     const char *name,
			     unsigned expected_mode)
{
	if (state->cached || state->check_index) {
		if (read_file_or_gitlink(ce, buf))
			return error(_("failed to read %s"), name);
	} else if (name) {
		if (S_ISGITLINK(expected_mode)) {
			if (ce)
				return read_file_or_gitlink(ce, buf);
			else
				return SUBMODULE_PATCH_WITHOUT_INDEX;
		} else if (has_symlink_leading_path(name, strlen(name))) {
			return error(_("reading from '%s' beyond a symbolic link"), name);
		} else {
			if (read_old_data(st, patch, name, buf))
				return error(_("failed to read %s"), name);
		}
	}
	return 0;
}

/*
 * We are about to apply "patch"; populate the "image" with the
 * current version we have, from the working tree or from the index,
 * depending on the situation e.g. --cached/--index.  If we are
 * applying a non-git patch that incrementally updates the tree,
 * we read from the result of a previous diff.
 */
static int load_preimage(struct apply_state *state,
			 struct image *image,
			 struct patch *patch, struct stat *st,
			 const struct cache_entry *ce)
{
	struct strbuf buf = STRBUF_INIT;
	size_t len;
	char *img;
	struct patch *previous;
	int status;

	previous = previous_patch(state, patch, &status);
	if (status)
		return error(_("path %s has been renamed/deleted"),
			     patch->old_name);
	if (previous) {
		/* We have a patched copy in memory; use that. */
		strbuf_add(&buf, previous->result, previous->resultsize);
	} else {
		status = load_patch_target(state, &buf, ce, st, patch,
					   patch->old_name, patch->old_mode);
		if (status < 0)
			return status;
		else if (status == SUBMODULE_PATCH_WITHOUT_INDEX) {
			/*
			 * There is no way to apply subproject
			 * patch without looking at the index.
			 * NEEDSWORK: shouldn't this be flagged
			 * as an error???
			 */
			free_fragment_list(patch->fragments);
			patch->fragments = NULL;
		} else if (status) {
			return error(_("failed to read %s"), patch->old_name);
		}
	}

	img = strbuf_detach(&buf, &len);
	prepare_image(image, img, len, !patch->is_binary);
	return 0;
}

static int resolve_to(struct image *image, const struct object_id *result_id)
{
	unsigned long size;
	enum object_type type;

	clear_image(image);

	image->buf = repo_read_object_file(the_repository, result_id, &type,
					   &size);
	if (!image->buf || type != OBJ_BLOB)
		die("unable to read blob object %s", oid_to_hex(result_id));
	image->len = size;

	return 0;
}

static int three_way_merge(struct apply_state *state,
			   struct image *image,
			   char *path,
			   const struct object_id *base,
			   const struct object_id *ours,
			   const struct object_id *theirs)
{
	mmfile_t base_file, our_file, their_file;
	mmbuffer_t result = { NULL };
	enum ll_merge_result status;

	/* resolve trivial cases first */
	if (oideq(base, ours))
		return resolve_to(image, theirs);
	else if (oideq(base, theirs) || oideq(ours, theirs))
		return resolve_to(image, ours);

	read_mmblob(&base_file, base);
	read_mmblob(&our_file, ours);
	read_mmblob(&their_file, theirs);
	status = ll_merge(&result, path,
			  &base_file, "base",
			  &our_file, "ours",
			  &their_file, "theirs",
			  state->repo->index,
			  NULL);
	if (status == LL_MERGE_BINARY_CONFLICT)
		warning("Cannot merge binary files: %s (%s vs. %s)",
			path, "ours", "theirs");
	free(base_file.ptr);
	free(our_file.ptr);
	free(their_file.ptr);
	if (status < 0 || !result.ptr) {
		free(result.ptr);
		return -1;
	}
	clear_image(image);
	image->buf = result.ptr;
	image->len = result.size;

	return status;
}

/*
 * When directly falling back to add/add three-way merge, we read from
 * the current contents of the new_name.  In no cases other than that
 * this function will be called.
 */
static int load_current(struct apply_state *state,
			struct image *image,
			struct patch *patch)
{
	struct strbuf buf = STRBUF_INIT;
	int status, pos;
	size_t len;
	char *img;
	struct stat st;
	struct cache_entry *ce;
	char *name = patch->new_name;
	unsigned mode = patch->new_mode;

	if (!patch->is_new)
		BUG("patch to %s is not a creation", patch->old_name);

	pos = index_name_pos(state->repo->index, name, strlen(name));
	if (pos < 0)
		return error(_("%s: does not exist in index"), name);
	ce = state->repo->index->cache[pos];
	if (lstat(name, &st)) {
		if (errno != ENOENT)
			return error_errno("%s", name);
		if (checkout_target(state->repo->index, ce, &st))
			return -1;
	}
	if (verify_index_match(state, ce, &st))
		return error(_("%s: does not match index"), name);

	status = load_patch_target(state, &buf, ce, &st, patch, name, mode);
	if (status < 0)
		return status;
	else if (status)
		return -1;
	img = strbuf_detach(&buf, &len);
	prepare_image(image, img, len, !patch->is_binary);
	return 0;
}

static int try_threeway(struct apply_state *state,
			struct image *image,
			struct patch *patch,
			struct stat *st,
			const struct cache_entry *ce)
{
	struct object_id pre_oid, post_oid, our_oid;
	struct strbuf buf = STRBUF_INIT;
	size_t len;
	int status;
	char *img;
	struct image tmp_image;

	/* No point falling back to 3-way merge in these cases */
	if (patch->is_delete ||
	    S_ISGITLINK(patch->old_mode) || S_ISGITLINK(patch->new_mode) ||
	    (patch->is_new && !patch->direct_to_threeway) ||
	    (patch->is_rename && !patch->lines_added && !patch->lines_deleted))
		return -1;

	/* Preimage the patch was prepared for */
	if (patch->is_new)
		write_object_file("", 0, OBJ_BLOB, &pre_oid);
	else if (repo_get_oid(the_repository, patch->old_oid_prefix, &pre_oid) ||
		 read_blob_object(&buf, &pre_oid, patch->old_mode))
		return error(_("repository lacks the necessary blob to perform 3-way merge."));

	if (state->apply_verbosity > verbosity_silent && patch->direct_to_threeway)
		fprintf(stderr, _("Performing three-way merge...\n"));

	img = strbuf_detach(&buf, &len);
	prepare_image(&tmp_image, img, len, 1);
	/* Apply the patch to get the post image */
	if (apply_fragments(state, &tmp_image, patch) < 0) {
		clear_image(&tmp_image);
		return -1;
	}
	/* post_oid is theirs */
	write_object_file(tmp_image.buf, tmp_image.len, OBJ_BLOB, &post_oid);
	clear_image(&tmp_image);

	/* our_oid is ours */
	if (patch->is_new) {
		if (load_current(state, &tmp_image, patch))
			return error(_("cannot read the current contents of '%s'"),
				     patch->new_name);
	} else {
		if (load_preimage(state, &tmp_image, patch, st, ce))
			return error(_("cannot read the current contents of '%s'"),
				     patch->old_name);
	}
	write_object_file(tmp_image.buf, tmp_image.len, OBJ_BLOB, &our_oid);
	clear_image(&tmp_image);

	/* in-core three-way merge between post and our using pre as base */
	status = three_way_merge(state, image, patch->new_name,
				 &pre_oid, &our_oid, &post_oid);
	if (status < 0) {
		if (state->apply_verbosity > verbosity_silent)
			fprintf(stderr,
				_("Failed to perform three-way merge...\n"));
		return status;
	}

	if (status) {
		patch->conflicted_threeway = 1;
		if (patch->is_new)
			oidclr(&patch->threeway_stage[0]);
		else
			oidcpy(&patch->threeway_stage[0], &pre_oid);
		oidcpy(&patch->threeway_stage[1], &our_oid);
		oidcpy(&patch->threeway_stage[2], &post_oid);
		if (state->apply_verbosity > verbosity_silent)
			fprintf(stderr,
				_("Applied patch to '%s' with conflicts.\n"),
				patch->new_name);
	} else {
		if (state->apply_verbosity > verbosity_silent)
			fprintf(stderr,
				_("Applied patch to '%s' cleanly.\n"),
				patch->new_name);
	}
	return 0;
}

static int apply_data(struct apply_state *state, struct patch *patch,
		      struct stat *st, const struct cache_entry *ce)
{
	struct image image;

	if (load_preimage(state, &image, patch, st, ce) < 0)
		return -1;

	if (!state->threeway || try_threeway(state, &image, patch, st, ce) < 0) {
		if (state->apply_verbosity > verbosity_silent &&
		    state->threeway && !patch->direct_to_threeway)
			fprintf(stderr, _("Falling back to direct application...\n"));

		/* Note: with --reject, apply_fragments() returns 0 */
		if (patch->direct_to_threeway || apply_fragments(state, &image, patch) < 0)
			return -1;
	}
	patch->result = image.buf;
	patch->resultsize = image.len;
	add_to_fn_table(state, patch);
	free(image.line_allocated);

	if (0 < patch->is_delete && patch->resultsize)
		return error(_("removal patch leaves file contents"));

	return 0;
}

/*
 * If "patch" that we are looking at modifies or deletes what we have,
 * we would want it not to lose any local modification we have, either
 * in the working tree or in the index.
 *
 * This also decides if a non-git patch is a creation patch or a
 * modification to an existing empty file.  We do not check the state
 * of the current tree for a creation patch in this function; the caller
 * check_patch() separately makes sure (and errors out otherwise) that
 * the path the patch creates does not exist in the current tree.
 */
static int check_preimage(struct apply_state *state,
			  struct patch *patch,
			  struct cache_entry **ce,
			  struct stat *st)
{
	const char *old_name = patch->old_name;
	struct patch *previous = NULL;
	int stat_ret = 0, status;
	unsigned st_mode = 0;

	if (!old_name)
		return 0;

	assert(patch->is_new <= 0);
	previous = previous_patch(state, patch, &status);

	if (status)
		return error(_("path %s has been renamed/deleted"), old_name);
	if (previous) {
		st_mode = previous->new_mode;
	} else if (!state->cached) {
		stat_ret = lstat(old_name, st);
		if (stat_ret && errno != ENOENT)
			return error_errno("%s", old_name);
	}

	if (state->check_index && !previous) {
		int pos = index_name_pos(state->repo->index, old_name,
					 strlen(old_name));
		if (pos < 0) {
			if (patch->is_new < 0)
				goto is_new;
			return error(_("%s: does not exist in index"), old_name);
		}
		*ce = state->repo->index->cache[pos];
		if (stat_ret < 0) {
			if (checkout_target(state->repo->index, *ce, st))
				return -1;
		}
		if (!state->cached && verify_index_match(state, *ce, st))
			return error(_("%s: does not match index"), old_name);
		if (state->cached)
			st_mode = (*ce)->ce_mode;
	} else if (stat_ret < 0) {
		if (patch->is_new < 0)
			goto is_new;
		return error_errno("%s", old_name);
	}

	if (!state->cached && !previous)
		st_mode = ce_mode_from_stat(*ce, st->st_mode);

	if (patch->is_new < 0)
		patch->is_new = 0;
	if (!patch->old_mode)
		patch->old_mode = st_mode;
	if ((st_mode ^ patch->old_mode) & S_IFMT)
		return error(_("%s: wrong type"), old_name);
	if (st_mode != patch->old_mode)
		warning(_("%s has type %o, expected %o"),
			old_name, st_mode, patch->old_mode);
	if (!patch->new_mode && !patch->is_delete)
		patch->new_mode = st_mode;
	return 0;

 is_new:
	patch->is_new = 1;
	patch->is_delete = 0;
	FREE_AND_NULL(patch->old_name);
	return 0;
}


#define EXISTS_IN_INDEX 1
#define EXISTS_IN_WORKTREE 2
#define EXISTS_IN_INDEX_AS_ITA 3

static int check_to_create(struct apply_state *state,
			   const char *new_name,
			   int ok_if_exists)
{
	struct stat nst;

	if (state->check_index && (!ok_if_exists || !state->cached)) {
		int pos;

		pos = index_name_pos(state->repo->index, new_name, strlen(new_name));
		if (pos >= 0) {
			struct cache_entry *ce = state->repo->index->cache[pos];

			/* allow ITA, as they do not yet exist in the index */
			if (!ok_if_exists && !(ce->ce_flags & CE_INTENT_TO_ADD))
				return EXISTS_IN_INDEX;

			/* ITA entries can never match working tree files */
			if (!state->cached && (ce->ce_flags & CE_INTENT_TO_ADD))
				return EXISTS_IN_INDEX_AS_ITA;
		}
	}

	if (state->cached)
		return 0;

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
		if (has_symlink_leading_path(new_name, strlen(new_name)))
			return 0;

		return EXISTS_IN_WORKTREE;
	} else if (!is_missing_file_error(errno)) {
		return error_errno("%s", new_name);
	}
	return 0;
}

static void prepare_symlink_changes(struct apply_state *state, struct patch *patch)
{
	for ( ; patch; patch = patch->next) {
		if ((patch->old_name && S_ISLNK(patch->old_mode)) &&
		    (patch->is_rename || patch->is_delete))
			/* the symlink at patch->old_name is removed */
			strset_add(&state->removed_symlinks, patch->old_name);

		if (patch->new_name && S_ISLNK(patch->new_mode))
			/* the symlink at patch->new_name is created or remains */
			strset_add(&state->kept_symlinks, patch->new_name);
	}
}

static int path_is_beyond_symlink_1(struct apply_state *state, struct strbuf *name)
{
	do {
		while (--name->len && name->buf[name->len] != '/')
			; /* scan backwards */
		if (!name->len)
			break;
		name->buf[name->len] = '\0';
		if (strset_contains(&state->kept_symlinks, name->buf))
			return 1;
		if (strset_contains(&state->removed_symlinks, name->buf))
			/*
			 * This cannot be "return 0", because we may
			 * see a new one created at a higher level.
			 */
			continue;

		/* otherwise, check the preimage */
		if (state->check_index) {
			struct cache_entry *ce;

			ce = index_file_exists(state->repo->index, name->buf,
					       name->len, ignore_case);
			if (ce && S_ISLNK(ce->ce_mode))
				return 1;
		} else {
			struct stat st;
			if (!lstat(name->buf, &st) && S_ISLNK(st.st_mode))
				return 1;
		}
	} while (1);
	return 0;
}

static int path_is_beyond_symlink(struct apply_state *state, const char *name_)
{
	int ret;
	struct strbuf name = STRBUF_INIT;

	assert(*name_ != '\0');
	strbuf_addstr(&name, name_);
	ret = path_is_beyond_symlink_1(state, &name);
	strbuf_release(&name);

	return ret;
}

static int check_unsafe_path(struct patch *patch)
{
	const char *old_name = NULL;
	const char *new_name = NULL;
	if (patch->is_delete)
		old_name = patch->old_name;
	else if (!patch->is_new && !patch->is_copy)
		old_name = patch->old_name;
	if (!patch->is_delete)
		new_name = patch->new_name;

	if (old_name && !verify_path(old_name, patch->old_mode))
		return error(_("invalid path '%s'"), old_name);
	if (new_name && !verify_path(new_name, patch->new_mode))
		return error(_("invalid path '%s'"), new_name);
	return 0;
}

/*
 * Check and apply the patch in-core; leave the result in patch->result
 * for the caller to write it out to the final destination.
 */
static int check_patch(struct apply_state *state, struct patch *patch)
{
	struct stat st;
	const char *old_name = patch->old_name;
	const char *new_name = patch->new_name;
	const char *name = old_name ? old_name : new_name;
	struct cache_entry *ce = NULL;
	struct patch *tpatch;
	int ok_if_exists;
	int status;

	patch->rejected = 1; /* we will drop this after we succeed */

	status = check_preimage(state, patch, &ce, &st);
	if (status)
		return status;
	old_name = patch->old_name;

	/*
	 * A type-change diff is always split into a patch to delete
	 * old, immediately followed by a patch to create new (see
	 * diff.c::run_diff()); in such a case it is Ok that the entry
	 * to be deleted by the previous patch is still in the working
	 * tree and in the index.
	 *
	 * A patch to swap-rename between A and B would first rename A
	 * to B and then rename B to A.  While applying the first one,
	 * the presence of B should not stop A from getting renamed to
	 * B; ask to_be_deleted() about the later rename.  Removal of
	 * B and rename from A to B is handled the same way by asking
	 * was_deleted().
	 */
	if ((tpatch = in_fn_table(state, new_name)) &&
	    (was_deleted(tpatch) || to_be_deleted(tpatch)))
		ok_if_exists = 1;
	else
		ok_if_exists = 0;

	if (new_name &&
	    ((0 < patch->is_new) || patch->is_rename || patch->is_copy)) {
		int err = check_to_create(state, new_name, ok_if_exists);

		if (err && state->threeway) {
			patch->direct_to_threeway = 1;
		} else switch (err) {
		case 0:
			break; /* happy */
		case EXISTS_IN_INDEX:
			return error(_("%s: already exists in index"), new_name);
		case EXISTS_IN_INDEX_AS_ITA:
			return error(_("%s: does not match index"), new_name);
		case EXISTS_IN_WORKTREE:
			return error(_("%s: already exists in working directory"),
				     new_name);
		default:
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
		if ((patch->old_mode ^ patch->new_mode) & S_IFMT) {
			if (same)
				return error(_("new mode (%o) of %s does not "
					       "match old mode (%o)"),
					patch->new_mode, new_name,
					patch->old_mode);
			else
				return error(_("new mode (%o) of %s does not "
					       "match old mode (%o) of %s"),
					patch->new_mode, new_name,
					patch->old_mode, old_name);
		}
	}

	if (!state->unsafe_paths && check_unsafe_path(patch))
		return -128;

	/*
	 * An attempt to read from or delete a path that is beyond a
	 * symbolic link will be prevented by load_patch_target() that
	 * is called at the beginning of apply_data() so we do not
	 * have to worry about a patch marked with "is_delete" bit
	 * here.  We however need to make sure that the patch result
	 * is not deposited to a path that is beyond a symbolic link
	 * here.
	 */
	if (!patch->is_delete && path_is_beyond_symlink(state, patch->new_name))
		return error(_("affected file '%s' is beyond a symbolic link"),
			     patch->new_name);

	if (apply_data(state, patch, &st, ce) < 0)
		return error(_("%s: patch does not apply"), name);
	patch->rejected = 0;
	return 0;
}

static int check_patch_list(struct apply_state *state, struct patch *patch)
{
	int err = 0;

	prepare_symlink_changes(state, patch);
	prepare_fn_table(state, patch);
	while (patch) {
		int res;
		if (state->apply_verbosity > verbosity_normal)
			say_patch_name(stderr,
				       _("Checking patch %s..."), patch);
		res = check_patch(state, patch);
		if (res == -128)
			return -128;
		err |= res;
		patch = patch->next;
	}
	return err;
}

static int read_apply_cache(struct apply_state *state)
{
	if (state->index_file)
		return read_index_from(state->repo->index, state->index_file,
				       get_git_dir());
	else
		return repo_read_index(state->repo);
}

/* This function tries to read the object name from the current index */
static int get_current_oid(struct apply_state *state, const char *path,
			   struct object_id *oid)
{
	int pos;

	if (read_apply_cache(state) < 0)
		return -1;
	pos = index_name_pos(state->repo->index, path, strlen(path));
	if (pos < 0)
		return -1;
	oidcpy(oid, &state->repo->index->cache[pos]->oid);
	return 0;
}

static int preimage_oid_in_gitlink_patch(struct patch *p, struct object_id *oid)
{
	/*
	 * A usable gitlink patch has only one fragment (hunk) that looks like:
	 * @@ -1 +1 @@
	 * -Subproject commit <old sha1>
	 * +Subproject commit <new sha1>
	 * or
	 * @@ -1 +0,0 @@
	 * -Subproject commit <old sha1>
	 * for a removal patch.
	 */
	struct fragment *hunk = p->fragments;
	static const char heading[] = "-Subproject commit ";
	char *preimage;

	if (/* does the patch have only one hunk? */
	    hunk && !hunk->next &&
	    /* is its preimage one line? */
	    hunk->oldpos == 1 && hunk->oldlines == 1 &&
	    /* does preimage begin with the heading? */
	    (preimage = memchr(hunk->patch, '\n', hunk->size)) != NULL &&
	    starts_with(++preimage, heading) &&
	    /* does it record full SHA-1? */
	    !get_oid_hex(preimage + sizeof(heading) - 1, oid) &&
	    preimage[sizeof(heading) + the_hash_algo->hexsz - 1] == '\n' &&
	    /* does the abbreviated name on the index line agree with it? */
	    starts_with(preimage + sizeof(heading) - 1, p->old_oid_prefix))
		return 0; /* it all looks fine */

	/* we may have full object name on the index line */
	return get_oid_hex(p->old_oid_prefix, oid);
}

/* Build an index that contains just the files needed for a 3way merge */
static int build_fake_ancestor(struct apply_state *state, struct patch *list)
{
	struct patch *patch;
	struct index_state result = INDEX_STATE_INIT(state->repo);
	struct lock_file lock = LOCK_INIT;
	int res;

	/* Once we start supporting the reverse patch, it may be
	 * worth showing the new sha1 prefix, but until then...
	 */
	for (patch = list; patch; patch = patch->next) {
		struct object_id oid;
		struct cache_entry *ce;
		const char *name;

		name = patch->old_name ? patch->old_name : patch->new_name;
		if (0 < patch->is_new)
			continue;

		if (S_ISGITLINK(patch->old_mode)) {
			if (!preimage_oid_in_gitlink_patch(patch, &oid))
				; /* ok, the textual part looks sane */
			else
				return error(_("sha1 information is lacking or "
					       "useless for submodule %s"), name);
		} else if (!repo_get_oid_blob(the_repository, patch->old_oid_prefix, &oid)) {
			; /* ok */
		} else if (!patch->lines_added && !patch->lines_deleted) {
			/* mode-only change: update the current */
			if (get_current_oid(state, patch->old_name, &oid))
				return error(_("mode change for %s, which is not "
					       "in current HEAD"), name);
		} else
			return error(_("sha1 information is lacking or useless "
				       "(%s)."), name);

		ce = make_cache_entry(&result, patch->old_mode, &oid, name, 0, 0);
		if (!ce)
			return error(_("make_cache_entry failed for path '%s'"),
				     name);
		if (add_index_entry(&result, ce, ADD_CACHE_OK_TO_ADD)) {
			discard_cache_entry(ce);
			return error(_("could not add %s to temporary index"),
				     name);
		}
	}

	hold_lock_file_for_update(&lock, state->fake_ancestor, LOCK_DIE_ON_ERROR);
	res = write_locked_index(&result, &lock, COMMIT_LOCK);
	discard_index(&result);

	if (res)
		return error(_("could not write temporary index to %s"),
			     state->fake_ancestor);

	return 0;
}

static void stat_patch_list(struct apply_state *state, struct patch *patch)
{
	int files, adds, dels;

	for (files = adds = dels = 0 ; patch ; patch = patch->next) {
		files++;
		adds += patch->lines_added;
		dels += patch->lines_deleted;
		show_stats(state, patch);
	}

	print_stat_summary(stdout, files, adds, dels);
}

static void numstat_patch_list(struct apply_state *state,
			       struct patch *patch)
{
	for ( ; patch; patch = patch->next) {
		const char *name;
		name = patch->new_name ? patch->new_name : patch->old_name;
		if (patch->is_binary)
			printf("-\t-\t");
		else
			printf("%d\t%d\t", patch->lines_added, patch->lines_deleted);
		write_name_quoted(name, stdout, state->line_termination);
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
	const char *old_name, *new_name;

	/* Find common prefix */
	old_name = p->old_name;
	new_name = p->new_name;
	while (1) {
		const char *slash_old, *slash_new;
		slash_old = strchr(old_name, '/');
		slash_new = strchr(new_name, '/');
		if (!slash_old ||
		    !slash_new ||
		    slash_old - old_name != slash_new - new_name ||
		    memcmp(old_name, new_name, slash_new - new_name))
			break;
		old_name = slash_old + 1;
		new_name = slash_new + 1;
	}
	/* p->old_name through old_name is the common prefix, and old_name and
	 * new_name through the end of names are renames
	 */
	if (old_name != p->old_name)
		printf(" %s %.*s{%s => %s} (%d%%)\n", renamecopy,
		       (int)(old_name - p->old_name), p->old_name,
		       old_name, new_name, p->score);
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

static void patch_stats(struct apply_state *state, struct patch *patch)
{
	int lines = patch->lines_added + patch->lines_deleted;

	if (lines > state->max_change)
		state->max_change = lines;
	if (patch->old_name) {
		int len = quote_c_style(patch->old_name, NULL, NULL, 0);
		if (!len)
			len = strlen(patch->old_name);
		if (len > state->max_len)
			state->max_len = len;
	}
	if (patch->new_name) {
		int len = quote_c_style(patch->new_name, NULL, NULL, 0);
		if (!len)
			len = strlen(patch->new_name);
		if (len > state->max_len)
			state->max_len = len;
	}
}

static int remove_file(struct apply_state *state, struct patch *patch, int rmdir_empty)
{
	if (state->update_index && !state->ita_only) {
		if (remove_file_from_index(state->repo->index, patch->old_name) < 0)
			return error(_("unable to remove %s from index"), patch->old_name);
	}
	if (!state->cached) {
		if (!remove_or_warn(patch->old_mode, patch->old_name) && rmdir_empty) {
			remove_path(patch->old_name);
		}
	}
	return 0;
}

static int add_index_file(struct apply_state *state,
			  const char *path,
			  unsigned mode,
			  void *buf,
			  unsigned long size)
{
	struct stat st;
	struct cache_entry *ce;
	int namelen = strlen(path);

	ce = make_empty_cache_entry(state->repo->index, namelen);
	memcpy(ce->name, path, namelen);
	ce->ce_mode = create_ce_mode(mode);
	ce->ce_flags = create_ce_flags(0);
	ce->ce_namelen = namelen;
	if (state->ita_only) {
		ce->ce_flags |= CE_INTENT_TO_ADD;
		set_object_name_for_intent_to_add_entry(ce);
	} else if (S_ISGITLINK(mode)) {
		const char *s;

		if (!skip_prefix(buf, "Subproject commit ", &s) ||
		    get_oid_hex(s, &ce->oid)) {
			discard_cache_entry(ce);
			return error(_("corrupt patch for submodule %s"), path);
		}
	} else {
		if (!state->cached) {
			if (lstat(path, &st) < 0) {
				discard_cache_entry(ce);
				return error_errno(_("unable to stat newly "
						     "created file '%s'"),
						   path);
			}
			fill_stat_cache_info(state->repo->index, ce, &st);
		}
		if (write_object_file(buf, size, OBJ_BLOB, &ce->oid) < 0) {
			discard_cache_entry(ce);
			return error(_("unable to create backing store "
				       "for newly created file %s"), path);
		}
	}
	if (add_index_entry(state->repo->index, ce, ADD_CACHE_OK_TO_ADD) < 0) {
		discard_cache_entry(ce);
		return error(_("unable to add cache entry for %s"), path);
	}

	return 0;
}

/*
 * Returns:
 *  -1 if an unrecoverable error happened
 *   0 if everything went well
 *   1 if a recoverable error happened
 */
static int try_create_file(struct apply_state *state, const char *path,
			   unsigned int mode, const char *buf,
			   unsigned long size)
{
	int fd, res;
	struct strbuf nbuf = STRBUF_INIT;

	if (S_ISGITLINK(mode)) {
		struct stat st;
		if (!lstat(path, &st) && S_ISDIR(st.st_mode))
			return 0;
		return !!mkdir(path, 0777);
	}

	if (has_symlinks && S_ISLNK(mode))
		/* Although buf:size is counted string, it also is NUL
		 * terminated.
		 */
		return !!symlink(buf, path);

	fd = open(path, O_CREAT | O_EXCL | O_WRONLY, (mode & 0100) ? 0777 : 0666);
	if (fd < 0)
		return 1;

	if (convert_to_working_tree(state->repo->index, path, buf, size, &nbuf, NULL)) {
		size = nbuf.len;
		buf  = nbuf.buf;
	}

	res = write_in_full(fd, buf, size) < 0;
	if (res)
		error_errno(_("failed to write to '%s'"), path);
	strbuf_release(&nbuf);

	if (close(fd) < 0 && !res)
		return error_errno(_("closing file '%s'"), path);

	return res ? -1 : 0;
}

/*
 * We optimistically assume that the directories exist,
 * which is true 99% of the time anyway. If they don't,
 * we create them and try again.
 *
 * Returns:
 *   -1 on error
 *   0 otherwise
 */
static int create_one_file(struct apply_state *state,
			   char *path,
			   unsigned mode,
			   const char *buf,
			   unsigned long size)
{
	int res;

	if (state->cached)
		return 0;

	/*
	 * We already try to detect whether files are beyond a symlink in our
	 * up-front checks. But in the case where symlinks are created by any
	 * of the intermediate hunks it can happen that our up-front checks
	 * didn't yet see the symlink, but at the point of arriving here there
	 * in fact is one. We thus repeat the check for symlinks here.
	 *
	 * Note that this does not make the up-front check obsolete as the
	 * failure mode is different:
	 *
	 * - The up-front checks cause us to abort before we have written
	 *   anything into the working directory. So when we exit this way the
	 *   working directory remains clean.
	 *
	 * - The checks here happen in the middle of the action where we have
	 *   already started to apply the patch. The end result will be a dirty
	 *   working directory.
	 *
	 * Ideally, we should update the up-front checks to catch what would
	 * happen when we apply the patch before we damage the working tree.
	 * We have all the information necessary to do so.  But for now, as a
	 * part of embargoed security work, having this check would serve as a
	 * reasonable first step.
	 */
	if (path_is_beyond_symlink(state, path))
		return error(_("affected file '%s' is beyond a symbolic link"), path);

	res = try_create_file(state, path, mode, buf, size);
	if (res < 0)
		return -1;
	if (!res)
		return 0;

	if (errno == ENOENT) {
		if (safe_create_leading_directories_no_share(path))
			return 0;
		res = try_create_file(state, path, mode, buf, size);
		if (res < 0)
			return -1;
		if (!res)
			return 0;
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
			char newpath[PATH_MAX];
			mksnpath(newpath, sizeof(newpath), "%s~%u", path, nr);
			res = try_create_file(state, newpath, mode, buf, size);
			if (res < 0)
				return -1;
			if (!res) {
				if (!rename(newpath, path))
					return 0;
				unlink_or_warn(newpath);
				break;
			}
			if (errno != EEXIST)
				break;
			++nr;
		}
	}
	return error_errno(_("unable to write file '%s' mode %o"),
			   path, mode);
}

static int add_conflicted_stages_file(struct apply_state *state,
				       struct patch *patch)
{
	int stage, namelen;
	unsigned mode;
	struct cache_entry *ce;

	if (!state->update_index)
		return 0;
	namelen = strlen(patch->new_name);
	mode = patch->new_mode ? patch->new_mode : (S_IFREG | 0644);

	remove_file_from_index(state->repo->index, patch->new_name);
	for (stage = 1; stage < 4; stage++) {
		if (is_null_oid(&patch->threeway_stage[stage - 1]))
			continue;
		ce = make_empty_cache_entry(state->repo->index, namelen);
		memcpy(ce->name, patch->new_name, namelen);
		ce->ce_mode = create_ce_mode(mode);
		ce->ce_flags = create_ce_flags(stage);
		ce->ce_namelen = namelen;
		oidcpy(&ce->oid, &patch->threeway_stage[stage - 1]);
		if (add_index_entry(state->repo->index, ce, ADD_CACHE_OK_TO_ADD) < 0) {
			discard_cache_entry(ce);
			return error(_("unable to add cache entry for %s"),
				     patch->new_name);
		}
	}

	return 0;
}

static int create_file(struct apply_state *state, struct patch *patch)
{
	char *path = patch->new_name;
	unsigned mode = patch->new_mode;
	unsigned long size = patch->resultsize;
	char *buf = patch->result;

	if (!mode)
		mode = S_IFREG | 0644;
	if (create_one_file(state, path, mode, buf, size))
		return -1;

	if (patch->conflicted_threeway)
		return add_conflicted_stages_file(state, patch);
	else if (state->update_index)
		return add_index_file(state, path, mode, buf, size);
	return 0;
}

/* phase zero is to remove, phase one is to create */
static int write_out_one_result(struct apply_state *state,
				struct patch *patch,
				int phase)
{
	if (patch->is_delete > 0) {
		if (phase == 0)
			return remove_file(state, patch, 1);
		return 0;
	}
	if (patch->is_new > 0 || patch->is_copy) {
		if (phase == 1)
			return create_file(state, patch);
		return 0;
	}
	/*
	 * Rename or modification boils down to the same
	 * thing: remove the old, write the new
	 */
	if (phase == 0)
		return remove_file(state, patch, patch->is_rename);
	if (phase == 1)
		return create_file(state, patch);
	return 0;
}

static int write_out_one_reject(struct apply_state *state, struct patch *patch)
{
	FILE *rej;
	char namebuf[PATH_MAX];
	struct fragment *frag;
	int cnt = 0;
	struct strbuf sb = STRBUF_INIT;

	for (cnt = 0, frag = patch->fragments; frag; frag = frag->next) {
		if (!frag->rejected)
			continue;
		cnt++;
	}

	if (!cnt) {
		if (state->apply_verbosity > verbosity_normal)
			say_patch_name(stderr,
				       _("Applied patch %s cleanly."), patch);
		return 0;
	}

	/* This should not happen, because a removal patch that leaves
	 * contents are marked "rejected" at the patch level.
	 */
	if (!patch->new_name)
		die(_("internal error"));

	/* Say this even without --verbose */
	strbuf_addf(&sb, Q_("Applying patch %%s with %d reject...",
			    "Applying patch %%s with %d rejects...",
			    cnt),
		    cnt);
	if (state->apply_verbosity > verbosity_silent)
		say_patch_name(stderr, sb.buf, patch);
	strbuf_release(&sb);

	cnt = strlen(patch->new_name);
	if (ARRAY_SIZE(namebuf) <= cnt + 5) {
		cnt = ARRAY_SIZE(namebuf) - 5;
		warning(_("truncating .rej filename to %.*s.rej"),
			cnt - 1, patch->new_name);
	}
	memcpy(namebuf, patch->new_name, cnt);
	memcpy(namebuf + cnt, ".rej", 5);

	rej = fopen(namebuf, "w");
	if (!rej)
		return error_errno(_("cannot open %s"), namebuf);

	/* Normal git tools never deal with .rej, so do not pretend
	 * this is a git patch by saying --git or giving extended
	 * headers.  While at it, maybe please "kompare" that wants
	 * the trailing TAB and some garbage at the end of line ;-).
	 */
	fprintf(rej, "diff a/%s b/%s\t(rejected hunks)\n",
		patch->new_name, patch->new_name);
	for (cnt = 1, frag = patch->fragments;
	     frag;
	     cnt++, frag = frag->next) {
		if (!frag->rejected) {
			if (state->apply_verbosity > verbosity_silent)
				fprintf_ln(stderr, _("Hunk #%d applied cleanly."), cnt);
			continue;
		}
		if (state->apply_verbosity > verbosity_silent)
			fprintf_ln(stderr, _("Rejected hunk #%d."), cnt);
		fprintf(rej, "%.*s", frag->size, frag->patch);
		if (frag->patch[frag->size-1] != '\n')
			fputc('\n', rej);
	}
	fclose(rej);
	return -1;
}

/*
 * Returns:
 *  -1 if an error happened
 *   0 if the patch applied cleanly
 *   1 if the patch did not apply cleanly
 */
static int write_out_results(struct apply_state *state, struct patch *list)
{
	int phase;
	int errs = 0;
	struct patch *l;
	struct string_list cpath = STRING_LIST_INIT_DUP;

	for (phase = 0; phase < 2; phase++) {
		l = list;
		while (l) {
			if (l->rejected)
				errs = 1;
			else {
				if (write_out_one_result(state, l, phase)) {
					string_list_clear(&cpath, 0);
					return -1;
				}
				if (phase == 1) {
					if (write_out_one_reject(state, l))
						errs = 1;
					if (l->conflicted_threeway) {
						string_list_append(&cpath, l->new_name);
						errs = 1;
					}
				}
			}
			l = l->next;
		}
	}

	if (cpath.nr) {
		struct string_list_item *item;

		string_list_sort(&cpath);
		if (state->apply_verbosity > verbosity_silent) {
			for_each_string_list_item(item, &cpath)
				fprintf(stderr, "U %s\n", item->string);
		}
		string_list_clear(&cpath, 0);

		/*
		 * rerere relies on the partially merged result being in the working
		 * tree with conflict markers, but that isn't written with --cached.
		 */
		if (!state->cached)
			repo_rerere(state->repo, 0);
	}

	return errs;
}

/*
 * Try to apply a patch.
 *
 * Returns:
 *  -128 if a bad error happened (like patch unreadable)
 *  -1 if patch did not apply and user cannot deal with it
 *   0 if the patch applied
 *   1 if the patch did not apply but user might fix it
 */
static int apply_patch(struct apply_state *state,
		       int fd,
		       const char *filename,
		       int options)
{
	size_t offset;
	struct strbuf buf = STRBUF_INIT; /* owns the patch text */
	struct patch *list = NULL, **listp = &list;
	int skipped_patch = 0;
	int res = 0;
	int flush_attributes = 0;

	state->patch_input_file = filename;
	if (read_patch_file(&buf, fd) < 0)
		return -128;
	offset = 0;
	while (offset < buf.len) {
		struct patch *patch;
		int nr;

		CALLOC_ARRAY(patch, 1);
		patch->inaccurate_eof = !!(options & APPLY_OPT_INACCURATE_EOF);
		patch->recount =  !!(options & APPLY_OPT_RECOUNT);
		nr = parse_chunk(state, buf.buf + offset, buf.len - offset, patch);
		if (nr < 0) {
			free_patch(patch);
			if (nr == -128) {
				res = -128;
				goto end;
			}
			break;
		}
		if (state->apply_in_reverse)
			reverse_patches(patch);
		if (use_patch(state, patch)) {
			patch_stats(state, patch);
			if (!list || !state->apply_in_reverse) {
				*listp = patch;
				listp = &patch->next;
			} else {
				patch->next = list;
				list = patch;
			}

			if ((patch->new_name &&
			     ends_with_path_components(patch->new_name,
						       GITATTRIBUTES_FILE)) ||
			    (patch->old_name &&
			     ends_with_path_components(patch->old_name,
						       GITATTRIBUTES_FILE)))
				flush_attributes = 1;
		}
		else {
			if (state->apply_verbosity > verbosity_normal)
				say_patch_name(stderr, _("Skipped patch '%s'."), patch);
			free_patch(patch);
			skipped_patch++;
		}
		offset += nr;
	}

	if (!list && !skipped_patch) {
		if (!state->allow_empty) {
			error(_("No valid patches in input (allow with \"--allow-empty\")"));
			res = -128;
		}
		goto end;
	}

	if (state->whitespace_error && (state->ws_error_action == die_on_ws_error))
		state->apply = 0;

	state->update_index = (state->check_index || state->ita_only) && state->apply;
	if (state->update_index && !is_lock_file_locked(&state->lock_file)) {
		if (state->index_file)
			hold_lock_file_for_update(&state->lock_file,
						  state->index_file,
						  LOCK_DIE_ON_ERROR);
		else
			repo_hold_locked_index(state->repo, &state->lock_file,
					       LOCK_DIE_ON_ERROR);
	}

	if (state->check_index && read_apply_cache(state) < 0) {
		error(_("unable to read index file"));
		res = -128;
		goto end;
	}

	if (state->check || state->apply) {
		int r = check_patch_list(state, list);
		if (r == -128) {
			res = -128;
			goto end;
		}
		if (r < 0 && !state->apply_with_reject) {
			res = -1;
			goto end;
		}
	}

	if (state->apply) {
		int write_res = write_out_results(state, list);
		if (write_res < 0) {
			res = -128;
			goto end;
		}
		if (write_res > 0) {
			/* with --3way, we still need to write the index out */
			res = state->apply_with_reject ? -1 : 1;
			goto end;
		}
	}

	if (state->fake_ancestor &&
	    build_fake_ancestor(state, list)) {
		res = -128;
		goto end;
	}

	if (state->diffstat && state->apply_verbosity > verbosity_silent)
		stat_patch_list(state, list);

	if (state->numstat && state->apply_verbosity > verbosity_silent)
		numstat_patch_list(state, list);

	if (state->summary && state->apply_verbosity > verbosity_silent)
		summary_patch_list(list);

	if (flush_attributes)
		reset_parsed_attributes();
end:
	free_patch_list(list);
	strbuf_release(&buf);
	string_list_clear(&state->fn_table, 0);
	return res;
}

static int apply_option_parse_exclude(const struct option *opt,
				      const char *arg, int unset)
{
	struct apply_state *state = opt->value;

	BUG_ON_OPT_NEG(unset);

	add_name_limit(state, arg, 1);
	return 0;
}

static int apply_option_parse_include(const struct option *opt,
				      const char *arg, int unset)
{
	struct apply_state *state = opt->value;

	BUG_ON_OPT_NEG(unset);

	add_name_limit(state, arg, 0);
	state->has_include = 1;
	return 0;
}

static int apply_option_parse_p(const struct option *opt,
				const char *arg,
				int unset)
{
	struct apply_state *state = opt->value;

	BUG_ON_OPT_NEG(unset);

	state->p_value = atoi(arg);
	state->p_value_known = 1;
	return 0;
}

static int apply_option_parse_space_change(const struct option *opt,
					   const char *arg, int unset)
{
	struct apply_state *state = opt->value;

	BUG_ON_OPT_ARG(arg);

	if (unset)
		state->ws_ignore_action = ignore_ws_none;
	else
		state->ws_ignore_action = ignore_ws_change;
	return 0;
}

static int apply_option_parse_whitespace(const struct option *opt,
					 const char *arg, int unset)
{
	struct apply_state *state = opt->value;

	BUG_ON_OPT_NEG(unset);

	state->whitespace_option = arg;
	if (parse_whitespace_option(state, arg))
		return -1;
	return 0;
}

static int apply_option_parse_directory(const struct option *opt,
					const char *arg, int unset)
{
	struct apply_state *state = opt->value;

	BUG_ON_OPT_NEG(unset);

	strbuf_reset(&state->root);
	strbuf_addstr(&state->root, arg);
	strbuf_complete(&state->root, '/');
	return 0;
}

int apply_all_patches(struct apply_state *state,
		      int argc,
		      const char **argv,
		      int options)
{
	int i;
	int res;
	int errs = 0;
	int read_stdin = 1;

	for (i = 0; i < argc; i++) {
		const char *arg = argv[i];
		char *to_free = NULL;
		int fd;

		if (!strcmp(arg, "-")) {
			res = apply_patch(state, 0, "<stdin>", options);
			if (res < 0)
				goto end;
			errs |= res;
			read_stdin = 0;
			continue;
		} else
			arg = to_free = prefix_filename(state->prefix, arg);

		fd = open(arg, O_RDONLY);
		if (fd < 0) {
			error(_("can't open patch '%s': %s"), arg, strerror(errno));
			res = -128;
			free(to_free);
			goto end;
		}
		read_stdin = 0;
		set_default_whitespace_mode(state);
		res = apply_patch(state, fd, arg, options);
		close(fd);
		free(to_free);
		if (res < 0)
			goto end;
		errs |= res;
	}
	set_default_whitespace_mode(state);
	if (read_stdin) {
		res = apply_patch(state, 0, "<stdin>", options);
		if (res < 0)
			goto end;
		errs |= res;
	}

	if (state->whitespace_error) {
		if (state->squelch_whitespace_errors &&
		    state->squelch_whitespace_errors < state->whitespace_error) {
			int squelched =
				state->whitespace_error - state->squelch_whitespace_errors;
			warning(Q_("squelched %d whitespace error",
				   "squelched %d whitespace errors",
				   squelched),
				squelched);
		}
		if (state->ws_error_action == die_on_ws_error) {
			error(Q_("%d line adds whitespace errors.",
				 "%d lines add whitespace errors.",
				 state->whitespace_error),
			      state->whitespace_error);
			res = -128;
			goto end;
		}
		if (state->applied_after_fixing_ws && state->apply)
			warning(Q_("%d line applied after"
				   " fixing whitespace errors.",
				   "%d lines applied after"
				   " fixing whitespace errors.",
				   state->applied_after_fixing_ws),
				state->applied_after_fixing_ws);
		else if (state->whitespace_error)
			warning(Q_("%d line adds whitespace errors.",
				   "%d lines add whitespace errors.",
				   state->whitespace_error),
				state->whitespace_error);
	}

	if (state->update_index) {
		res = write_locked_index(state->repo->index, &state->lock_file, COMMIT_LOCK);
		if (res) {
			error(_("Unable to write new index file"));
			res = -128;
			goto end;
		}
	}

	res = !!errs;

end:
	rollback_lock_file(&state->lock_file);

	if (state->apply_verbosity <= verbosity_silent) {
		set_error_routine(state->saved_error_routine);
		set_warn_routine(state->saved_warn_routine);
	}

	if (res > -1)
		return res;
	return (res == -1 ? 1 : 128);
}

int apply_parse_options(int argc, const char **argv,
			struct apply_state *state,
			int *force_apply, int *options,
			const char * const *apply_usage)
{
	struct option builtin_apply_options[] = {
		OPT_CALLBACK_F(0, "exclude", state, N_("path"),
			N_("don't apply changes matching the given path"),
			PARSE_OPT_NONEG, apply_option_parse_exclude),
		OPT_CALLBACK_F(0, "include", state, N_("path"),
			N_("apply changes matching the given path"),
			PARSE_OPT_NONEG, apply_option_parse_include),
		OPT_CALLBACK('p', NULL, state, N_("num"),
			N_("remove <num> leading slashes from traditional diff paths"),
			apply_option_parse_p),
		OPT_BOOL(0, "no-add", &state->no_add,
			N_("ignore additions made by the patch")),
		OPT_BOOL(0, "stat", &state->diffstat,
			N_("instead of applying the patch, output diffstat for the input")),
		OPT_NOOP_NOARG(0, "allow-binary-replacement"),
		OPT_NOOP_NOARG(0, "binary"),
		OPT_BOOL(0, "numstat", &state->numstat,
			N_("show number of added and deleted lines in decimal notation")),
		OPT_BOOL(0, "summary", &state->summary,
			N_("instead of applying the patch, output a summary for the input")),
		OPT_BOOL(0, "check", &state->check,
			N_("instead of applying the patch, see if the patch is applicable")),
		OPT_BOOL(0, "index", &state->check_index,
			N_("make sure the patch is applicable to the current index")),
		OPT_BOOL('N', "intent-to-add", &state->ita_only,
			N_("mark new files with `git add --intent-to-add`")),
		OPT_BOOL(0, "cached", &state->cached,
			N_("apply a patch without touching the working tree")),
		OPT_BOOL_F(0, "unsafe-paths", &state->unsafe_paths,
			   N_("accept a patch that touches outside the working area"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_BOOL(0, "apply", force_apply,
			N_("also apply the patch (use with --stat/--summary/--check)")),
		OPT_BOOL('3', "3way", &state->threeway,
			 N_( "attempt three-way merge, fall back on normal patch if that fails")),
		OPT_FILENAME(0, "build-fake-ancestor", &state->fake_ancestor,
			N_("build a temporary index based on embedded index information")),
		/* Think twice before adding "--nul" synonym to this */
		OPT_SET_INT('z', NULL, &state->line_termination,
			N_("paths are separated with NUL character"), '\0'),
		OPT_INTEGER('C', NULL, &state->p_context,
				N_("ensure at least <n> lines of context match")),
		OPT_CALLBACK(0, "whitespace", state, N_("action"),
			N_("detect new or modified lines that have whitespace errors"),
			apply_option_parse_whitespace),
		OPT_CALLBACK_F(0, "ignore-space-change", state, NULL,
			N_("ignore changes in whitespace when finding context"),
			PARSE_OPT_NOARG, apply_option_parse_space_change),
		OPT_CALLBACK_F(0, "ignore-whitespace", state, NULL,
			N_("ignore changes in whitespace when finding context"),
			PARSE_OPT_NOARG, apply_option_parse_space_change),
		OPT_BOOL('R', "reverse", &state->apply_in_reverse,
			N_("apply the patch in reverse")),
		OPT_BOOL(0, "unidiff-zero", &state->unidiff_zero,
			N_("don't expect at least one line of context")),
		OPT_BOOL(0, "reject", &state->apply_with_reject,
			N_("leave the rejected hunks in corresponding *.rej files")),
		OPT_BOOL(0, "allow-overlap", &state->allow_overlap,
			N_("allow overlapping hunks")),
		OPT__VERBOSITY(&state->apply_verbosity),
		OPT_BIT(0, "inaccurate-eof", options,
			N_("tolerate incorrectly detected missing new-line at the end of file"),
			APPLY_OPT_INACCURATE_EOF),
		OPT_BIT(0, "recount", options,
			N_("do not trust the line counts in the hunk headers"),
			APPLY_OPT_RECOUNT),
		OPT_CALLBACK(0, "directory", state, N_("root"),
			N_("prepend <root> to all filenames"),
			apply_option_parse_directory),
		OPT_BOOL(0, "allow-empty", &state->allow_empty,
			N_("don't return error for empty patches")),
		OPT_END()
	};

	return parse_options(argc, argv, state->prefix, builtin_apply_options, apply_usage, 0);
}
