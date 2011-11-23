/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "quote.h"
#include "commit.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "refs.h"
#include "submodule.h"

/*
 * diff-files
 */

/*
 * Has the work tree entity been removed?
 *
 * Return 1 if it was removed from the work tree, 0 if an entity to be
 * compared with the cache entry ce still exists (the latter includes
 * the case where a directory that is not a submodule repository
 * exists for ce that is a submodule -- it is a submodule that is not
 * checked out).  Return negative for an error.
 */
static int check_removed(const struct cache_entry *ce, struct stat *st)
{
	if (lstat(ce->name, st) < 0) {
		if (errno != ENOENT && errno != ENOTDIR)
			return -1;
		return 1;
	}
	if (has_symlink_leading_path(ce->name, ce_namelen(ce)))
		return 1;
	if (S_ISDIR(st->st_mode)) {
		unsigned char sub[20];

		/*
		 * If ce is already a gitlink, we can have a plain
		 * directory (i.e. the submodule is not checked out),
		 * or a checked out submodule.  Either case this is not
		 * a case where something was removed from the work tree,
		 * so we will return 0.
		 *
		 * Otherwise, if the directory is not a submodule
		 * repository, that means ce which was a blob turned into
		 * a directory --- the blob was removed!
		 */
		if (!S_ISGITLINK(ce->ce_mode) &&
		    resolve_gitlink_ref(ce->name, "HEAD", sub))
			return 1;
	}
	return 0;
}

/*
 * Has a file changed or has a submodule new commits or a dirty work tree?
 *
 * Return 1 when changes are detected, 0 otherwise. If the DIRTY_SUBMODULES
 * option is set, the caller does not only want to know if a submodule is
 * modified at all but wants to know all the conditions that are met (new
 * commits, untracked content and/or modified content).
 */
static int match_stat_with_submodule(struct diff_options *diffopt,
				      struct cache_entry *ce, struct stat *st,
				      unsigned ce_option, unsigned *dirty_submodule)
{
	int changed = ce_match_stat(ce, st, ce_option);
	if (S_ISGITLINK(ce->ce_mode)) {
		unsigned orig_flags = diffopt->flags;
		if (!DIFF_OPT_TST(diffopt, OVERRIDE_SUBMODULE_CONFIG))
			set_diffopt_flags_from_submodule_config(diffopt, ce->name);
		if (DIFF_OPT_TST(diffopt, IGNORE_SUBMODULES))
			changed = 0;
		else if (!DIFF_OPT_TST(diffopt, IGNORE_DIRTY_SUBMODULES)
		    && (!changed || DIFF_OPT_TST(diffopt, DIRTY_SUBMODULES)))
			*dirty_submodule = is_submodule_modified(ce->name, DIFF_OPT_TST(diffopt, IGNORE_UNTRACKED_IN_SUBMODULES));
		diffopt->flags = orig_flags;
	}
	return changed;
}

int run_diff_files(struct rev_info *revs, unsigned int option)
{
	int entries, i;
	int diff_unmerged_stage = revs->max_count;
	int silent_on_removed = option & DIFF_SILENT_ON_REMOVED;
	unsigned ce_option = ((option & DIFF_RACY_IS_MODIFIED)
			      ? CE_MATCH_RACY_IS_DIRTY : 0);

	diff_set_mnemonic_prefix(&revs->diffopt, "i/", "w/");

	if (diff_unmerged_stage < 0)
		diff_unmerged_stage = 2;
	entries = active_nr;
	for (i = 0; i < entries; i++) {
		struct stat st;
		unsigned int oldmode, newmode;
		struct cache_entry *ce = active_cache[i];
		int changed;
		unsigned dirty_submodule = 0;

		if (diff_can_quit_early(&revs->diffopt))
			break;

		if (!ce_path_match(ce, &revs->prune_data))
			continue;

		if (ce_stage(ce)) {
			struct combine_diff_path *dpath;
			struct diff_filepair *pair;
			unsigned int wt_mode = 0;
			int num_compare_stages = 0;
			size_t path_len;

			path_len = ce_namelen(ce);

			dpath = xmalloc(combine_diff_path_size(5, path_len));
			dpath->path = (char *) &(dpath->parent[5]);

			dpath->next = NULL;
			dpath->len = path_len;
			memcpy(dpath->path, ce->name, path_len);
			dpath->path[path_len] = '\0';
			hashclr(dpath->sha1);
			memset(&(dpath->parent[0]), 0,
			       sizeof(struct combine_diff_parent)*5);

			changed = check_removed(ce, &st);
			if (!changed)
				wt_mode = ce_mode_from_stat(ce, st.st_mode);
			else {
				if (changed < 0) {
					perror(ce->name);
					continue;
				}
				if (silent_on_removed)
					continue;
				wt_mode = 0;
			}
			dpath->mode = wt_mode;

			while (i < entries) {
				struct cache_entry *nce = active_cache[i];
				int stage;

				if (strcmp(ce->name, nce->name))
					break;

				/* Stage #2 (ours) is the first parent,
				 * stage #3 (theirs) is the second.
				 */
				stage = ce_stage(nce);
				if (2 <= stage) {
					int mode = nce->ce_mode;
					num_compare_stages++;
					hashcpy(dpath->parent[stage-2].sha1, nce->sha1);
					dpath->parent[stage-2].mode = ce_mode_from_stat(nce, mode);
					dpath->parent[stage-2].status =
						DIFF_STATUS_MODIFIED;
				}

				/* diff against the proper unmerged stage */
				if (stage == diff_unmerged_stage)
					ce = nce;
				i++;
			}
			/*
			 * Compensate for loop update
			 */
			i--;

			if (revs->combine_merges && num_compare_stages == 2) {
				show_combined_diff(dpath, 2,
						   revs->dense_combined_merges,
						   revs);
				free(dpath);
				continue;
			}
			free(dpath);
			dpath = NULL;

			/*
			 * Show the diff for the 'ce' if we found the one
			 * from the desired stage.
			 */
			pair = diff_unmerge(&revs->diffopt, ce->name);
			if (wt_mode)
				pair->two->mode = wt_mode;
			if (ce_stage(ce) != diff_unmerged_stage)
				continue;
		}

		if (ce_uptodate(ce) || ce_skip_worktree(ce))
			continue;

		/* If CE_VALID is set, don't look at workdir for file removal */
		changed = (ce->ce_flags & CE_VALID) ? 0 : check_removed(ce, &st);
		if (changed) {
			if (changed < 0) {
				perror(ce->name);
				continue;
			}
			if (silent_on_removed)
				continue;
			diff_addremove(&revs->diffopt, '-', ce->ce_mode,
				       ce->sha1, ce->name, 0);
			continue;
		}
		changed = match_stat_with_submodule(&revs->diffopt, ce, &st,
						    ce_option, &dirty_submodule);
		if (!changed && !dirty_submodule) {
			ce_mark_uptodate(ce);
			if (!DIFF_OPT_TST(&revs->diffopt, FIND_COPIES_HARDER))
				continue;
		}
		oldmode = ce->ce_mode;
		newmode = ce_mode_from_stat(ce, st.st_mode);
		diff_change(&revs->diffopt, oldmode, newmode,
			    ce->sha1, (changed ? null_sha1 : ce->sha1),
			    ce->name, 0, dirty_submodule);

	}
	diffcore_std(&revs->diffopt);
	diff_flush(&revs->diffopt);
	return 0;
}

/*
 * diff-index
 */

/* A file entry went away or appeared */
static void diff_index_show_file(struct rev_info *revs,
				 const char *prefix,
				 struct cache_entry *ce,
				 const unsigned char *sha1, unsigned int mode,
				 unsigned dirty_submodule)
{
	diff_addremove(&revs->diffopt, prefix[0], mode,
		       sha1, ce->name, dirty_submodule);
}

static int get_stat_data(struct cache_entry *ce,
			 const unsigned char **sha1p,
			 unsigned int *modep,
			 int cached, int match_missing,
			 unsigned *dirty_submodule, struct diff_options *diffopt)
{
	const unsigned char *sha1 = ce->sha1;
	unsigned int mode = ce->ce_mode;

	if (!cached && !ce_uptodate(ce)) {
		int changed;
		struct stat st;
		changed = check_removed(ce, &st);
		if (changed < 0)
			return -1;
		else if (changed) {
			if (match_missing) {
				*sha1p = sha1;
				*modep = mode;
				return 0;
			}
			return -1;
		}
		changed = match_stat_with_submodule(diffopt, ce, &st,
						    0, dirty_submodule);
		if (changed) {
			mode = ce_mode_from_stat(ce, st.st_mode);
			sha1 = null_sha1;
		}
	}

	*sha1p = sha1;
	*modep = mode;
	return 0;
}

static void show_new_file(struct rev_info *revs,
			  struct cache_entry *new,
			  int cached, int match_missing)
{
	const unsigned char *sha1;
	unsigned int mode;
	unsigned dirty_submodule = 0;

	/*
	 * New file in the index: it might actually be different in
	 * the working tree.
	 */
	if (get_stat_data(new, &sha1, &mode, cached, match_missing,
	    &dirty_submodule, &revs->diffopt) < 0)
		return;

	diff_index_show_file(revs, "+", new, sha1, mode, dirty_submodule);
}

static int show_modified(struct rev_info *revs,
			 struct cache_entry *old,
			 struct cache_entry *new,
			 int report_missing,
			 int cached, int match_missing)
{
	unsigned int mode, oldmode;
	const unsigned char *sha1;
	unsigned dirty_submodule = 0;

	if (get_stat_data(new, &sha1, &mode, cached, match_missing,
			  &dirty_submodule, &revs->diffopt) < 0) {
		if (report_missing)
			diff_index_show_file(revs, "-", old,
					     old->sha1, old->ce_mode, 0);
		return -1;
	}

	if (revs->combine_merges && !cached &&
	    (hashcmp(sha1, old->sha1) || hashcmp(old->sha1, new->sha1))) {
		struct combine_diff_path *p;
		int pathlen = ce_namelen(new);

		p = xmalloc(combine_diff_path_size(2, pathlen));
		p->path = (char *) &p->parent[2];
		p->next = NULL;
		p->len = pathlen;
		memcpy(p->path, new->name, pathlen);
		p->path[pathlen] = 0;
		p->mode = mode;
		hashclr(p->sha1);
		memset(p->parent, 0, 2 * sizeof(struct combine_diff_parent));
		p->parent[0].status = DIFF_STATUS_MODIFIED;
		p->parent[0].mode = new->ce_mode;
		hashcpy(p->parent[0].sha1, new->sha1);
		p->parent[1].status = DIFF_STATUS_MODIFIED;
		p->parent[1].mode = old->ce_mode;
		hashcpy(p->parent[1].sha1, old->sha1);
		show_combined_diff(p, 2, revs->dense_combined_merges, revs);
		free(p);
		return 0;
	}

	oldmode = old->ce_mode;
	if (mode == oldmode && !hashcmp(sha1, old->sha1) && !dirty_submodule &&
	    !DIFF_OPT_TST(&revs->diffopt, FIND_COPIES_HARDER))
		return 0;

	diff_change(&revs->diffopt, oldmode, mode,
		    old->sha1, sha1, old->name, 0, dirty_submodule);
	return 0;
}

/*
 * This gets a mix of an existing index and a tree, one pathname entry
 * at a time. The index entry may be a single stage-0 one, but it could
 * also be multiple unmerged entries (in which case idx_pos/idx_nr will
 * give you the position and number of entries in the index).
 */
static void do_oneway_diff(struct unpack_trees_options *o,
	struct cache_entry *idx,
	struct cache_entry *tree)
{
	struct rev_info *revs = o->unpack_data;
	int match_missing, cached;

	/* if the entry is not checked out, don't examine work tree */
	cached = o->index_only ||
		(idx && ((idx->ce_flags & CE_VALID) || ce_skip_worktree(idx)));
	/*
	 * Backward compatibility wart - "diff-index -m" does
	 * not mean "do not ignore merges", but "match_missing".
	 *
	 * But with the revision flag parsing, that's found in
	 * "!revs->ignore_merges".
	 */
	match_missing = !revs->ignore_merges;

	if (cached && idx && ce_stage(idx)) {
		struct diff_filepair *pair;
		pair = diff_unmerge(&revs->diffopt, idx->name);
		if (tree)
			fill_filespec(pair->one, tree->sha1, tree->ce_mode);
		return;
	}

	/*
	 * Something added to the tree?
	 */
	if (!tree) {
		show_new_file(revs, idx, cached, match_missing);
		return;
	}

	/*
	 * Something removed from the tree?
	 */
	if (!idx) {
		diff_index_show_file(revs, "-", tree, tree->sha1, tree->ce_mode, 0);
		return;
	}

	/* Show difference between old and new */
	show_modified(revs, tree, idx, 1, cached, match_missing);
}

/*
 * The unpack_trees() interface is designed for merging, so
 * the different source entries are designed primarily for
 * the source trees, with the old index being really mainly
 * used for being replaced by the result.
 *
 * For diffing, the index is more important, and we only have a
 * single tree.
 *
 * We're supposed to advance o->pos to skip what we have already processed.
 *
 * This wrapper makes it all more readable, and takes care of all
 * the fairly complex unpack_trees() semantic requirements, including
 * the skipping, the path matching, the type conflict cases etc.
 */
static int oneway_diff(struct cache_entry **src, struct unpack_trees_options *o)
{
	struct cache_entry *idx = src[0];
	struct cache_entry *tree = src[1];
	struct rev_info *revs = o->unpack_data;

	/*
	 * Unpack-trees generates a DF/conflict entry if
	 * there was a directory in the index and a tree
	 * in the tree. From a diff standpoint, that's a
	 * delete of the tree and a create of the file.
	 */
	if (tree == o->df_conflict_entry)
		tree = NULL;

	if (ce_path_match(idx ? idx : tree, &revs->prune_data)) {
		do_oneway_diff(o, idx, tree);
		if (diff_can_quit_early(&revs->diffopt)) {
			o->exiting_early = 1;
			return -1;
		}
	}

	return 0;
}

static int diff_cache(struct rev_info *revs,
		      const unsigned char *tree_sha1,
		      const char *tree_name,
		      int cached)
{
	struct tree *tree;
	struct tree_desc t;
	struct unpack_trees_options opts;

	tree = parse_tree_indirect(tree_sha1);
	if (!tree)
		return error("bad tree object %s",
			     tree_name ? tree_name : sha1_to_hex(tree_sha1));
	memset(&opts, 0, sizeof(opts));
	opts.head_idx = 1;
	opts.index_only = cached;
	opts.diff_index_cached = (cached &&
				  !DIFF_OPT_TST(&revs->diffopt, FIND_COPIES_HARDER));
	opts.merge = 1;
	opts.fn = oneway_diff;
	opts.unpack_data = revs;
	opts.src_index = &the_index;
	opts.dst_index = NULL;
	opts.pathspec = &revs->diffopt.pathspec;

	init_tree_desc(&t, tree->buffer, tree->size);
	return unpack_trees(1, &t, &opts);
}

int run_diff_index(struct rev_info *revs, int cached)
{
	struct object_array_entry *ent;

	ent = revs->pending.objects;
	if (diff_cache(revs, ent->item->sha1, ent->name, cached))
		exit(128);

	diff_set_mnemonic_prefix(&revs->diffopt, "c/", cached ? "i/" : "w/");
	diffcore_fix_diff_index(&revs->diffopt);
	diffcore_std(&revs->diffopt);
	diff_flush(&revs->diffopt);
	return 0;
}

int do_diff_cache(const unsigned char *tree_sha1, struct diff_options *opt)
{
	struct rev_info revs;

	init_revisions(&revs, NULL);
	init_pathspec(&revs.prune_data, opt->pathspec.raw);
	revs.diffopt = *opt;

	if (diff_cache(&revs, tree_sha1, NULL, 1))
		exit(128);
	return 0;
}

int index_differs_from(const char *def, int diff_flags)
{
	struct rev_info rev;
	struct setup_revision_opt opt;

	init_revisions(&rev, NULL);
	memset(&opt, 0, sizeof(opt));
	opt.def = def;
	setup_revisions(0, NULL, &rev, &opt);
	DIFF_OPT_SET(&rev.diffopt, QUICK);
	DIFF_OPT_SET(&rev.diffopt, EXIT_WITH_STATUS);
	rev.diffopt.flags |= diff_flags;
	run_diff_index(&rev, 1);
	if (rev.pending.alloc)
		free(rev.pending.objects);
	return (DIFF_OPT_TST(&rev.diffopt, HAS_CHANGES) != 0);
}
