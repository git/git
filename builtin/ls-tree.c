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
	return !!read_tree(the_repository, tree,
			   &pathspec, show_tree, NULL);
}
