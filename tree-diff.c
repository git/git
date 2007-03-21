/*
 * Helper functions for tree diff generation
 */
#include "cache.h"
#include "diff.h"
#include "tree.h"

static char *malloc_base(const char *base, int baselen, const char *path, int pathlen)
{
	char *newbase = xmalloc(baselen + pathlen + 2);
	memcpy(newbase, base, baselen);
	memcpy(newbase + baselen, path, pathlen);
	memcpy(newbase + baselen + pathlen, "/", 2);
	return newbase;
}

static void show_entry(struct diff_options *opt, const char *prefix, struct tree_desc *desc,
		       const char *base, int baselen);

static int compare_tree_entry(struct tree_desc *t1, struct tree_desc *t2, const char *base, int baselen, struct diff_options *opt)
{
	unsigned mode1, mode2;
	const char *path1, *path2;
	const unsigned char *sha1, *sha2;
	int cmp, pathlen1, pathlen2;

	sha1 = tree_entry_extract(t1, &path1, &mode1);
	sha2 = tree_entry_extract(t2, &path2, &mode2);

	pathlen1 = tree_entry_len(path1, sha1);
	pathlen2 = tree_entry_len(path2, sha2);
	cmp = base_name_compare(path1, pathlen1, mode1, path2, pathlen2, mode2);
	if (cmp < 0) {
		show_entry(opt, "-", t1, base, baselen);
		return -1;
	}
	if (cmp > 0) {
		show_entry(opt, "+", t2, base, baselen);
		return 1;
	}
	if (!opt->find_copies_harder && !hashcmp(sha1, sha2) && mode1 == mode2)
		return 0;

	/*
	 * If the filemode has changed to/from a directory from/to a regular
	 * file, we need to consider it a remove and an add.
	 */
	if (S_ISDIR(mode1) != S_ISDIR(mode2)) {
		show_entry(opt, "-", t1, base, baselen);
		show_entry(opt, "+", t2, base, baselen);
		return 0;
	}

	if (opt->recursive && S_ISDIR(mode1)) {
		int retval;
		char *newbase = malloc_base(base, baselen, path1, pathlen1);
		if (opt->tree_in_recursive)
			opt->change(opt, mode1, mode2,
				    sha1, sha2, base, path1);
		retval = diff_tree_sha1(sha1, sha2, newbase, opt);
		free(newbase);
		return retval;
	}

	opt->change(opt, mode1, mode2, sha1, sha2, base, path1);
	return 0;
}

/*
 * Is a tree entry interesting given the pathspec we have?
 *
 * Return:
 *  - positive for yes
 *  - zero for no
 *  - negative for "no, and no subsequent entries will be either"
 */
static int tree_entry_interesting(struct tree_desc *desc, const char *base, int baselen, struct diff_options *opt)
{
	const char *path;
	const unsigned char *sha1;
	unsigned mode;
	int i;
	int pathlen;

	if (!opt->nr_paths)
		return 1;

	sha1 = tree_entry_extract(desc, &path, &mode);

	pathlen = tree_entry_len(path, sha1);

	for (i=0; i < opt->nr_paths; i++) {
		const char *match = opt->paths[i];
		int matchlen = opt->pathlens[i];

		if (baselen >= matchlen) {
			/* If it doesn't match, move along... */
			if (strncmp(base, match, matchlen))
				continue;

			/* The base is a subdirectory of a path which was specified. */
			return 1;
		}

		/* Does the base match? */
		if (strncmp(base, match, baselen))
			continue;

		match += baselen;
		matchlen -= baselen;

		if (pathlen > matchlen)
			continue;

		if (matchlen > pathlen) {
			if (match[pathlen] != '/')
				continue;
			if (!S_ISDIR(mode))
				continue;
		}

		if (strncmp(path, match, pathlen))
			continue;

		return 1;
	}
	return 0; /* No matches */
}

/* A whole sub-tree went away or appeared */
static void show_tree(struct diff_options *opt, const char *prefix, struct tree_desc *desc, const char *base, int baselen)
{
	while (desc->size) {
		int show = tree_entry_interesting(desc, base, baselen, opt);
		if (show < 0)
			break;
		if (show)
			show_entry(opt, prefix, desc, base, baselen);
		update_tree_entry(desc);
	}
}

/* A file entry went away or appeared */
static void show_entry(struct diff_options *opt, const char *prefix, struct tree_desc *desc,
		       const char *base, int baselen)
{
	unsigned mode;
	const char *path;
	const unsigned char *sha1 = tree_entry_extract(desc, &path, &mode);

	if (opt->recursive && S_ISDIR(mode)) {
		enum object_type type;
		int pathlen = tree_entry_len(path, sha1);
		char *newbase = malloc_base(base, baselen, path, pathlen);
		struct tree_desc inner;
		void *tree;
		unsigned long size;

		tree = read_sha1_file(sha1, &type, &size);
		if (!tree || type != OBJ_TREE)
			die("corrupt tree sha %s", sha1_to_hex(sha1));

		init_tree_desc(&inner, tree, size);
		show_tree(opt, prefix, &inner, newbase, baselen + 1 + pathlen);

		free(tree);
		free(newbase);
	} else {
		opt->add_remove(opt, prefix[0], mode, sha1, base, path);
	}
}

static void skip_uninteresting(struct tree_desc *t, const char *base, int baselen, struct diff_options *opt)
{
	while (t->size) {
		int show = tree_entry_interesting(t, base, baselen, opt);
		if (!show) {
			update_tree_entry(t);
			continue;
		}
		/* Skip it all? */
		if (show < 0)
			t->size = 0;
		return;
	}
}

int diff_tree(struct tree_desc *t1, struct tree_desc *t2, const char *base, struct diff_options *opt)
{
	int baselen = strlen(base);

	for (;;) {
		if (opt->quiet && opt->has_changes)
			break;
		if (opt->nr_paths) {
			skip_uninteresting(t1, base, baselen, opt);
			skip_uninteresting(t2, base, baselen, opt);
		}
		if (!t1->size) {
			if (!t2->size)
				break;
			show_entry(opt, "+", t2, base, baselen);
			update_tree_entry(t2);
			continue;
		}
		if (!t2->size) {
			show_entry(opt, "-", t1, base, baselen);
			update_tree_entry(t1);
			continue;
		}
		switch (compare_tree_entry(t1, t2, base, baselen, opt)) {
		case -1:
			update_tree_entry(t1);
			continue;
		case 0:
			update_tree_entry(t1);
			/* Fallthrough */
		case 1:
			update_tree_entry(t2);
			continue;
		}
		die("git-diff-tree: internal error");
	}
	return 0;
}

int diff_tree_sha1(const unsigned char *old, const unsigned char *new, const char *base, struct diff_options *opt)
{
	void *tree1, *tree2;
	struct tree_desc t1, t2;
	unsigned long size1, size2;
	int retval;

	tree1 = read_object_with_reference(old, tree_type, &size1, NULL);
	if (!tree1)
		die("unable to read source tree (%s)", sha1_to_hex(old));
	tree2 = read_object_with_reference(new, tree_type, &size2, NULL);
	if (!tree2)
		die("unable to read destination tree (%s)", sha1_to_hex(new));
	init_tree_desc(&t1, tree1, size1);
	init_tree_desc(&t2, tree2, size2);
	retval = diff_tree(&t1, &t2, base, opt);
	free(tree1);
	free(tree2);
	return retval;
}

int diff_root_tree_sha1(const unsigned char *new, const char *base, struct diff_options *opt)
{
	int retval;
	void *tree;
	unsigned long size;
	struct tree_desc empty, real;

	tree = read_object_with_reference(new, tree_type, &size, NULL);
	if (!tree)
		die("unable to read root tree (%s)", sha1_to_hex(new));
	init_tree_desc(&real, tree, size);

	init_tree_desc(&empty, "", 0);
	retval = diff_tree(&empty, &real, base, opt);
	free(tree);
	return retval;
}

static int count_paths(const char **paths)
{
	int i = 0;
	while (*paths++)
		i++;
	return i;
}

void diff_tree_release_paths(struct diff_options *opt)
{
	free(opt->pathlens);
}

void diff_tree_setup_paths(const char **p, struct diff_options *opt)
{
	opt->nr_paths = 0;
	opt->pathlens = NULL;
	opt->paths = NULL;

	if (p) {
		int i;

		opt->paths = p;
		opt->nr_paths = count_paths(p);
		if (opt->nr_paths == 0) {
			opt->pathlens = NULL;
			return;
		}
		opt->pathlens = xmalloc(opt->nr_paths * sizeof(int));
		for (i=0; i < opt->nr_paths; i++)
			opt->pathlens[i] = strlen(p[i]);
	}
}
