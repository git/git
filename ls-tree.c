/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "blob.h"
#include "tree.h"
#include "quote.h"

static int line_termination = '\n';
#define LS_RECURSIVE 1
#define LS_TREE_ONLY 2
#define LS_SHOW_TREES 4
#define LS_NAME_ONLY 8
static int abbrev = 0;
static int ls_options = 0;
const char **pathspec;
static int chomp_prefix = 0;
static const char *prefix;

static const char ls_tree_usage[] =
	"git-ls-tree [-d] [-r] [-t] [-z] [--name-only] [--name-status] [--full-name] [--abbrev[=<n>]] <tree-ish> [path...]";

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

static int show_tree(unsigned char *sha1, const char *base, int baselen,
		     const char *pathname, unsigned mode, int stage)
{
	int retval = 0;
	const char *type = blob_type;

	if (S_ISDIR(mode)) {
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
	    (baselen < chomp_prefix || memcmp(prefix, base, chomp_prefix)))
		return 0;

	if (!(ls_options & LS_NAME_ONLY))
		printf("%06o %s %s\t", mode, type,
				abbrev ? find_unique_abbrev(sha1,abbrev)
					: sha1_to_hex(sha1));
	write_name_quoted(base + chomp_prefix, baselen - chomp_prefix,
			  pathname,
			  line_termination, stdout);
	putchar(line_termination);
	return retval;
}

int main(int argc, const char **argv)
{
	unsigned char sha1[20];
	struct tree *tree;

	prefix = setup_git_directory();
	git_config(git_default_config);
	if (prefix && *prefix)
		chomp_prefix = strlen(prefix);
	while (1 < argc && argv[1][0] == '-') {
		switch (argv[1][1]) {
		case 'z':
			line_termination = 0;
			break;
		case 'r':
			ls_options |= LS_RECURSIVE;
			break;
		case 'd':
			ls_options |= LS_TREE_ONLY;
			break;
		case 't':
			ls_options |= LS_SHOW_TREES;
			break;
		case '-':
			if (!strcmp(argv[1]+2, "name-only") ||
			    !strcmp(argv[1]+2, "name-status")) {
				ls_options |= LS_NAME_ONLY;
				break;
			}
			if (!strcmp(argv[1]+2, "full-name")) {
				chomp_prefix = 0;
				break;
			}
			if (!strncmp(argv[1]+2, "abbrev=",7)) {
				abbrev = strtoul(argv[1]+9, NULL, 10);
				if (abbrev && abbrev < MINIMUM_ABBREV)
					abbrev = MINIMUM_ABBREV;
				else if (abbrev > 40)
					abbrev = 40;
				break;
			}
			if (!strcmp(argv[1]+2, "abbrev")) {
				abbrev = DEFAULT_ABBREV;
				break;
			}
			/* otherwise fallthru */
		default:
			usage(ls_tree_usage);
		}
		argc--; argv++;
	}
	/* -d -r should imply -t, but -d by itself should not have to. */
	if ( (LS_TREE_ONLY|LS_RECURSIVE) ==
	    ((LS_TREE_ONLY|LS_RECURSIVE) & ls_options))
		ls_options |= LS_SHOW_TREES;

	if (argc < 2)
		usage(ls_tree_usage);
	if (get_sha1(argv[1], sha1))
		die("Not a valid object name %s", argv[1]);

	pathspec = get_pathspec(prefix, argv + 2);
	tree = parse_tree_indirect(sha1);
	if (!tree)
		die("not a tree object");
	read_tree_recursive(tree, "", 0, 0, pathspec, show_tree);

	return 0;
}
