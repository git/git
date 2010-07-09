/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "quote.h"
#include "builtin.h"
#include "parse-options.h"

static int line_termination = '\n';
#define LS_RECURSIVE 1
#define LS_TREE_ONLY 2
#define LS_SHOW_TREES 4
#define LS_NAME_ONLY 8
#define LS_SHOW_SIZE 16
static int abbrev;
static int ls_options;
static const char **pathspec;
static int chomp_prefix;
static const char *ls_tree_prefix;

static const  char * const ls_tree_usage[] = {
	"git ls-tree [<options>] <tree-ish> [path...]",
	NULL
};

static int show_recursive(const char *base, int baselen, const char *pathname)
{
	const char **s;

	if (ls_options & LS_RECURSIVE)
		return 1;

	s = pathspec;
	if (!s)
		return 0;

	for (;;) {
		const char *spec = *s++;
		int len, speclen;

		if (!spec)
			return 0;
		if (strncmp(base, spec, baselen))
			continue;
		len = strlen(pathname);
		spec += baselen;
		speclen = strlen(spec);
		if (speclen <= len)
			continue;
		if (memcmp(pathname, spec, len))
			continue;
		return 1;
	}
}

static int show_tree(const unsigned char *sha1, const char *base, int baselen,
		const char *pathname, unsigned mode, int stage, void *context)
{
	int retval = 0;
	const char *type = blob_type;

	if (S_ISGITLINK(mode)) {
		/*
		 * Maybe we want to have some recursive version here?
		 *
		 * Something similar to this incomplete example:
		 *
		if (show_subprojects(base, baselen, pathname))
			retval = READ_TREE_RECURSIVE;
		 *
		 */
		type = commit_type;
	} else if (S_ISDIR(mode)) {
		if (show_recursive(base, baselen, pathname)) {
			retval = READ_TREE_RECURSIVE;
			if (!(ls_options & LS_SHOW_TREES))
				return retval;
		}
		type = tree_type;
	}
	else if (ls_options & LS_TREE_ONLY)
		return 0;

	if (chomp_prefix &&
	    (baselen < chomp_prefix || memcmp(ls_tree_prefix, base, chomp_prefix)))
		return 0;

	if (!(ls_options & LS_NAME_ONLY)) {
		if (ls_options & LS_SHOW_SIZE) {
			char size_text[24];
			if (!strcmp(type, blob_type)) {
				unsigned long size;
				if (sha1_object_info(sha1, &size) == OBJ_BAD)
					strcpy(size_text, "BAD");
				else
					snprintf(size_text, sizeof(size_text),
						 "%lu", size);
			} else
				strcpy(size_text, "-");
			printf("%06o %s %s %7s\t", mode, type,
			       find_unique_abbrev(sha1, abbrev),
			       size_text);
		} else
			printf("%06o %s %s\t", mode, type,
			       find_unique_abbrev(sha1, abbrev));
	}
	write_name_quotedpfx(base + chomp_prefix, baselen - chomp_prefix,
			  pathname, stdout, line_termination);
	return retval;
}

int cmd_ls_tree(int argc, const char **argv, const char *prefix)
{
	unsigned char sha1[20];
	struct tree *tree;
	int full_tree = 0;
	const struct option ls_tree_options[] = {
		OPT_BIT('d', NULL, &ls_options, "only show trees",
			LS_TREE_ONLY),
		OPT_BIT('r', NULL, &ls_options, "recurse into subtrees",
			LS_RECURSIVE),
		OPT_BIT('t', NULL, &ls_options, "show trees when recursing",
			LS_SHOW_TREES),
		OPT_SET_INT('z', NULL, &line_termination,
			    "terminate entries with NUL byte", 0),
		OPT_BIT('l', "long", &ls_options, "include object size",
			LS_SHOW_SIZE),
		OPT_BIT(0, "name-only", &ls_options, "list only filenames",
			LS_NAME_ONLY),
		OPT_BIT(0, "name-status", &ls_options, "list only filenames",
			LS_NAME_ONLY),
		OPT_SET_INT(0, "full-name", &chomp_prefix,
			    "use full path names", 0),
		OPT_BOOLEAN(0, "full-tree", &full_tree,
			    "list entire tree; not just current directory "
			    "(implies --full-name)"),
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
	if (get_sha1(argv[0], sha1))
		die("Not a valid object name %s", argv[0]);

	pathspec = get_pathspec(prefix, argv + 1);
	tree = parse_tree_indirect(sha1);
	if (!tree)
		die("not a tree object");
	read_tree_recursive(tree, "", 0, 0, pathspec, show_tree, NULL);

	return 0;
}
