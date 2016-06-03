#include "cache.h"
#include "dir.h"
#include "pathspec.h"
#include "attr.h"

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

static size_t strcspn_escaped(const char *s, const char *stop)
{
	const char *i;

	for (i = s; *i; i++) {
		/* skip the escaped character */
		if (i[0] == '\\' && i[1]) {
			i++;
			continue;
		}

		if (strchr(stop, *i))
			break;
	}
	return i - s;
}

static inline int invalid_value_char(const char ch)
{
	if (isalnum(ch) || strchr(",-_", ch))
		return 0;
	return -1;
}

static char *attr_value_unescape(const char *value)
{
	const char *src;
	char *dst, *ret;

	ret = xmallocz(strlen(value));
	for (src = value, dst = ret; *src; src++, dst++) {
		if (*src == '\\') {
			if (!src[1])
				die(_("Escape character '\\' not allowed as "
				      "last character in attr value"));
			src++;
		}
		if (invalid_value_char(*src))
			die("cannot use '%c' for value matching", *src);
		*dst = *src;
	}
	*dst = '\0';
	return ret;
}

static void parse_pathspec_attr_match(struct pathspec_item *item, const char *value)
{
	struct string_list_item *si;
	struct string_list list = STRING_LIST_INIT_DUP;

	if (!value || !strlen(value))
		die(_("attr spec must not be empty"));

	string_list_split(&list, value, ' ', -1);
	string_list_remove_empty_items(&list, 0);

	if (!item->attr_check)
		item->attr_check = git_attr_check_alloc();
	else
		die(_("Only one 'attr:' specification is allowed."));

	ALLOC_GROW(item->attr_match, item->attr_match_nr + list.nr, item->attr_match_alloc);

	for_each_string_list_item(si, &list) {
		size_t attr_len;

		int j = item->attr_match_nr++;
		const char *attr = si->string;
		struct attr_match *am = &item->attr_match[j];

		switch (*attr) {
		case '!':
			am->match_mode = MATCH_UNSPECIFIED;
			attr++;
			attr_len = strlen(attr);
			break;
		case '-':
			am->match_mode = MATCH_UNSET;
			attr++;
			attr_len = strlen(attr);
			break;
		default:
			attr_len = strcspn(attr, "=");
			if (attr[attr_len] != '=')
				am->match_mode = MATCH_SET;
			else {
				const char *v = &attr[attr_len + 1];
				am->match_mode = MATCH_VALUE;
				am->value = attr_value_unescape(v);
			}
			break;
		}

		am->attr = git_attr_counted(attr, attr_len);
		if (!am->attr) {
			struct strbuf sb = STRBUF_INIT;
			am->match_mode = INVALID_ATTR;
			invalid_attr_name_message(&sb, attr, attr_len);
			die(_("invalid attribute in '%s': '%s'"), value, sb.buf);
		}

		git_attr_check_append(item->attr_check, am->attr);
	}

	string_list_clear(&list, 0);
	return;
}

static void eat_long_magic(struct pathspec_item *item, const char *elt,
		unsigned *magic, int *pathspec_prefix,
		const char **copyfrom_, const char **long_magic_end)
{
	int i;
	const char *copyfrom = *copyfrom_;
	const char *body;
	/* longhand */
	const char *nextat;
	for (copyfrom = elt + 2;
	     *copyfrom && *copyfrom != ')';
	     copyfrom = nextat) {
		size_t len = strcspn_escaped(copyfrom, ",)");
		if (copyfrom[len] == ',')
			nextat = copyfrom + len + 1;
		else
			/* handle ')' and '\0' */
			nextat = copyfrom + len;
		if (!len)
			continue;

		if (skip_prefix(copyfrom, "prefix:", &body)) {
			char *endptr;
			*pathspec_prefix = strtol(body, &endptr, 10);
			if (endptr - copyfrom != len)
				die(_("invalid parameter for pathspec magic 'prefix'"));
			continue;
		}

		if (skip_prefix(copyfrom, "attr:", &body)) {
			char *attr_body = xmemdupz(body, len - strlen("attr:"));
			parse_pathspec_attr_match(item, attr_body);
			free(attr_body);
			continue;
		}

		for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++) {
			if (strlen(pathspec_magic[i].name) == len &&
			    !strncmp(pathspec_magic[i].name, copyfrom, len)) {
				*magic |= pathspec_magic[i].bit;
				break;
			}
		}
		if (ARRAY_SIZE(pathspec_magic) <= i)
			die(_("Invalid pathspec magic '%.*s' in '%s'"),
			    (int) len, copyfrom, elt);
	}
	if (*copyfrom != ')')
		die(_("Missing ')' at the end of pathspec magic in '%s'"), elt);
	*long_magic_end = copyfrom;
	copyfrom++;
	*copyfrom_ = copyfrom;
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
		eat_long_magic(item, elt, &magic, &pathspec_prefix, &copyfrom, &long_magic_end);
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
	ALLOC_ARRAY(pathspec->items, n);
	item = pathspec->items;
	pathspec->_raw = argv;
	prefixlen = prefix ? strlen(prefix) : 0;

	for (i = 0; i < n; i++) {
		unsigned short_magic;
		entry = argv[i];
		item[i].attr_check = NULL;
		item[i].attr_match = NULL;
		item[i].attr_match_nr = 0;
		item[i].attr_match_alloc = 0;
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

		if (item[i].attr_match_nr) {
			int j;
			for (j = 0; j < item[i].attr_match_nr; j++)
				if (item[i].attr_match[j].match_mode == INVALID_ATTR)
					die(_("attribute spec in the wrong syntax are prohibited."));
		}
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
	ALLOC_ARRAY(dst->items, dst->nr);
	memcpy(dst->items, src->items,
	       sizeof(struct pathspec_item) * dst->nr);
}

void free_pathspec(struct pathspec *pathspec)
{
	int i, j;
	for (i = 0; i < pathspec->nr; i++) {
		if (!pathspec->items[i].attr_match_nr)
			continue;
		for (j = 0; j < pathspec->items[j].attr_match_nr; j++)
			free(pathspec->items[i].attr_match[j].value);
		free(pathspec->items[i].attr_match);
		git_attr_check_free(pathspec->items[i].attr_check);
	}

	free(pathspec->items);
	pathspec->items = NULL;
}
