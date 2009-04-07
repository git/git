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

		if (DIFF_OPT_TST(&revs->diffopt, QUIET) &&
			DIFF_OPT_TST(&revs->diffopt, HAS_CHANGES))
			break;

		if (!ce_path_match(ce, revs->prune_data))
			continue;

		if (ce_stage(ce)) {
			struct combine_diff_path *dpath;
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
				dpath->mode = ce_mode_from_stat(ce, st.st_mode);
			else {
				if (changed < 0) {
					perror(ce->name);
					continue;
				}
				if (silent_on_removed)
					continue;
			}

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
			diff_unmerge(&revs->diffopt, ce->name, 0, null_sha1);
			if (ce_stage(ce) != diff_unmerged_stage)
				continue;
		}

		if (ce_uptodate(ce))
			continue;

		changed = check_removed(ce, &st);
		if (changed) {
			if (changed < 0) {
				perror(ce->name);
				continue;
			}
			if (silent_on_removed)
				continue;
			diff_addremove(&revs->diffopt, '-', ce->ce_mode,
				       ce->sha1, ce->name);
			continue;
		}
		changed = ce_match_stat(ce, &st, ce_option);
		if (!changed) {
			ce_mark_uptodate(ce);
			if (!DIFF_OPT_TST(&revs->diffopt, FIND_COPIES_HARDER))
				continue;
		}
		oldmode = ce->ce_mode;
		newmode = ce_mode_from_stat(ce, st.st_mode);
		diff_change(&revs->diffopt, oldmode, newmode,
			    ce->sha1, (changed ? null_sha1 : ce->sha1),
			    ce->name);

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
				 const unsigned char *sha1, unsigned int mode)
{
	diff_addremove(&revs->diffopt, prefix[0], mode,
		       sha1, ce->name);
}

static int get_stat_data(struct cache_entry *ce,
			 const unsigned char **sha1p,
			 unsigned int *modep,
			 int cached, int match_missing)
{
	const unsigned char *sha1 = ce->sha1;
	unsigned int mode = ce->ce_mode;

	if (!cached) {
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
		changed = ce_match_stat(ce, &st, 0);
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

	/*
	 * New file in the index: it might actually be different in
	 * the working copy.
	 */
	if (get_stat_data(new, &sha1, &mode, cached, match_missing) < 0)
		return;

	diff_index_show_file(revs, "+", new, sha1, mode);
}

static int show_modified(struct rev_info *revs,
			 struct cache_entry *old,
			 struct cache_entry *new,
			 int report_missing,
			 int cached, int match_missing)
{
	unsigned int mode, oldmode;
	const unsigned char *sha1;

	if (get_stat_data(new, &sha1, &mode, cached, match_missing) < 0) {
		if (report_missing)
			diff_index_show_file(revs, "-", old,
					     old->sha1, old->ce_mode);
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
	if (mode == oldmode && !hashcmp(sha1, old->sha1) &&
	    !DIFF_OPT_TST(&revs->diffopt, FIND_COPIES_HARDER))
		return 0;

	diff_change(&revs->diffopt, oldmode, mode,
		    old->sha1, sha1, old->name);
	return 0;
}

/*
 * This turns all merge entries into "stage 3". That guarantees that
 * when we read in the new tree (into "stage 1"), we won't lose sight
 * of the fact that we had unmerged entries.
 */
static void mark_merge_entries(void)
{
	int i;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (!ce_stage(ce))
			continue;
		ce->ce_flags |= CE_STAGEMASK;
	}
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

	/*
	 * Backward compatibility wart - "diff-index -m" does
	 * not mean "do not ignore merges", but "match_missing".
	 *
	 * But with the revision flag parsing, that's found in
	 * "!revs->ignore_merges".
	 */
	cached = o->index_only;
	match_missing = !revs->ignore_merges;

	if (cached && idx && ce_stage(idx)) {
		if (tree)
			diff_unmerge(&revs->diffopt, idx->name, idx->ce_mode, idx->sha1);
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
		diff_index_show_file(revs, "-", tree, tree->sha1, tree->ce_mode);
		return;
	}

	/* Show difference between old and new */
	show_modified(revs, tree, idx, 1, cached, match_missing);
}

static inline void skip_same_name(struct cache_entry *ce, struct unpack_trees_options *o)
{
	int len = ce_namelen(ce);
	const struct index_state *index = o->src_index;

	while (o->pos < index->cache_nr) {
		struct cache_entry *next = index->cache[o->pos];
		if (len != ce_namelen(next))
			break;
		if (memcmp(ce->name, next->name, len))
			break;
		o->pos++;
	}
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
 * We're supposed to return how many index entries we want to skip.
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

	if (idx && ce_stage(idx))
		skip_same_name(idx, o);

	/*
	 * Unpack-trees generates a DF/conflict entry if
	 * there was a directory in the index and a tree
	 * in the tree. From a diff standpoint, that's a
	 * delete of the tree and a create of the file.
	 */
	if (tree == o->df_conflict_entry)
		tree = NULL;

	if (ce_path_match(idx ? idx : tree, revs->prune_data))
		do_oneway_diff(o, idx, tree);

	return 0;
}

int run_diff_index(struct rev_info *revs, int cached)
{
	struct object *ent;
	struct tree *tree;
	const char *tree_name;
	struct unpack_trees_options opts;
	struct tree_desc t;

	mark_merge_entries();

	ent = revs->pending.objects[0].item;
	tree_name = revs->pending.objects[0].name;
	tree = parse_tree_indirect(ent->sha1);
	if (!tree)
		return error("bad tree object %s", tree_name);

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = 1;
	opts.index_only = cached;
	opts.merge = 1;
	opts.fn = oneway_diff;
	opts.unpack_data = revs;
	opts.src_index = &the_index;
	opts.dst_index = NULL;

	init_tree_desc(&t, tree->buffer, tree->size);
	if (unpack_trees(1, &t, &opts))
		exit(128);

	diff_set_mnemonic_prefix(&revs->diffopt, "c/", cached ? "i/" : "w/");
	diffcore_std(&revs->diffopt);
	diff_flush(&revs->diffopt);
	return 0;
}

int do_diff_cache(const unsigned char *tree_sha1, struct diff_options *opt)
{
	struct tree *tree;
	struct rev_info revs;
	int i;
	struct cache_entry **dst;
	struct cache_entry *last = NULL;
	struct unpack_trees_options opts;
	struct tree_desc t;

	/*
	 * This is used by git-blame to run diff-cache internally;
	 * it potentially needs to repeatedly run this, so we will
	 * start by removing the higher order entries the last round
	 * left behind.
	 */
	dst = active_cache;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ce_stage(ce)) {
			if (last && !strcmp(ce->name, last->name))
				continue;
			cache_tree_invalidate_path(active_cache_tree,
						   ce->name);
			last = ce;
			ce->ce_flags |= CE_REMOVE;
		}
		*dst++ = ce;
	}
	active_nr = dst - active_cache;

	init_revisions(&revs, NULL);
	revs.prune_data = opt->paths;
	tree = parse_tree_indirect(tree_sha1);
	if (!tree)
		die("bad tree object %s", sha1_to_hex(tree_sha1));

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = 1;
	opts.index_only = 1;
	opts.merge = 1;
	opts.fn = oneway_diff;
	opts.unpack_data = &revs;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;

	init_tree_desc(&t, tree->buffer, tree->size);
	if (unpack_trees(1, &t, &opts))
		exit(128);
	return 0;
}

int index_differs_from(const char *def, int diff_flags)
{
	struct rev_info rev;

	init_revisions(&rev, NULL);
	setup_revisions(0, NULL, &rev, def);
	DIFF_OPT_SET(&rev.diffopt, QUIET);
	DIFF_OPT_SET(&rev.diffopt, EXIT_WITH_STATUS);
	rev.diffopt.flags |= diff_flags;
	run_diff_index(&rev, 1);
	if (rev.pending.alloc)
		free(rev.pending.objects);
	return (DIFF_OPT_TST(&rev.diffopt, HAS_CHANGES) != 0);
}
