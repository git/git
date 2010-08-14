/*
 * Helper functions for tree diff generation
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "tree.h"

static char *malloc_base(const char *base, int baselen, const char *path, int pathlen)
{
	char *newbase = xmalloc(baselen + pathlen + 2);
	memcpy(newbase, base, baselen);
	memcpy(newbase + baselen, path, pathlen);
	memcpy(newbase + baselen + pathlen, "/", 2);
	return newbase;
}

static char *malloc_fullname(const char *base, int baselen, const char *path, int pathlen)
{
	char *fullname = xmalloc(baselen + pathlen + 1);
	memcpy(fullname, base, baselen);
	memcpy(fullname + baselen, path, pathlen);
	fullname[baselen + pathlen] = 0;
	return fullname;
}

static void show_entry(struct diff_options *opt, const char *prefix, struct tree_desc *desc,
		       const char *base, int baselen);

static int compare_tree_entry(struct tree_desc *t1, struct tree_desc *t2, const char *base, int baselen, struct diff_options *opt)
{
	unsigned mode1, mode2;
	const char *path1, *path2;
	const unsigned char *sha1, *sha2;
	int cmp, pathlen1, pathlen2;
	char *fullname;

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
	if (!DIFF_OPT_TST(opt, FIND_COPIES_HARDER) && !hashcmp(sha1, sha2) && mode1 == mode2)
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

	if (DIFF_OPT_TST(opt, RECURSIVE) && S_ISDIR(mode1)) {
		int retval;
		char *newbase = malloc_base(base, baselen, path1, pathlen1);
		if (DIFF_OPT_TST(opt, TREE_IN_RECURSIVE)) {
			newbase[baselen + pathlen1] = 0;
			opt->change(opt, mode1, mode2,
				    sha1, sha2, newbase, 0, 0);
			newbase[baselen + pathlen1] = '/';
		}
		retval = diff_tree_sha1(sha1, sha2, newbase, opt);
		free(newbase);
		return retval;
	}

	fullname = malloc_fullname(base, baselen, path1, pathlen1);
	opt->change(opt, mode1, mode2, sha1, sha2, fullname, 0, 0);
	free(fullname);
	return 0;
}

/*
 * Is a tree entry interesting given the pathspec we have?
 *
 * Return:
 *  - 2 for "yes, and all subsequent entries will be"
 *  - 1 for yes
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
	int never_interesting = -1;

	if (!opt->nr_paths)
		return 1;

	sha1 = tree_entry_extract(desc, &path, &mode);

	pathlen = tree_entry_len(path, sha1);

	for (i = 0; i < opt->nr_paths; i++) {
		const char *match = opt->paths[i];
		int matchlen = opt->pathlens[i];
		int m = -1; /* signals that we haven't called strncmp() */

		if (baselen >= matchlen) {
			/* If it doesn't match, move along... */
			if (strncmp(base, match, matchlen))
				continue;

			/*
			 * If the base is a subdirectory of a path which
			 * was specified, all of them are interesting.
			 */
			if (!matchlen ||
			    base[matchlen] == '/' ||
			    match[matchlen - 1] == '/')
				return 2;

			/* Just a random prefix match */
			continue;
		}

		/* Does the base match? */
		if (strncmp(base, match, baselen))
			continue;

		match += baselen;
		matchlen -= baselen;

		if (never_interesting) {
			/*
			 * We have not seen any match that sorts later
			 * than the current path.
			 */

			/*
			 * Does match sort strictly earlier than path
			 * with their common parts?
			 */
			m = strncmp(match, path,
				    (matchlen < pathlen) ? matchlen : pathlen);
			if (m < 0)
				continue;

			/*
			 * If we come here even once, that means there is at
			 * least one pathspec that would sort equal to or
			 * later than the path we are currently looking at.
			 * In other words, if we have never reached this point
			 * after iterating all pathspecs, it means all
			 * pathspecs are either outside of base, or inside the
			 * base but sorts strictly earlier than the current
			 * one.  In either case, they will never match the
			 * subsequent entries.  In such a case, we initialized
			 * the variable to -1 and that is what will be
			 * returned, allowing the caller to terminate early.
			 */
			never_interesting = 0;
		}

		if (pathlen > matchlen)
			continue;

		if (matchlen > pathlen) {
			if (match[pathlen] != '/')
				continue;
			if (!S_ISDIR(mode))
				continue;
		}

		if (m == -1)
			/*
			 * we cheated and did not do strncmp(), so we do
			 * that here.
			 */
			m = strncmp(match, path, pathlen);

		/*
		 * If common part matched earlier then it is a hit,
		 * because we rejected the case where path is not a
		 * leading directory and is shorter than match.
		 */
		if (!m)
			return 1;
	}
	return never_interesting; /* No matches */
}

/* A whole sub-tree went away or appeared */
static void show_tree(struct diff_options *opt, const char *prefix, struct tree_desc *desc, const char *base, int baselen)
{
	int all_interesting = 0;
	while (desc->size) {
		int show;

		if (all_interesting)
			show = 1;
		else {
			show = tree_entry_interesting(desc, base, baselen,
						      opt);
			if (show == 2)
				all_interesting = 1;
		}
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
	int pathlen = tree_entry_len(path, sha1);

	if (DIFF_OPT_TST(opt, RECURSIVE) && S_ISDIR(mode)) {
		enum object_type type;
		char *newbase = malloc_base(base, baselen, path, pathlen);
		struct tree_desc inner;
		void *tree;
		unsigned long size;

		tree = read_sha1_file(sha1, &type, &size);
		if (!tree || type != OBJ_TREE)
			die("corrupt tree sha %s", sha1_to_hex(sha1));

		if (DIFF_OPT_TST(opt, TREE_IN_RECURSIVE)) {
			newbase[baselen + pathlen] = 0;
			opt->add_remove(opt, *prefix, mode, sha1, newbase, 0);
			newbase[baselen + pathlen] = '/';
		}

		init_tree_desc(&inner, tree, size);
		show_tree(opt, prefix, &inner, newbase, baselen + 1 + pathlen);

		free(tree);
		free(newbase);
	} else {
		char *fullname = malloc_fullname(base, baselen, path, pathlen);
		opt->add_remove(opt, prefix[0], mode, sha1, fullname, 0);
		free(fullname);
	}
}

static void skip_uninteresting(struct tree_desc *t, const char *base, int baselen, struct diff_options *opt)
{
	int all_interesting = 0;
	while (t->size) {
		int show;

		if (all_interesting)
			show = 1;
		else {
			show = tree_entry_interesting(t, base, baselen, opt);
			if (show == 2)
				all_interesting = 1;
		}
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
		if (DIFF_OPT_TST(opt, QUICK) &&
		    DIFF_OPT_TST(opt, HAS_CHANGES))
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
		die("git diff-tree: internal error");
	}
	return 0;
}

/*
 * Does it look like the resulting diff might be due to a rename?
 *  - single entry
 *  - not a valid previous file
 */
static inline int diff_might_be_rename(void)
{
	return diff_queued_diff.nr == 1 &&
		!DIFF_FILE_VALID(diff_queued_diff.queue[0]->one);
}

static void try_to_follow_renames(struct tree_desc *t1, struct tree_desc *t2, const char *base, struct diff_options *opt)
{
	struct diff_options diff_opts;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_filepair *choice;
	const char *paths[1];
	int i;

	/* Remove the file creation entry from the diff queue, and remember it */
	choice = q->queue[0];
	q->nr = 0;

	diff_setup(&diff_opts);
	DIFF_OPT_SET(&diff_opts, RECURSIVE);
	DIFF_OPT_SET(&diff_opts, FIND_COPIES_HARDER);
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_opts.single_follow = opt->paths[0];
	diff_opts.break_opt = opt->break_opt;
	paths[0] = NULL;
	diff_tree_setup_paths(paths, &diff_opts);
	if (diff_setup_done(&diff_opts) < 0)
		die("unable to set up diff options to follow renames");
	diff_tree(t1, t2, base, &diff_opts);
	diffcore_std(&diff_opts);
	diff_tree_release_paths(&diff_opts);

	/* Go through the new set of filepairing, and see if we find a more interesting one */
	opt->found_follow = 0;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];

		/*
		 * Found a source? Not only do we use that for the new
		 * diff_queued_diff, we will also use that as the path in
		 * the future!
		 */
		if ((p->status == 'R' || p->status == 'C') && !strcmp(p->two->path, opt->paths[0])) {
			/* Switch the file-pairs around */
			q->queue[i] = choice;
			choice = p;

			/* Update the path we use from now on.. */
			diff_tree_release_paths(opt);
			opt->paths[0] = xstrdup(p->one->path);
			diff_tree_setup_paths(opt->paths, opt);

			/*
			 * The caller expects us to return a set of vanilla
			 * filepairs to let a later call to diffcore_std()
			 * it makes to sort the renames out (among other
			 * things), but we already have found renames
			 * ourselves; signal diffcore_std() not to muck with
			 * rename information.
			 */
			opt->found_follow = 1;
			break;
		}
	}

	/*
	 * Then, discard all the non-relevant file pairs...
	 */
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		diff_free_filepair(p);
	}

	/*
	 * .. and re-instate the one we want (which might be either the
	 * original one, or the rename/copy we found)
	 */
	q->queue[0] = choice;
	q->nr = 1;
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
	if (!*base && DIFF_OPT_TST(opt, FOLLOW_RENAMES) && diff_might_be_rename()) {
		init_tree_desc(&t1, tree1, size1);
		init_tree_desc(&t2, tree2, size2);
		try_to_follow_renames(&t1, &t2, base, opt);
	}
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
