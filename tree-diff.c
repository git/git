/*
 * Helper functions for tree diff generation
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "tree.h"

static void show_entry(struct diff_options *opt, const char *prefix,
		       struct tree_desc *desc, struct strbuf *base);

static int compare_tree_entry(struct tree_desc *t1, struct tree_desc *t2,
			      struct strbuf *base, struct diff_options *opt)
{
	unsigned mode1, mode2;
	const char *path1, *path2;
	const unsigned char *sha1, *sha2;
	int cmp, pathlen1, pathlen2;
	int old_baselen = base->len;

	sha1 = tree_entry_extract(t1, &path1, &mode1);
	sha2 = tree_entry_extract(t2, &path2, &mode2);

	pathlen1 = tree_entry_len(&t1->entry);
	pathlen2 = tree_entry_len(&t2->entry);

	/*
	 * NOTE files and directories *always* compare differently,
	 * even when having the same name.
	 */
	cmp = base_name_compare(path1, pathlen1, mode1, path2, pathlen2, mode2);
	if (cmp < 0) {
		show_entry(opt, "-", t1, base);
		return -1;
	}
	if (cmp > 0) {
		show_entry(opt, "+", t2, base);
		return 1;
	}
	if (!DIFF_OPT_TST(opt, FIND_COPIES_HARDER) && !hashcmp(sha1, sha2) && mode1 == mode2)
		return 0;

	strbuf_add(base, path1, pathlen1);
	if (DIFF_OPT_TST(opt, RECURSIVE) && S_ISDIR(mode1)) {
		if (DIFF_OPT_TST(opt, TREE_IN_RECURSIVE)) {
			opt->change(opt, mode1, mode2,
				    sha1, sha2, 1, 1, base->buf, 0, 0);
		}
		strbuf_addch(base, '/');
		diff_tree_sha1(sha1, sha2, base->buf, opt);
	} else {
		opt->change(opt, mode1, mode2, sha1, sha2, 1, 1, base->buf, 0, 0);
	}
	strbuf_setlen(base, old_baselen);
	return 0;
}

/* An entry went away or appeared */
static void show_entry(struct diff_options *opt, const char *prefix,
		       struct tree_desc *desc, struct strbuf *base)
{
	unsigned mode;
	const char *path;
	const unsigned char *sha1 = tree_entry_extract(desc, &path, &mode);
	int pathlen = tree_entry_len(&desc->entry);
	int old_baselen = base->len;

	strbuf_add(base, path, pathlen);
	if (DIFF_OPT_TST(opt, RECURSIVE) && S_ISDIR(mode)) {
		if (DIFF_OPT_TST(opt, TREE_IN_RECURSIVE))
			opt->add_remove(opt, *prefix, mode, sha1, 1, base->buf, 0);

		strbuf_addch(base, '/');
		diff_tree_sha1(*prefix == '-' ? sha1 : NULL,
			       *prefix == '+' ? sha1 : NULL, base->buf, opt);
	} else
		opt->add_remove(opt, prefix[0], mode, sha1, 1, base->buf, 0);

	strbuf_setlen(base, old_baselen);
}

static void skip_uninteresting(struct tree_desc *t, struct strbuf *base,
			       struct diff_options *opt)
{
	enum interesting match;

	while (t->size) {
		match = tree_entry_interesting(&t->entry, base, 0, &opt->pathspec);
		if (match) {
			if (match == all_entries_not_interesting)
				t->size = 0;
			break;
		}
		update_tree_entry(t);
	}
}

int diff_tree(struct tree_desc *t1, struct tree_desc *t2,
	      const char *base_str, struct diff_options *opt)
{
	struct strbuf base;
	int baselen = strlen(base_str);

	/* Enable recursion indefinitely */
	opt->pathspec.recursive = DIFF_OPT_TST(opt, RECURSIVE);

	strbuf_init(&base, PATH_MAX);
	strbuf_add(&base, base_str, baselen);

	for (;;) {
		if (diff_can_quit_early(opt))
			break;
		if (opt->pathspec.nr) {
			skip_uninteresting(t1, &base, opt);
			skip_uninteresting(t2, &base, opt);
		}
		if (!t1->size) {
			if (!t2->size)
				break;
			show_entry(opt, "+", t2, &base);
			update_tree_entry(t2);
			continue;
		}
		if (!t2->size) {
			show_entry(opt, "-", t1, &base);
			update_tree_entry(t1);
			continue;
		}
		switch (compare_tree_entry(t1, t2, &base, opt)) {
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

	strbuf_release(&base);
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
	int i;

	/*
	 * follow-rename code is very specific, we need exactly one
	 * path. Magic that matches more than one path is not
	 * supported.
	 */
	GUARD_PATHSPEC(&opt->pathspec, PATHSPEC_FROMTOP | PATHSPEC_LITERAL);
#if 0
	/*
	 * We should reject wildcards as well. Unfortunately we
	 * haven't got a reliable way to detect that 'foo\*bar' in
	 * fact has no wildcards. nowildcard_len is merely a hint for
	 * optimization. Let it slip for now until wildmatch is taught
	 * about dry-run mode and returns wildcard info.
	 */
	if (opt->pathspec.has_wildcard)
		die("BUG:%s:%d: wildcards are not supported",
		    __FILE__, __LINE__);
#endif

	/* Remove the file creation entry from the diff queue, and remember it */
	choice = q->queue[0];
	q->nr = 0;

	diff_setup(&diff_opts);
	DIFF_OPT_SET(&diff_opts, RECURSIVE);
	DIFF_OPT_SET(&diff_opts, FIND_COPIES_HARDER);
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_opts.single_follow = opt->pathspec.items[0].match;
	diff_opts.break_opt = opt->break_opt;
	diff_opts.rename_score = opt->rename_score;
	diff_setup_done(&diff_opts);
	diff_tree(t1, t2, base, &diff_opts);
	diffcore_std(&diff_opts);
	free_pathspec(&diff_opts.pathspec);

	/* Go through the new set of filepairing, and see if we find a more interesting one */
	opt->found_follow = 0;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];

		/*
		 * Found a source? Not only do we use that for the new
		 * diff_queued_diff, we will also use that as the path in
		 * the future!
		 */
		if ((p->status == 'R' || p->status == 'C') &&
		    !strcmp(p->two->path, opt->pathspec.items[0].match)) {
			const char *path[2];

			/* Switch the file-pairs around */
			q->queue[i] = choice;
			choice = p;

			/* Update the path we use from now on.. */
			path[0] = p->one->path;
			path[1] = NULL;
			free_pathspec(&opt->pathspec);
			parse_pathspec(&opt->pathspec,
				       PATHSPEC_ALL_MAGIC & ~PATHSPEC_LITERAL,
				       PATHSPEC_LITERAL_PATH, "", path);

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

	tree1 = fill_tree_descriptor(&t1, old);
	tree2 = fill_tree_descriptor(&t2, new);
	size1 = t1.size;
	size2 = t2.size;
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
	return diff_tree_sha1(NULL, new, base, opt);
}
