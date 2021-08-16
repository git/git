#include "cache.h"
#include "config.h"
#include "dir.h"
#include "pathspec.h"
#include "attr.h"
#include "strvec.h"
#include "quote.h"

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
					struct index_state *istate,
					char *seen,
					enum ps_skip_worktree_action sw_action)
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
	for (i = 0; i < istate->cache_nr; i++) {
		const struct cache_entry *ce = istate->cache[i];
		if (sw_action == PS_IGNORE_SKIP_WORKTREE && ce_skip_worktree(ce))
			continue;
		ce_path_match(istate, ce, pathspec, seen);
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
char *find_pathspecs_matching_against_index(const struct pathspec *pathspec,
					    struct index_state *istate,
					    enum ps_skip_worktree_action sw_action)
{
	char *seen = xcalloc(pathspec->nr, 1);
	add_pathspec_matches_against_index(pathspec, istate, seen, sw_action);
	return seen;
}

char *find_pathspecs_matching_skip_worktree(const struct pathspec *pathspec)
{
	struct index_state *istate = the_repository->index;
	char *seen = xcalloc(pathspec->nr, 1);
	int i;

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		if (ce_skip_worktree(ce))
		    ce_path_match(istate, ce, pathspec, seen);
	}

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
	{ PATHSPEC_FROMTOP,  '/', "top" },
	{ PATHSPEC_LITERAL, '\0', "literal" },
	{ PATHSPEC_GLOB,    '\0', "glob" },
	{ PATHSPEC_ICASE,   '\0', "icase" },
	{ PATHSPEC_EXCLUDE,  '!', "exclude" },
	{ PATHSPEC_ATTR,    '\0', "attr" },
};

static void prefix_magic(struct strbuf *sb, int prefixlen, unsigned magic)
{
	int i;
	strbuf_addstr(sb, ":(");
	for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++)
		if (magic & pathspec_magic[i].bit) {
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

	if (item->attr_check || item->attr_match)
		die(_("Only one 'attr:' specification is allowed."));

	if (!value || !*value)
		die(_("attr spec must not be empty"));

	string_list_split(&list, value, ' ', -1);
	string_list_remove_empty_items(&list, 0);

	item->attr_check = attr_check_alloc();
	CALLOC_ARRAY(item->attr_match, list.nr);

	for_each_string_list_item(si, &list) {
		size_t attr_len;
		char *attr_name;
		const struct git_attr *a;

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

		attr_name = xmemdupz(attr, attr_len);
		a = git_attr(attr_name);
		if (!a)
			die(_("invalid attribute name %s"), attr_name);

		attr_check_append(item->attr_check, a);

		free(attr_name);
	}

	if (item->attr_check->nr != item->attr_match_nr)
		BUG("should have same number of entries");

	string_list_clear(&list, 0);
}

static inline int get_literal_global(void)
{
	static int literal = -1;

	if (literal < 0)
		literal = git_env_bool(GIT_LITERAL_PATHSPECS_ENVIRONMENT, 0);

	return literal;
}

static inline int get_glob_global(void)
{
	static int glob = -1;

	if (glob < 0)
		glob = git_env_bool(GIT_GLOB_PATHSPECS_ENVIRONMENT, 0);

	return glob;
}

static inline int get_noglob_global(void)
{
	static int noglob = -1;

	if (noglob < 0)
		noglob = git_env_bool(GIT_NOGLOB_PATHSPECS_ENVIRONMENT, 0);

	return noglob;
}

static inline int get_icase_global(void)
{
	static int icase = -1;

	if (icase < 0)
		icase = git_env_bool(GIT_ICASE_PATHSPECS_ENVIRONMENT, 0);

	return icase;
}

static int get_global_magic(int element_magic)
{
	int global_magic = 0;

	if (get_literal_global())
		global_magic |= PATHSPEC_LITERAL;

	/* --glob-pathspec is overridden by :(literal) */
	if (get_glob_global() && !(element_magic & PATHSPEC_LITERAL))
		global_magic |= PATHSPEC_GLOB;

	if (get_glob_global() && get_noglob_global())
		die(_("global 'glob' and 'noglob' pathspec settings are incompatible"));

	if (get_icase_global())
		global_magic |= PATHSPEC_ICASE;

	if ((global_magic & PATHSPEC_LITERAL) &&
	    (global_magic & ~PATHSPEC_LITERAL))
		die(_("global 'literal' pathspec setting is incompatible "
		      "with all other global pathspec settings"));

	/* --noglob-pathspec adds :(literal) _unless_ :(glob) is specified */
	if (get_noglob_global() && !(element_magic & PATHSPEC_GLOB))
		global_magic |= PATHSPEC_LITERAL;

	return global_magic;
}

/*
 * Parse the pathspec element looking for long magic
 *
 * saves all magic in 'magic'
 * if prefix magic is used, save the prefix length in 'prefix_len'
 * returns the position in 'elem' after all magic has been parsed
 */
static const char *parse_long_magic(unsigned *magic, int *prefix_len,
				    struct pathspec_item *item,
				    const char *elem)
{
	const char *pos;
	const char *nextat;

	for (pos = elem + 2; *pos && *pos != ')'; pos = nextat) {
		size_t len = strcspn_escaped(pos, ",)");
		int i;

		if (pos[len] == ',')
			nextat = pos + len + 1; /* handle ',' */
		else
			nextat = pos + len; /* handle ')' and '\0' */

		if (!len)
			continue;

		if (starts_with(pos, "prefix:")) {
			char *endptr;
			*prefix_len = strtol(pos + 7, &endptr, 10);
			if (endptr - pos != len)
				die(_("invalid parameter for pathspec magic 'prefix'"));
			continue;
		}

		if (starts_with(pos, "attr:")) {
			char *attr_body = xmemdupz(pos + 5, len - 5);
			parse_pathspec_attr_match(item, attr_body);
			*magic |= PATHSPEC_ATTR;
			free(attr_body);
			continue;
		}

		for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++) {
			if (strlen(pathspec_magic[i].name) == len &&
			    !strncmp(pathspec_magic[i].name, pos, len)) {
				*magic |= pathspec_magic[i].bit;
				break;
			}
		}

		if (ARRAY_SIZE(pathspec_magic) <= i)
			die(_("Invalid pathspec magic '%.*s' in '%s'"),
			    (int) len, pos, elem);
	}

	if (*pos != ')')
		die(_("Missing ')' at the end of pathspec magic in '%s'"),
		    elem);
	pos++;

	return pos;
}

/*
 * Parse the pathspec element looking for short magic
 *
 * saves all magic in 'magic'
 * returns the position in 'elem' after all magic has been parsed
 */
static const char *parse_short_magic(unsigned *magic, const char *elem)
{
	const char *pos;

	for (pos = elem + 1; *pos && *pos != ':'; pos++) {
		char ch = *pos;
		int i;

		/* Special case alias for '!' */
		if (ch == '^') {
			*magic |= PATHSPEC_EXCLUDE;
			continue;
		}

		if (!is_pathspec_magic(ch))
			break;

		for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++) {
			if (pathspec_magic[i].mnemonic == ch) {
				*magic |= pathspec_magic[i].bit;
				break;
			}
		}

		if (ARRAY_SIZE(pathspec_magic) <= i)
			die(_("Unimplemented pathspec magic '%c' in '%s'"),
			    ch, elem);
	}

	if (*pos == ':')
		pos++;

	return pos;
}

static const char *parse_element_magic(unsigned *magic, int *prefix_len,
				       struct pathspec_item *item,
				       const char *elem)
{
	if (elem[0] != ':' || get_literal_global())
		return elem; /* nothing to do */
	else if (elem[1] == '(')
		/* longhand */
		return parse_long_magic(magic, prefix_len, item, elem);
	else
		/* shorthand */
		return parse_short_magic(magic, elem);
}

/*
 * Perform the initialization of a pathspec_item based on a pathspec element.
 */
static void init_pathspec_item(struct pathspec_item *item, unsigned flags,
			       const char *prefix, int prefixlen,
			       const char *elt)
{
	unsigned magic = 0, element_magic = 0;
	const char *copyfrom = elt;
	char *match;
	int pathspec_prefix = -1;

	item->attr_check = NULL;
	item->attr_match = NULL;
	item->attr_match_nr = 0;

	/* PATHSPEC_LITERAL_PATH ignores magic */
	if (flags & PATHSPEC_LITERAL_PATH) {
		magic = PATHSPEC_LITERAL;
	} else {
		copyfrom = parse_element_magic(&element_magic,
					       &pathspec_prefix,
					       item,
					       elt);
		magic |= element_magic;
		magic |= get_global_magic(element_magic);
	}

	item->magic = magic;

	if (pathspec_prefix >= 0 &&
	    (prefixlen || (prefix && *prefix)))
		BUG("'prefix' magic is supposed to be used at worktree's root");

	if ((magic & PATHSPEC_LITERAL) && (magic & PATHSPEC_GLOB))
		die(_("%s: 'literal' and 'glob' are incompatible"), elt);

	/* Create match string which will be used for pathspec matching */
	if (pathspec_prefix >= 0) {
		match = xstrdup(copyfrom);
		prefixlen = pathspec_prefix;
	} else if (magic & PATHSPEC_FROMTOP) {
		match = xstrdup(copyfrom);
		prefixlen = 0;
	} else {
		match = prefix_path_gently(prefix, prefixlen,
					   &prefixlen, copyfrom);
		if (!match) {
			const char *hint_path = get_git_work_tree();
			if (!hint_path)
				hint_path = get_git_dir();
			die(_("%s: '%s' is outside repository at '%s'"), elt,
			    copyfrom, absolute_path(hint_path));
		}
	}

	item->match = match;
	item->len = strlen(item->match);
	item->prefix = prefixlen;

	/*
	 * Prefix the pathspec (keep all magic) and assign to
	 * original. Useful for passing to another command.
	 */
	if ((flags & PATHSPEC_PREFIX_ORIGIN) &&
	    !get_literal_global()) {
		struct strbuf sb = STRBUF_INIT;

		/* Preserve the actual prefix length of each pattern */
		prefix_magic(&sb, prefixlen, element_magic);

		strbuf_addstr(&sb, match);
		item->original = strbuf_detach(&sb, NULL);
	} else {
		item->original = xstrdup(elt);
	}

	if (magic & PATHSPEC_LITERAL) {
		item->nowildcard_len = item->len;
	} else {
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
	if (item->nowildcard_len > item->len ||
	    item->prefix         > item->len) {
		BUG("error initializing pathspec_item");
	}
}

static int pathspec_item_cmp(const void *a_, const void *b_)
{
	struct pathspec_item *a, *b;

	a = (struct pathspec_item *)a_;
	b = (struct pathspec_item *)b_;
	return strcmp(a->match, b->match);
}

static void NORETURN unsupported_magic(const char *pattern,
				       unsigned magic)
{
	struct strbuf sb = STRBUF_INIT;
	int i;
	for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++) {
		const struct pathspec_magic *m = pathspec_magic + i;
		if (!(magic & m->bit))
			continue;
		if (sb.len)
			strbuf_addstr(&sb, ", ");

		if (m->mnemonic)
			strbuf_addf(&sb, _("'%s' (mnemonic: '%c')"),
				    m->name, m->mnemonic);
		else
			strbuf_addf(&sb, "'%s'", m->name);
	}
	/*
	 * We may want to substitute "this command" with a command
	 * name. E.g. when add--interactive dies when running
	 * "checkout -p"
	 */
	die(_("%s: pathspec magic not supported by this command: %s"),
	    pattern, sb.buf);
}

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
		BUG("PATHSPEC_PREFER_CWD and PATHSPEC_PREFER_FULL are incompatible");

	/* No arguments with prefix -> prefix pathspec */
	if (!entry) {
		if (flags & PATHSPEC_PREFER_FULL)
			return;

		if (!(flags & PATHSPEC_PREFER_CWD))
			BUG("PATHSPEC_PREFER_CWD requires arguments");

		pathspec->items = CALLOC_ARRAY(item, 1);
		item->match = xstrdup(prefix);
		item->original = xstrdup(prefix);
		item->nowildcard_len = item->len = strlen(prefix);
		item->prefix = item->len;
		pathspec->nr = 1;
		return;
	}

	n = 0;
	while (argv[n]) {
		if (*argv[n] == '\0')
			die("empty string is not a valid pathspec. "
				  "please use . instead if you meant to match all paths");
		n++;
	}

	pathspec->nr = n;
	ALLOC_ARRAY(pathspec->items, n + 1);
	item = pathspec->items;
	prefixlen = prefix ? strlen(prefix) : 0;

	for (i = 0; i < n; i++) {
		entry = argv[i];

		init_pathspec_item(item + i, flags, prefix, prefixlen, entry);

		if (item[i].magic & PATHSPEC_EXCLUDE)
			nr_exclude++;
		if (item[i].magic & magic_mask)
			unsupported_magic(entry, item[i].magic & magic_mask);

		if ((flags & PATHSPEC_SYMLINK_LEADING_PATH) &&
		    has_symlink_leading_path(item[i].match, item[i].len)) {
			die(_("pathspec '%s' is beyond a symbolic link"), entry);
		}

		if (item[i].nowildcard_len < item[i].len)
			pathspec->has_wildcard = 1;
		pathspec->magic |= item[i].magic;
	}

	/*
	 * If everything is an exclude pattern, add one positive pattern
	 * that matches everything. We allocated an extra one for this.
	 */
	if (nr_exclude == n) {
		int plen = (!(flags & PATHSPEC_PREFER_CWD)) ? 0 : prefixlen;
		init_pathspec_item(item + n, 0, prefix, plen, "");
		pathspec->nr++;
	}

	if (pathspec->magic & PATHSPEC_MAXDEPTH) {
		if (flags & PATHSPEC_KEEP_ORDER)
			BUG("PATHSPEC_MAXDEPTH_VALID and PATHSPEC_KEEP_ORDER are incompatible");
		QSORT(pathspec->items, pathspec->nr, pathspec_item_cmp);
	}
}

void parse_pathspec_file(struct pathspec *pathspec, unsigned magic_mask,
			 unsigned flags, const char *prefix,
			 const char *file, int nul_term_line)
{
	struct strvec parsed_file = STRVEC_INIT;
	strbuf_getline_fn getline_fn = nul_term_line ? strbuf_getline_nul :
						       strbuf_getline;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf unquoted = STRBUF_INIT;
	FILE *in;

	if (!strcmp(file, "-"))
		in = stdin;
	else
		in = xfopen(file, "r");

	while (getline_fn(&buf, in) != EOF) {
		if (!nul_term_line && buf.buf[0] == '"') {
			strbuf_reset(&unquoted);
			if (unquote_c_style(&unquoted, buf.buf, NULL))
				die(_("line is badly quoted: %s"), buf.buf);
			strbuf_swap(&buf, &unquoted);
		}
		strvec_push(&parsed_file, buf.buf);
		strbuf_reset(&buf);
	}

	strbuf_release(&unquoted);
	strbuf_release(&buf);
	if (in != stdin)
		fclose(in);

	parse_pathspec(pathspec, magic_mask, flags, prefix, parsed_file.v);
	strvec_clear(&parsed_file);
}

void copy_pathspec(struct pathspec *dst, const struct pathspec *src)
{
	int i, j;

	*dst = *src;
	ALLOC_ARRAY(dst->items, dst->nr);
	COPY_ARRAY(dst->items, src->items, dst->nr);

	for (i = 0; i < dst->nr; i++) {
		struct pathspec_item *d = &dst->items[i];
		struct pathspec_item *s = &src->items[i];

		d->match = xstrdup(s->match);
		d->original = xstrdup(s->original);

		ALLOC_ARRAY(d->attr_match, d->attr_match_nr);
		COPY_ARRAY(d->attr_match, s->attr_match, d->attr_match_nr);
		for (j = 0; j < d->attr_match_nr; j++) {
			const char *value = s->attr_match[j].value;
			d->attr_match[j].value = xstrdup_or_null(value);
		}

		d->attr_check = attr_check_dup(s->attr_check);
	}
}

void clear_pathspec(struct pathspec *pathspec)
{
	int i, j;

	for (i = 0; i < pathspec->nr; i++) {
		free(pathspec->items[i].match);
		free(pathspec->items[i].original);

		for (j = 0; j < pathspec->items[i].attr_match_nr; j++)
			free(pathspec->items[i].attr_match[j].value);
		free(pathspec->items[i].attr_match);

		if (pathspec->items[i].attr_check)
			attr_check_free(pathspec->items[i].attr_check);
	}

	FREE_AND_NULL(pathspec->items);
	pathspec->nr = 0;
}

int match_pathspec_attrs(struct index_state *istate,
			 const char *name, int namelen,
			 const struct pathspec_item *item)
{
	int i;
	char *to_free = NULL;

	if (name[namelen])
		name = to_free = xmemdupz(name, namelen);

	git_check_attr(istate, name, item->attr_check);

	free(to_free);

	for (i = 0; i < item->attr_match_nr; i++) {
		const char *value;
		int matched;
		enum attr_match_mode match_mode;

		value = item->attr_check->items[i].value;
		match_mode = item->attr_match[i].match_mode;

		if (ATTR_TRUE(value))
			matched = (match_mode == MATCH_SET);
		else if (ATTR_FALSE(value))
			matched = (match_mode == MATCH_UNSET);
		else if (ATTR_UNSET(value))
			matched = (match_mode == MATCH_UNSPECIFIED);
		else
			matched = (match_mode == MATCH_VALUE &&
				   !strcmp(item->attr_match[i].value, value));
		if (!matched)
			return 0;
	}

	return 1;
}
