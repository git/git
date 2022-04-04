/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "config.h"
#include "object-store.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "quote.h"
#include "builtin.h"
#include "parse-options.h"
#include "pathspec.h"

static int line_termination = '\n';
#define LS_RECURSIVE 1
#define LS_TREE_ONLY (1 << 1)
#define LS_SHOW_TREES (1 << 2)
static int abbrev;
static int ls_options;
static struct pathspec pathspec;
static int chomp_prefix;
static const char *ls_tree_prefix;
static const char *format;
struct show_tree_data {
	unsigned mode;
	enum object_type type;
	const struct object_id *oid;
	const char *pathname;
	struct strbuf *base;
};

static const  char * const ls_tree_usage[] = {
	N_("git ls-tree [<options>] <tree-ish> [<path>...]"),
	NULL
};

static enum ls_tree_cmdmode {
	MODE_DEFAULT = 0,
	MODE_LONG,
	MODE_NAME_ONLY,
	MODE_NAME_STATUS,
	MODE_OBJECT_ONLY,
} cmdmode;

static void expand_objectsize(struct strbuf *line, const struct object_id *oid,
			      const enum object_type type, unsigned int padded)
{
	if (type == OBJ_BLOB) {
		unsigned long size;
		if (oid_object_info(the_repository, oid, &size) < 0)
			die(_("could not get object info about '%s'"),
			    oid_to_hex(oid));
		if (padded)
			strbuf_addf(line, "%7"PRIuMAX, (uintmax_t)size);
		else
			strbuf_addf(line, "%"PRIuMAX, (uintmax_t)size);
	} else if (padded) {
		strbuf_addf(line, "%7s", "-");
	} else {
		strbuf_addstr(line, "-");
	}
}

static size_t expand_show_tree(struct strbuf *sb, const char *start,
			       void *context)
{
	struct show_tree_data *data = context;
	const char *end;
	const char *p;
	unsigned int errlen;
	size_t len = strbuf_expand_literal_cb(sb, start, NULL);

	if (len)
		return len;
	if (*start != '(')
		die(_("bad ls-tree format: element '%s' does not start with '('"), start);

	end = strchr(start + 1, ')');
	if (!end)
		die(_("bad ls-tree format: element '%s' does not end in ')'"), start);

	len = end - start + 1;
	if (skip_prefix(start, "(objectmode)", &p)) {
		strbuf_addf(sb, "%06o", data->mode);
	} else if (skip_prefix(start, "(objecttype)", &p)) {
		strbuf_addstr(sb, type_name(data->type));
	} else if (skip_prefix(start, "(objectsize:padded)", &p)) {
		expand_objectsize(sb, data->oid, data->type, 1);
	} else if (skip_prefix(start, "(objectsize)", &p)) {
		expand_objectsize(sb, data->oid, data->type, 0);
	} else if (skip_prefix(start, "(objectname)", &p)) {
		strbuf_add_unique_abbrev(sb, data->oid, abbrev);
	} else if (skip_prefix(start, "(path)", &p)) {
		const char *name = data->base->buf;
		const char *prefix = chomp_prefix ? ls_tree_prefix : NULL;
		struct strbuf quoted = STRBUF_INIT;
		struct strbuf sbuf = STRBUF_INIT;
		strbuf_addstr(data->base, data->pathname);
		name = relative_path(data->base->buf, prefix, &sbuf);
		quote_c_style(name, &quoted, NULL, 0);
		strbuf_addbuf(sb, &quoted);
		strbuf_release(&sbuf);
		strbuf_release(&quoted);
	} else {
		errlen = (unsigned long)len;
		die(_("bad ls-tree format: %%%.*s"), errlen, start);
	}
	return len;
}

static int show_recursive(const char *base, size_t baselen, const char *pathname)
{
	int i;

	if (ls_options & LS_RECURSIVE)
		return 1;

	if (!pathspec.nr)
		return 0;

	for (i = 0; i < pathspec.nr; i++) {
		const char *spec = pathspec.items[i].match;
		size_t len, speclen;

		if (strncmp(base, spec, baselen))
			continue;
		len = strlen(pathname);
		spec += baselen;
		speclen = strlen(spec);
		if (speclen <= len)
			continue;
		if (spec[len] && spec[len] != '/')
			continue;
		if (memcmp(pathname, spec, len))
			continue;
		return 1;
	}
	return 0;
}

static int show_tree_fmt(const struct object_id *oid, struct strbuf *base,
			 const char *pathname, unsigned mode, void *context)
{
	size_t baselen;
	int recurse = 0;
	struct strbuf sb = STRBUF_INIT;
	enum object_type type = object_type(mode);

	struct show_tree_data data = {
		.mode = mode,
		.type = type,
		.oid = oid,
		.pathname = pathname,
		.base = base,
	};

	if (type == OBJ_TREE && show_recursive(base->buf, base->len, pathname))
		recurse = READ_TREE_RECURSIVE;
	if (type == OBJ_TREE && recurse && !(ls_options & LS_SHOW_TREES))
		return recurse;
	if (type == OBJ_BLOB && (ls_options & LS_TREE_ONLY))
		return 0;

	baselen = base->len;
	strbuf_expand(&sb, format, expand_show_tree, &data);
	strbuf_addch(&sb, line_termination);
	fwrite(sb.buf, sb.len, 1, stdout);
	strbuf_release(&sb);
	strbuf_setlen(base, baselen);
	return recurse;
}

static int show_tree_common(struct show_tree_data *data, int *recurse,
			    const struct object_id *oid, struct strbuf *base,
			    const char *pathname, unsigned mode)
{
	enum object_type type = object_type(mode);
	int ret = -1;

	*recurse = 0;
	data->mode = mode;
	data->type = type;
	data->oid = oid;
	data->pathname = pathname;
	data->base = base;

	if (type == OBJ_BLOB) {
		if (ls_options & LS_TREE_ONLY)
			ret = 0;
	} else if (type == OBJ_TREE &&
		   show_recursive(base->buf, base->len, pathname)) {
		*recurse = READ_TREE_RECURSIVE;
		if (!(ls_options & LS_SHOW_TREES))
			ret = *recurse;
	}

	return ret;
}

static void show_tree_common_default_long(struct strbuf *base,
					  const char *pathname,
					  const size_t baselen)
{
	strbuf_addstr(base, pathname);
	write_name_quoted_relative(base->buf,
				   chomp_prefix ? ls_tree_prefix : NULL, stdout,
				   line_termination);
	strbuf_setlen(base, baselen);
}

static int show_tree_default(const struct object_id *oid, struct strbuf *base,
			     const char *pathname, unsigned mode,
			     void *context)
{
	int early;
	int recurse;
	struct show_tree_data data = { 0 };

	early = show_tree_common(&data, &recurse, oid, base, pathname, mode);
	if (early >= 0)
		return early;

	printf("%06o %s %s\t", data.mode, type_name(data.type),
	       find_unique_abbrev(data.oid, abbrev));
	show_tree_common_default_long(base, pathname, data.base->len);
	return recurse;
}

static int show_tree_long(const struct object_id *oid, struct strbuf *base,
			  const char *pathname, unsigned mode, void *context)
{
	int early;
	int recurse;
	struct show_tree_data data = { 0 };
	char size_text[24];

	early = show_tree_common(&data, &recurse, oid, base, pathname, mode);
	if (early >= 0)
		return early;

	if (data.type == OBJ_BLOB) {
		unsigned long size;
		if (oid_object_info(the_repository, data.oid, &size) == OBJ_BAD)
			xsnprintf(size_text, sizeof(size_text), "BAD");
		else
			xsnprintf(size_text, sizeof(size_text),
				  "%" PRIuMAX, (uintmax_t)size);
	} else {
		xsnprintf(size_text, sizeof(size_text), "-");
	}

	printf("%06o %s %s %7s\t", data.mode, type_name(data.type),
	       find_unique_abbrev(data.oid, abbrev), size_text);
	show_tree_common_default_long(base, pathname, data.base->len);
	return 1;
}

static int show_tree_name_only(const struct object_id *oid, struct strbuf *base,
			       const char *pathname, unsigned mode, void *context)
{
	int early;
	int recurse;
	const size_t baselen = base->len;
	struct show_tree_data data = { 0 };

	early = show_tree_common(&data, &recurse, oid, base, pathname, mode);
	if (early >= 0)
		return early;

	strbuf_addstr(base, pathname);
	write_name_quoted_relative(base->buf,
				   chomp_prefix ? ls_tree_prefix : NULL,
				   stdout, line_termination);
	strbuf_setlen(base, baselen);
	return recurse;
}

static int show_tree_object(const struct object_id *oid, struct strbuf *base,
			    const char *pathname, unsigned mode, void *context)
{
	int early;
	int recurse;
	struct show_tree_data data = { 0 };

	early = show_tree_common(&data, &recurse, oid, base, pathname, mode);
	if (early >= 0)
		return early;

	printf("%s%c", find_unique_abbrev(oid, abbrev), line_termination);
	return recurse;
}

struct ls_tree_cmdmode_to_fmt {
	enum ls_tree_cmdmode mode;
	const char *const fmt;
	read_tree_fn_t fn;
};

static struct ls_tree_cmdmode_to_fmt ls_tree_cmdmode_format[] = {
	{
		.mode = MODE_DEFAULT,
		.fmt = "%(objectmode) %(objecttype) %(objectname)%x09%(path)",
		.fn = show_tree_default,
	},
	{
		.mode = MODE_LONG,
		.fmt = "%(objectmode) %(objecttype) %(objectname) %(objectsize:padded)%x09%(path)",
		.fn = show_tree_long,
	},
	{
		.mode = MODE_NAME_ONLY, /* And MODE_NAME_STATUS */
		.fmt = "%(path)",
		.fn = show_tree_name_only,
	},
	{
		.mode = MODE_OBJECT_ONLY,
		.fmt = "%(objectname)",
		.fn = show_tree_object
	},
	{
		/* fallback */
		.fn = show_tree_default,
	},
};

int cmd_ls_tree(int argc, const char **argv, const char *prefix)
{
	struct object_id oid;
	struct tree *tree;
	int i, full_tree = 0;
	read_tree_fn_t fn = NULL;
	const struct option ls_tree_options[] = {
		OPT_BIT('d', NULL, &ls_options, N_("only show trees"),
			LS_TREE_ONLY),
		OPT_BIT('r', NULL, &ls_options, N_("recurse into subtrees"),
			LS_RECURSIVE),
		OPT_BIT('t', NULL, &ls_options, N_("show trees when recursing"),
			LS_SHOW_TREES),
		OPT_SET_INT('z', NULL, &line_termination,
			    N_("terminate entries with NUL byte"), 0),
		OPT_CMDMODE('l', "long", &cmdmode, N_("include object size"),
			    MODE_LONG),
		OPT_CMDMODE(0, "name-only", &cmdmode, N_("list only filenames"),
			    MODE_NAME_ONLY),
		OPT_CMDMODE(0, "name-status", &cmdmode, N_("list only filenames"),
			    MODE_NAME_STATUS),
		OPT_CMDMODE(0, "object-only", &cmdmode, N_("list only objects"),
			    MODE_OBJECT_ONLY),
		OPT_SET_INT(0, "full-name", &chomp_prefix,
			    N_("use full path names"), 0),
		OPT_BOOL(0, "full-tree", &full_tree,
			 N_("list entire tree; not just current directory "
			    "(implies --full-name)")),
		OPT_STRING_F(0, "format", &format, N_("format"),
					 N_("format to use for the output"),
					 PARSE_OPT_NONEG),
		OPT__ABBREV(&abbrev),
		OPT_END()
	};
	struct ls_tree_cmdmode_to_fmt *m2f = ls_tree_cmdmode_format;

	git_config(git_default_config, NULL);
	ls_tree_prefix = prefix;
	if (prefix)
		chomp_prefix = strlen(prefix);

	argc = parse_options(argc, argv, prefix, ls_tree_options,
			     ls_tree_usage, 0);
	if (full_tree) {
		ls_tree_prefix = prefix = NULL;
		chomp_prefix = 0;
	}
	/*
	 * We wanted to detect conflicts between --name-only and
	 * --name-status, but once we're done with that subsequent
	 * code should only need to check the primary name.
	 */
	if (cmdmode == MODE_NAME_STATUS)
		cmdmode = MODE_NAME_ONLY;

	/* -d -r should imply -t, but -d by itself should not have to. */
	if ( (LS_TREE_ONLY|LS_RECURSIVE) ==
	    ((LS_TREE_ONLY|LS_RECURSIVE) & ls_options))
		ls_options |= LS_SHOW_TREES;

	if (format && cmdmode)
		usage_msg_opt(
			_("--format can't be combined with other format-altering options"),
			ls_tree_usage, ls_tree_options);
	if (argc < 1)
		usage_with_options(ls_tree_usage, ls_tree_options);
	if (get_oid(argv[0], &oid))
		die("Not a valid object name %s", argv[0]);

	/*
	 * show_recursive() rolls its own matching code and is
	 * generally ignorant of 'struct pathspec'. The magic mask
	 * cannot be lifted until it is converted to use
	 * match_pathspec() or tree_entry_interesting()
	 */
	parse_pathspec(&pathspec, PATHSPEC_ALL_MAGIC &
				  ~(PATHSPEC_FROMTOP | PATHSPEC_LITERAL),
		       PATHSPEC_PREFER_CWD,
		       prefix, argv + 1);
	for (i = 0; i < pathspec.nr; i++)
		pathspec.items[i].nowildcard_len = pathspec.items[i].len;
	pathspec.has_wildcard = 0;
	tree = parse_tree_indirect(&oid);
	if (!tree)
		die("not a tree object");
	/*
	 * The generic show_tree_fmt() is slower than show_tree(), so
	 * take the fast path if possible.
	 */
	while (m2f) {
		if (!m2f->fmt) {
			fn = format ? show_tree_fmt : show_tree_default;
		} else if (format && !strcmp(format, m2f->fmt)) {
			cmdmode = m2f->mode;
			fn = m2f->fn;
		} else if (!format && cmdmode == m2f->mode) {
			fn = m2f->fn;
		} else {
			m2f++;
			continue;
		}
		break;
	}

	return !!read_tree(the_repository, tree, &pathspec, fn, NULL);
}
