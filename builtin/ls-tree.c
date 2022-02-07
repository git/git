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
#define LS_NAME_ONLY (1 << 3)
#define LS_SHOW_SIZE (1 << 4)
#define LS_OBJECT_ONLY (1 << 5)
static int abbrev;
static int ls_options;
static struct pathspec pathspec;
static int chomp_prefix;
static const char *ls_tree_prefix;
static unsigned int shown_fields;
#define FIELD_FILE_NAME 1
#define FIELD_SIZE (1 << 1)
#define FIELD_OBJECT_NAME (1 << 2)
#define FIELD_TYPE (1 << 3)
#define FIELD_MODE (1 << 4)
#define FIELD_DEFAULT 29 /* 11101 size is not shown to output by default */
#define FIELD_LONG_DEFAULT  (FIELD_DEFAULT | FIELD_SIZE)

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

enum {
	MODE_UNSPECIFIED = 0,
	MODE_NAME_ONLY,
	MODE_OBJECT_ONLY,
	MODE_LONG,
};

static int cmdmode = MODE_UNSPECIFIED;

static const char *format;
static const char *default_format = "%(mode) %(type) %(object)%x09%(file)";
static const char *long_format = "%(mode) %(type) %(object) %(size:padded)%x09%(file)";
static const char *name_only_format = "%(file)";
static const char *object_only_format = "%(object)";

static int parse_shown_fields(void)
{
	if (cmdmode == MODE_NAME_ONLY) {
		shown_fields = FIELD_FILE_NAME;
		return 0;
	}
	if (cmdmode == MODE_OBJECT_ONLY) {
		shown_fields = FIELD_OBJECT_NAME;
		return 0;
	}
	if (!ls_options || (ls_options & LS_RECURSIVE)
	    || (ls_options & LS_SHOW_TREES)
	    || (ls_options & LS_TREE_ONLY))
		shown_fields = FIELD_DEFAULT;
	if (cmdmode == MODE_LONG)
		shown_fields = FIELD_LONG_DEFAULT;
	return 1;
}

static void expand_objectsize(struct strbuf *line, const struct object_id *oid,
			      const enum object_type type, unsigned int padded)
{
	if (type == OBJ_BLOB) {
		unsigned long size;
		if (oid_object_info(the_repository, oid, &size) < 0)
			die(_("could not get object info about '%s'"),
			    oid_to_hex(oid));
		if (padded)
			strbuf_addf(line, "%7" PRIuMAX, (uintmax_t)size);
		else
			strbuf_addf(line, "%" PRIuMAX, (uintmax_t)size);
	} else if (padded) {
		strbuf_addf(line, "%7s", "-");
	} else {
		strbuf_addstr(line, "-");
	}
}

static size_t expand_show_tree(struct strbuf *line, const char *start,
			       void *context)
{
	struct show_tree_data *data = context;
	const char *end;
	const char *p;
	unsigned int errlen;
	size_t len = strbuf_expand_literal_cb(line, start, NULL);

	if (len)
		return len;
	if (*start != '(')
		die(_("bad ls-tree format: as '%s'"), start);

	end = strchr(start + 1, ')');
	if (!end)
		die(_("bad ls-tree format: element '%s' does not end in ')'"), start);

	len = end - start + 1;
	if (skip_prefix(start, "(mode)", &p)) {
		strbuf_addf(line, "%06o", data->mode);
	} else if (skip_prefix(start, "(type)", &p)) {
		strbuf_addstr(line, type_name(data->type));
	} else if (skip_prefix(start, "(size:padded)", &p)) {
		expand_objectsize(line, data->oid, data->type, 1);
	} else if (skip_prefix(start, "(size)", &p)) {
		expand_objectsize(line, data->oid, data->type, 0);
	} else if (skip_prefix(start, "(object)", &p)) {
		strbuf_add_unique_abbrev(line, data->oid, abbrev);
	} else if (skip_prefix(start, "(file)", &p)) {
		const char *name = data->base->buf;
		const char *prefix = chomp_prefix ? ls_tree_prefix : NULL;
		struct strbuf quoted = STRBUF_INIT;
		struct strbuf sb = STRBUF_INIT;
		strbuf_addstr(data->base, data->pathname);
		name = relative_path(data->base->buf, prefix, &sb);
		quote_c_style(name, &quoted, NULL, 0);
		strbuf_addbuf(line, &quoted);
		strbuf_release(&sb);
		strbuf_release(&quoted);
	} else {
		errlen = (unsigned long)len;
		die(_("bad ls-tree format: %%%.*s"), errlen, start);
	}
	return len;
}

static int show_recursive(const char *base, size_t baselen,
			  const char *pathname)
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

static enum object_type get_type(unsigned int mode)
{
	return (S_ISGITLINK(mode)
		? OBJ_COMMIT
		: S_ISDIR(mode)
		? OBJ_TREE
		: OBJ_BLOB);
}

static int show_tree_fmt(const struct object_id *oid, struct strbuf *base,
			 const char *pathname, unsigned mode, void *context)
{
	size_t baselen;
	int recurse = 0;
	struct strbuf line = STRBUF_INIT;
	enum object_type type = get_type(mode);

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
	strbuf_expand(&line, format, expand_show_tree, &data);
	strbuf_addch(&line, line_termination);
	fwrite(line.buf, line.len, 1, stdout);
	strbuf_release(&line);
	strbuf_setlen(base, baselen);
	return recurse;
}

static int show_default(struct show_tree_data *data)
{
	size_t baselen = data->base->len;

	if (shown_fields & FIELD_SIZE) {
		char size_text[24];
		if (data->type == OBJ_BLOB) {
			unsigned long size;
			if (oid_object_info(the_repository, data->oid, &size) == OBJ_BAD)
				xsnprintf(size_text, sizeof(size_text), "BAD");
			else
				xsnprintf(size_text, sizeof(size_text),
					  "%" PRIuMAX, (uintmax_t)size);
		} else {
			xsnprintf(size_text, sizeof(size_text), "-");
		}
		printf("%06o %s %s %7s\t", data->mode, type_name(data->type),
		find_unique_abbrev(data->oid, abbrev), size_text);
	} else {
		printf("%06o %s %s\t", data->mode, type_name(data->type),
		find_unique_abbrev(data->oid, abbrev));
	}
	baselen = data->base->len;
	strbuf_addstr(data->base, data->pathname);
	write_name_quoted_relative(data->base->buf,
				   chomp_prefix ? ls_tree_prefix : NULL, stdout,
				   line_termination);
	strbuf_setlen(data->base, baselen);
	return 1;
}

static int show_tree(const struct object_id *oid, struct strbuf *base,
		const char *pathname, unsigned mode, void *context)
{
	int recurse = 0;
	size_t baselen;
	enum object_type type = get_type(mode);

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

	if (shown_fields == FIELD_OBJECT_NAME) {
		printf("%s%c", find_unique_abbrev(oid, abbrev), line_termination);
		return recurse;
	}

	if (shown_fields == FIELD_FILE_NAME) {
		baselen = base->len;
		strbuf_addstr(base, pathname);
		write_name_quoted_relative(base->buf,
					   chomp_prefix ? ls_tree_prefix : NULL,
					   stdout, line_termination);
		strbuf_setlen(base, baselen);
		return recurse;
	}

	if (shown_fields >= FIELD_DEFAULT)
		show_default(&data);

	return recurse;
}

int cmd_ls_tree(int argc, const char **argv, const char *prefix)
{
	struct object_id oid;
	struct tree *tree;
	int i, full_tree = 0;
	read_tree_fn_t fn = show_tree;
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
			    MODE_NAME_ONLY),
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

	git_config(git_default_config, NULL);
	ls_tree_prefix = prefix;
	if (prefix && *prefix)
		chomp_prefix = strlen(prefix);

	argc = parse_options(argc, argv, prefix, ls_tree_options,
			     ls_tree_usage, 0);
	if (full_tree) {
		ls_tree_prefix = prefix = NULL;
		chomp_prefix = 0;
	}
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

	parse_shown_fields();

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
	if (format &&
	    (!strcmp(format, default_format) ||
	     !strcmp(format, long_format) ||
	     !strcmp(format, name_only_format) ||
	     !strcmp(format, object_only_format)))
		fn = show_tree;
	else if (format)
		fn = show_tree_fmt;

	return !!read_tree(the_repository, tree, &pathspec, fn, NULL);
}
