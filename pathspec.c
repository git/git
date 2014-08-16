#include "cache.h"
#include "dir.h"
#include "pathspec.h"

/*
 * Finds which of the given pathspecs match items in the index.
 *
 * For each pathspec, sets the corresponding entry in the seen[] array
 * (which should be specs items long, i.e. the same size as pathspec)
 * to the nature of the "closest" (i.e. most specific) match found for
 * that pathspec in the index, if it was a closer type of match than
 * the existing entry.  As an optimization, matching is skipped
 * altogether if seen[] already only contains non-zero entries.
 *
 * If seen[] has not already been written to, it may make sense
 * to use find_pathspecs_matching_against_index() instead.
 */
void add_pathspec_matches_against_index(const struct pathspec *pathspec,
					char *seen)
{
	int num_unmatched = 0, i;

	/*
	 * Since we are walking the index as if we were walking the directory,
	 * we have to mark the matched pathspec as seen; otherwise we will
	 * mistakenly think that the user gave a pathspec that did not match
	 * anything.
	 */
	for (i = 0; i < pathspec->nr; i++)
		if (!seen[i])
			num_unmatched++;
	if (!num_unmatched)
		return;
	for (i = 0; i < active_nr; i++) {
		const struct cache_entry *ce = active_cache[i];
		ce_path_match(ce, pathspec, seen);
	}
}

/*
 * Finds which of the given pathspecs match items in the index.
 *
 * This is a one-shot wrapper around add_pathspec_matches_against_index()
 * which allocates, populates, and returns a seen[] array indicating the
 * nature of the "closest" (i.e. most specific) matches which each of the
 * given pathspecs achieves against all items in the index.
 */
char *find_pathspecs_matching_against_index(const struct pathspec *pathspec)
{
	char *seen = xcalloc(pathspec->nr, 1);
	add_pathspec_matches_against_index(pathspec, seen);
	return seen;
}

/*
 * Magic pathspec
 *
 * Possible future magic semantics include stuff like:
 *
 *	{ PATHSPEC_RECURSIVE, '*', "recursive" },
 *	{ PATHSPEC_REGEXP, '\0', "regexp" },
 *
 */

static struct pathspec_magic {
	unsigned bit;
	char mnemonic; /* this cannot be ':'! */
	const char *name;
} pathspec_magic[] = {
	{ PATHSPEC_FROMTOP, '/', "top" },
	{ PATHSPEC_LITERAL,   0, "literal" },
	{ PATHSPEC_GLOB,   '\0', "glob" },
	{ PATHSPEC_ICASE,  '\0', "icase" },
	{ PATHSPEC_EXCLUDE, '!', "exclude" },
};

static void prefix_short_magic(struct strbuf *sb, int prefixlen,
			       unsigned short_magic)
{
	int i;
	strbuf_addstr(sb, ":(");
	for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++)
		if (short_magic & pathspec_magic[i].bit) {
			if (sb->buf[sb->len - 1] != '(')
				strbuf_addch(sb, ',');
			strbuf_addstr(sb, pathspec_magic[i].name);
		}
	strbuf_addf(sb, ",prefix:%d)", prefixlen);
}

/*
 * Take an element of a pathspec and check for magic signatures.
 * Append the result to the prefix. Return the magic bitmap.
 *
 * For now, we only parse the syntax and throw out anything other than
 * "top" magic.
 *
 * NEEDSWORK: This needs to be rewritten when we start migrating
 * get_pathspec() users to use the "struct pathspec" interface.  For
 * example, a pathspec element may be marked as case-insensitive, but
 * the prefix part must always match literally, and a single stupid
 * string cannot express such a case.
 */
static unsigned prefix_pathspec(struct pathspec_item *item,
				unsigned *p_short_magic,
				const char **raw, unsigned flags,
				const char *prefix, int prefixlen,
				const char *elt)
{
	static int literal_global = -1;
	static int glob_global = -1;
	static int noglob_global = -1;
	static int icase_global = -1;
	unsigned magic = 0, short_magic = 0, global_magic = 0;
	const char *copyfrom = elt, *long_magic_end = NULL;
	char *match;
	int i, pathspec_prefix = -1;

	if (literal_global < 0)
		literal_global = git_env_bool(GIT_LITERAL_PATHSPECS_ENVIRONMENT, 0);
	if (literal_global)
		global_magic |= PATHSPEC_LITERAL;

	if (glob_global < 0)
		glob_global = git_env_bool(GIT_GLOB_PATHSPECS_ENVIRONMENT, 0);
	if (glob_global)
		global_magic |= PATHSPEC_GLOB;

	if (noglob_global < 0)
		noglob_global = git_env_bool(GIT_NOGLOB_PATHSPECS_ENVIRONMENT, 0);

	if (glob_global && noglob_global)
		die(_("global 'glob' and 'noglob' pathspec settings are incompatible"));


	if (icase_global < 0)
		icase_global = git_env_bool(GIT_ICASE_PATHSPECS_ENVIRONMENT, 0);
	if (icase_global)
		global_magic |= PATHSPEC_ICASE;

	if ((global_magic & PATHSPEC_LITERAL) &&
	    (global_magic & ~PATHSPEC_LITERAL))
		die(_("global 'literal' pathspec setting is incompatible "
		      "with all other global pathspec settings"));

	if (flags & PATHSPEC_LITERAL_PATH)
		global_magic = 0;

	if (elt[0] != ':' || literal_global ||
	    (flags & PATHSPEC_LITERAL_PATH)) {
		; /* nothing to do */
	} else if (elt[1] == '(') {
		/* longhand */
		const char *nextat;
		for (copyfrom = elt + 2;
		     *copyfrom && *copyfrom != ')';
		     copyfrom = nextat) {
			size_t len = strcspn(copyfrom, ",)");
			if (copyfrom[len] == ',')
				nextat = copyfrom + len + 1;
			else
				/* handle ')' and '\0' */
				nextat = copyfrom + len;
			if (!len)
				continue;
			for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++) {
				if (strlen(pathspec_magic[i].name) == len &&
				    !strncmp(pathspec_magic[i].name, copyfrom, len)) {
					magic |= pathspec_magic[i].bit;
					break;
				}
				if (starts_with(copyfrom, "prefix:")) {
					char *endptr;
					pathspec_prefix = strtol(copyfrom + 7,
								 &endptr, 10);
					if (endptr - copyfrom != len)
						die(_("invalid parameter for pathspec magic 'prefix'"));
					/* "i" would be wrong, but it does not matter */
					break;
				}
			}
			if (ARRAY_SIZE(pathspec_magic) <= i)
				die(_("Invalid pathspec magic '%.*s' in '%s'"),
				    (int) len, copyfrom, elt);
		}
		if (*copyfrom != ')')
			die(_("Missing ')' at the end of pathspec magic in '%s'"), elt);
		long_magic_end = copyfrom;
		copyfrom++;
	} else {
		/* shorthand */
		for (copyfrom = elt + 1;
		     *copyfrom && *copyfrom != ':';
		     copyfrom++) {
			char ch = *copyfrom;

			if (!is_pathspec_magic(ch))
				break;
			for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++)
				if (pathspec_magic[i].mnemonic == ch) {
					short_magic |= pathspec_magic[i].bit;
					break;
				}
			if (ARRAY_SIZE(pathspec_magic) <= i)
				die(_("Unimplemented pathspec magic '%c' in '%s'"),
				    ch, elt);
		}
		if (*copyfrom == ':')
			copyfrom++;
	}

	magic |= short_magic;
	*p_short_magic = short_magic;

	/* --noglob-pathspec adds :(literal) _unless_ :(glob) is specified */
	if (noglob_global && !(magic & PATHSPEC_GLOB))
		global_magic |= PATHSPEC_LITERAL;

	/* --glob-pathspec is overridden by :(literal) */
	if ((global_magic & PATHSPEC_GLOB) && (magic & PATHSPEC_LITERAL))
		global_magic &= ~PATHSPEC_GLOB;

	magic |= global_magic;

	if (pathspec_prefix >= 0 &&
	    (prefixlen || (prefix && *prefix)))
		die("BUG: 'prefix' magic is supposed to be used at worktree's root");

	if ((magic & PATHSPEC_LITERAL) && (magic & PATHSPEC_GLOB))
		die(_("%s: 'literal' and 'glob' are incompatible"), elt);

	if (pathspec_prefix >= 0) {
		match = xstrdup(copyfrom);
		prefixlen = pathspec_prefix;
	} else if (magic & PATHSPEC_FROMTOP) {
		match = xstrdup(copyfrom);
		prefixlen = 0;
	} else {
		match = prefix_path_gently(prefix, prefixlen, &prefixlen, copyfrom);
		if (!match)
			die(_("%s: '%s' is outside repository"), elt, copyfrom);
	}
	*raw = item->match = match;
	/*
	 * Prefix the pathspec (keep all magic) and assign to
	 * original. Useful for passing to another command.
	 */
	if (flags & PATHSPEC_PREFIX_ORIGIN) {
		struct strbuf sb = STRBUF_INIT;
		if (prefixlen && !literal_global) {
			/* Preserve the actual prefix length of each pattern */
			if (short_magic)
				prefix_short_magic(&sb, prefixlen, short_magic);
			else if (long_magic_end) {
				strbuf_add(&sb, elt, long_magic_end - elt);
				strbuf_addf(&sb, ",prefix:%d)", prefixlen);
			} else
				strbuf_addf(&sb, ":(prefix:%d)", prefixlen);
		}
		strbuf_addstr(&sb, match);
		item->original = strbuf_detach(&sb, NULL);
	} else
		item->original = elt;
	item->len = strlen(item->match);
	item->prefix = prefixlen;

	if ((flags & PATHSPEC_STRIP_SUBMODULE_SLASH_CHEAP) &&
	    (item->len >= 1 && item->match[item->len - 1] == '/') &&
	    (i = cache_name_pos(item->match, item->len - 1)) >= 0 &&
	    S_ISGITLINK(active_cache[i]->ce_mode)) {
		item->len--;
		match[item->len] = '\0';
	}

	if (flags & PATHSPEC_STRIP_SUBMODULE_SLASH_EXPENSIVE)
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			int ce_len = ce_namelen(ce);

			if (!S_ISGITLINK(ce->ce_mode))
				continue;

			if (item->len <= ce_len || match[ce_len] != '/' ||
			    memcmp(ce->name, match, ce_len))
				continue;
			if (item->len == ce_len + 1) {
				/* strip trailing slash */
				item->len--;
				match[item->len] = '\0';
			} else
				die (_("Pathspec '%s' is in submodule '%.*s'"),
				     elt, ce_len, ce->name);
		}

	if (magic & PATHSPEC_LITERAL)
		item->nowildcard_len = item->len;
	else {
		item->nowildcard_len = simple_length(item->match);
		if (item->nowildcard_len < prefixlen)
			item->nowildcard_len = prefixlen;
	}
	item->flags = 0;
	if (magic & PATHSPEC_GLOB) {
		/*
		 * FIXME: should we enable ONESTAR in _GLOB for
		 * pattern "* * / * . c"?
		 */
	} else {
		if (item->nowildcard_len < item->len &&
		    item->match[item->nowildcard_len] == '*' &&
		    no_wildcard(item->match + item->nowildcard_len + 1))
			item->flags |= PATHSPEC_ONESTAR;
	}

	/* sanity checks, pathspec matchers assume these are sane */
	assert(item->nowildcard_len <= item->len &&
	       item->prefix         <= item->len);
	return magic;
}

static int pathspec_item_cmp(const void *a_, const void *b_)
{
	struct pathspec_item *a, *b;

	a = (struct pathspec_item *)a_;
	b = (struct pathspec_item *)b_;
	return strcmp(a->match, b->match);
}

static void NORETURN unsupported_magic(const char *pattern,
				       unsigned magic,
				       unsigned short_magic)
{
	struct strbuf sb = STRBUF_INIT;
	int i, n;
	for (n = i = 0; i < ARRAY_SIZE(pathspec_magic); i++) {
		const struct pathspec_magic *m = pathspec_magic + i;
		if (!(magic & m->bit))
			continue;
		if (sb.len)
			strbuf_addch(&sb, ' ');
		if (short_magic & m->bit)
			strbuf_addf(&sb, "'%c'", m->mnemonic);
		else
			strbuf_addf(&sb, "'%s'", m->name);
		n++;
	}
	/*
	 * We may want to substitute "this command" with a command
	 * name. E.g. when add--interactive dies when running
	 * "checkout -p"
	 */
	die(_("%s: pathspec magic not supported by this command: %s"),
	    pattern, sb.buf);
}

/*
 * Given command line arguments and a prefix, convert the input to
 * pathspec. die() if any magic in magic_mask is used.
 */
void parse_pathspec(struct pathspec *pathspec,
		    unsigned magic_mask, unsigned flags,
		    const char *prefix, const char **argv)
{
	struct pathspec_item *item;
	const char *entry = argv ? *argv : NULL;
	int i, n, prefixlen, nr_exclude = 0;

	memset(pathspec, 0, sizeof(*pathspec));

	if (flags & PATHSPEC_MAXDEPTH_VALID)
		pathspec->magic |= PATHSPEC_MAXDEPTH;

	/* No arguments, no prefix -> no pathspec */
	if (!entry && !prefix)
		return;

	if ((flags & PATHSPEC_PREFER_CWD) &&
	    (flags & PATHSPEC_PREFER_FULL))
		die("BUG: PATHSPEC_PREFER_CWD and PATHSPEC_PREFER_FULL are incompatible");

	/* No arguments with prefix -> prefix pathspec */
	if (!entry) {
		static const char *raw[2];

		if (flags & PATHSPEC_PREFER_FULL)
			return;

		if (!(flags & PATHSPEC_PREFER_CWD))
			die("BUG: PATHSPEC_PREFER_CWD requires arguments");

		pathspec->items = item = xcalloc(1, sizeof(*item));
		item->match = prefix;
		item->original = prefix;
		item->nowildcard_len = item->len = strlen(prefix);
		item->prefix = item->len;
		raw[0] = prefix;
		raw[1] = NULL;
		pathspec->nr = 1;
		pathspec->_raw = raw;
		return;
	}

	n = 0;
	while (argv[n])
		n++;

	pathspec->nr = n;
	pathspec->items = item = xmalloc(sizeof(*item) * n);
	pathspec->_raw = argv;
	prefixlen = prefix ? strlen(prefix) : 0;

	for (i = 0; i < n; i++) {
		unsigned short_magic;
		entry = argv[i];

		item[i].magic = prefix_pathspec(item + i, &short_magic,
						argv + i, flags,
						prefix, prefixlen, entry);
		if ((flags & PATHSPEC_LITERAL_PATH) &&
		    !(magic_mask & PATHSPEC_LITERAL))
			item[i].magic |= PATHSPEC_LITERAL;
		if (item[i].magic & PATHSPEC_EXCLUDE)
			nr_exclude++;
		if (item[i].magic & magic_mask)
			unsupported_magic(entry,
					  item[i].magic & magic_mask,
					  short_magic);

		if ((flags & PATHSPEC_SYMLINK_LEADING_PATH) &&
		    has_symlink_leading_path(item[i].match, item[i].len)) {
			die(_("pathspec '%s' is beyond a symbolic link"), entry);
		}

		if (item[i].nowildcard_len < item[i].len)
			pathspec->has_wildcard = 1;
		pathspec->magic |= item[i].magic;
	}

	if (nr_exclude == n)
		die(_("There is nothing to exclude from by :(exclude) patterns.\n"
		      "Perhaps you forgot to add either ':/' or '.' ?"));


	if (pathspec->magic & PATHSPEC_MAXDEPTH) {
		if (flags & PATHSPEC_KEEP_ORDER)
			die("BUG: PATHSPEC_MAXDEPTH_VALID and PATHSPEC_KEEP_ORDER are incompatible");
		qsort(pathspec->items, pathspec->nr,
		      sizeof(struct pathspec_item), pathspec_item_cmp);
	}
}

/*
 * N.B. get_pathspec() is deprecated in favor of the "struct pathspec"
 * based interface - see pathspec.c:parse_pathspec().
 *
 * Arguments:
 *  - prefix - a path relative to the root of the working tree
 *  - pathspec - a list of paths underneath the prefix path
 *
 * Iterates over pathspec, prepending each path with prefix,
 * and return the resulting list.
 *
 * If pathspec is empty, return a singleton list containing prefix.
 *
 * If pathspec and prefix are both empty, return an empty list.
 *
 * This is typically used by built-in commands such as add.c, in order
 * to normalize argv arguments provided to the built-in into a list of
 * paths to process, all relative to the root of the working tree.
 */
const char **get_pathspec(const char *prefix, const char **pathspec)
{
	struct pathspec ps;
	parse_pathspec(&ps,
		       PATHSPEC_ALL_MAGIC &
		       ~(PATHSPEC_FROMTOP | PATHSPEC_LITERAL),
		       PATHSPEC_PREFER_CWD,
		       prefix, pathspec);
	return ps._raw;
}

void copy_pathspec(struct pathspec *dst, const struct pathspec *src)
{
	*dst = *src;
	dst->items = xmalloc(sizeof(struct pathspec_item) * dst->nr);
	memcpy(dst->items, src->items,
	       sizeof(struct pathspec_item) * dst->nr);
}

void free_pathspec(struct pathspec *pathspec)
{
	free(pathspec->items);
	pathspec->items = NULL;
}
