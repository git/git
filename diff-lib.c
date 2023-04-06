/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "quote.h"
#include "commit.h"
#include "diff.h"
#include "diffcore.h"
#include "hex.h"
#include "revision.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "refs.h"
#include "submodule.h"
#include "dir.h"
#include "fsmonitor.h"
#include "commit-reach.h"

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
static int check_removed(const struct index_state *istate, const struct cache_entry *ce, struct stat *st)
{
	assert(is_fsmonitor_refreshed(istate));
	if (!(ce->ce_flags & CE_FSMONITOR_VALID) && lstat(ce->name, st) < 0) {
		if (!is_missing_file_error(errno))
			return -1;
		return 1;
	}
	if (has_symlink_leading_path(ce->name, ce_namelen(ce)))
		return 1;
	if (S_ISDIR(st->st_mode)) {
		struct object_id sub;

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
		    resolve_gitlink_ref(ce->name, "HEAD", &sub))
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
				     const struct cache_entry *ce,
				     struct stat *st, unsigned ce_option,
				     unsigned *dirty_submodule)
{
	int changed = ie_match_stat(diffopt->repo->index, ce, st, ce_option);
	if (S_ISGITLINK(ce->ce_mode)) {
		struct diff_flags orig_flags = diffopt->flags;
		if (!diffopt->flags.override_submodule_config)
			set_diffopt_flags_from_submodule_config(diffopt, ce->name);
		if (diffopt->flags.ignore_submodules)
			changed = 0;
		else if (!diffopt->flags.ignore_dirty_submodules &&
			 (!changed || diffopt->flags.dirty_submodules))
			*dirty_submodule = is_submodule_modified(ce->name,
								 diffopt->flags.ignore_untracked_in_submodules);
		diffopt->flags = orig_flags;
	}
	return changed;
}

int run_diff_files(struct rev_info *revs, unsigned int option)
{
	int entries, i;
	int diff_unmerged_stage = revs->max_count;
	unsigned ce_option = ((option & DIFF_RACY_IS_MODIFIED)
			      ? CE_MATCH_RACY_IS_DIRTY : 0);
	uint64_t start = getnanotime();
	struct index_state *istate = revs->diffopt.repo->index;

	diff_set_mnemonic_prefix(&revs->diffopt, "i/", "w/");

	refresh_fsmonitor(istate);

	if (diff_unmerged_stage < 0)
		diff_unmerged_stage = 2;
	entries = istate->cache_nr;
	for (i = 0; i < entries; i++) {
		unsigned int oldmode, newmode;
		struct cache_entry *ce = istate->cache[i];
		int changed;
		unsigned dirty_submodule = 0;
		const struct object_id *old_oid, *new_oid;

		if (diff_can_quit_early(&revs->diffopt))
			break;

		if (!ce_path_match(istate, ce, &revs->prune_data, NULL))
			continue;

		if (revs->diffopt.prefix &&
		    strncmp(ce->name, revs->diffopt.prefix, revs->diffopt.prefix_length))
			continue;

		if (ce_stage(ce)) {
			struct combine_diff_path *dpath;
			struct diff_filepair *pair;
			unsigned int wt_mode = 0;
			int num_compare_stages = 0;
			size_t path_len;
			struct stat st;

			path_len = ce_namelen(ce);

			dpath = xmalloc(combine_diff_path_size(5, path_len));
			dpath->path = (char *) &(dpath->parent[5]);

			dpath->next = NULL;
			memcpy(dpath->path, ce->name, path_len);
			dpath->path[path_len] = '\0';
			oidclr(&dpath->oid);
			memset(&(dpath->parent[0]), 0,
			       sizeof(struct combine_diff_parent)*5);

			changed = check_removed(istate, ce, &st);
			if (!changed)
				wt_mode = ce_mode_from_stat(ce, st.st_mode);
			else {
				if (changed < 0) {
					perror(ce->name);
					continue;
				}
				wt_mode = 0;
			}
			dpath->mode = wt_mode;

			while (i < entries) {
				struct cache_entry *nce = istate->cache[i];
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
					oidcpy(&dpath->parent[stage - 2].oid,
					       &nce->oid);
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
				show_combined_diff(dpath, 2, revs);
				free(dpath);
				continue;
			}
			FREE_AND_NULL(dpath);

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

		/*
		 * When CE_VALID is set (via "update-index --assume-unchanged"
		 * or via adding paths while core.ignorestat is set to true),
		 * the user has promised that the working tree file for that
		 * path will not be modified.  When CE_FSMONITOR_VALID is true,
		 * the fsmonitor knows that the path hasn't been modified since
		 * we refreshed the cached stat information.  In either case,
		 * we do not have to stat to see if the path has been removed
		 * or modified.
		 */
		if (ce->ce_flags & (CE_VALID | CE_FSMONITOR_VALID)) {
			changed = 0;
			newmode = ce->ce_mode;
		} else {
			struct stat st;

			changed = check_removed(istate, ce, &st);
			if (changed) {
				if (changed < 0) {
					perror(ce->name);
					continue;
				}
				diff_addremove(&revs->diffopt, '-', ce->ce_mode,
					       &ce->oid,
					       !is_null_oid(&ce->oid),
					       ce->name, 0);
				continue;
			} else if (revs->diffopt.ita_invisible_in_index &&
				   ce_intent_to_add(ce)) {
				newmode = ce_mode_from_stat(ce, st.st_mode);
				diff_addremove(&revs->diffopt, '+', newmode,
					       null_oid(), 0, ce->name, 0);
				continue;
			}

			changed = match_stat_with_submodule(&revs->diffopt, ce, &st,
							    ce_option, &dirty_submodule);
			newmode = ce_mode_from_stat(ce, st.st_mode);
		}

		if (!changed && !dirty_submodule) {
			ce_mark_uptodate(ce);
			mark_fsmonitor_valid(istate, ce);
			if (!revs->diffopt.flags.find_copies_harder)
				continue;
		}
		oldmode = ce->ce_mode;
		old_oid = &ce->oid;
		new_oid = changed ? null_oid() : &ce->oid;
		diff_change(&revs->diffopt, oldmode, newmode,
			    old_oid, new_oid,
			    !is_null_oid(old_oid),
			    !is_null_oid(new_oid),
			    ce->name, 0, dirty_submodule);

	}
	diffcore_std(&revs->diffopt);
	diff_flush(&revs->diffopt);
	trace_performance_since(start, "diff-files");
	return 0;
}

/*
 * diff-index
 */

/* A file entry went away or appeared */
static void diff_index_show_file(struct rev_info *revs,
				 const char *prefix,
				 const struct cache_entry *ce,
				 const struct object_id *oid, int oid_valid,
				 unsigned int mode,
				 unsigned dirty_submodule)
{
	diff_addremove(&revs->diffopt, prefix[0], mode,
		       oid, oid_valid, ce->name, dirty_submodule);
}

static int get_stat_data(const struct index_state *istate,
			 const struct cache_entry *ce,
			 const struct object_id **oidp,
			 unsigned int *modep,
			 int cached, int match_missing,
			 unsigned *dirty_submodule, struct diff_options *diffopt)
{
	const struct object_id *oid = &ce->oid;
	unsigned int mode = ce->ce_mode;

	if (!cached && !ce_uptodate(ce)) {
		int changed;
		struct stat st;
		changed = check_removed(istate, ce, &st);
		if (changed < 0)
			return -1;
		else if (changed) {
			if (match_missing) {
				*oidp = oid;
				*modep = mode;
				return 0;
			}
			return -1;
		}
		changed = match_stat_with_submodule(diffopt, ce, &st,
						    0, dirty_submodule);
		if (changed) {
			mode = ce_mode_from_stat(ce, st.st_mode);
			oid = null_oid();
		}
	}

	*oidp = oid;
	*modep = mode;
	return 0;
}

static void show_new_file(struct rev_info *revs,
			  const struct cache_entry *new_file,
			  int cached, int match_missing)
{
	const struct object_id *oid;
	unsigned int mode;
	unsigned dirty_submodule = 0;
	struct index_state *istate = revs->diffopt.repo->index;

	if (new_file && S_ISSPARSEDIR(new_file->ce_mode)) {
		diff_tree_oid(NULL, &new_file->oid, new_file->name, &revs->diffopt);
		return;
	}

	/*
	 * New file in the index: it might actually be different in
	 * the working tree.
	 */
	if (get_stat_data(istate, new_file, &oid, &mode, cached, match_missing,
	    &dirty_submodule, &revs->diffopt) < 0)
		return;

	diff_index_show_file(revs, "+", new_file, oid, !is_null_oid(oid), mode, dirty_submodule);
}

static int show_modified(struct rev_info *revs,
			 const struct cache_entry *old_entry,
			 const struct cache_entry *new_entry,
			 int report_missing,
			 int cached, int match_missing)
{
	unsigned int mode, oldmode;
	const struct object_id *oid;
	unsigned dirty_submodule = 0;
	struct index_state *istate = revs->diffopt.repo->index;

	assert(S_ISSPARSEDIR(old_entry->ce_mode) ==
	       S_ISSPARSEDIR(new_entry->ce_mode));

	/*
	 * If both are sparse directory entries, then expand the
	 * modifications to the file level. If only one was a sparse
	 * directory, then they appear as an add and delete instead of
	 * a modification.
	 */
	if (S_ISSPARSEDIR(new_entry->ce_mode)) {
		diff_tree_oid(&old_entry->oid, &new_entry->oid, new_entry->name, &revs->diffopt);
		return 0;
	}

	if (get_stat_data(istate, new_entry, &oid, &mode, cached, match_missing,
			  &dirty_submodule, &revs->diffopt) < 0) {
		if (report_missing)
			diff_index_show_file(revs, "-", old_entry,
					     &old_entry->oid, 1, old_entry->ce_mode,
					     0);
		return -1;
	}

	if (revs->combine_merges && !cached &&
	    (!oideq(oid, &old_entry->oid) || !oideq(&old_entry->oid, &new_entry->oid))) {
		struct combine_diff_path *p;
		int pathlen = ce_namelen(new_entry);

		p = xmalloc(combine_diff_path_size(2, pathlen));
		p->path = (char *) &p->parent[2];
		p->next = NULL;
		memcpy(p->path, new_entry->name, pathlen);
		p->path[pathlen] = 0;
		p->mode = mode;
		oidclr(&p->oid);
		memset(p->parent, 0, 2 * sizeof(struct combine_diff_parent));
		p->parent[0].status = DIFF_STATUS_MODIFIED;
		p->parent[0].mode = new_entry->ce_mode;
		oidcpy(&p->parent[0].oid, &new_entry->oid);
		p->parent[1].status = DIFF_STATUS_MODIFIED;
		p->parent[1].mode = old_entry->ce_mode;
		oidcpy(&p->parent[1].oid, &old_entry->oid);
		show_combined_diff(p, 2, revs);
		free(p);
		return 0;
	}

	oldmode = old_entry->ce_mode;
	if (mode == oldmode && oideq(oid, &old_entry->oid) && !dirty_submodule &&
	    !revs->diffopt.flags.find_copies_harder)
		return 0;

	diff_change(&revs->diffopt, oldmode, mode,
		    &old_entry->oid, oid, 1, !is_null_oid(oid),
		    old_entry->name, 0, dirty_submodule);
	return 0;
}

/*
 * This gets a mix of an existing index and a tree, one pathname entry
 * at a time. The index entry may be a single stage-0 one, but it could
 * also be multiple unmerged entries (in which case idx_pos/idx_nr will
 * give you the position and number of entries in the index).
 */
static void do_oneway_diff(struct unpack_trees_options *o,
			   const struct cache_entry *idx,
			   const struct cache_entry *tree)
{
	struct rev_info *revs = o->unpack_data;
	int match_missing, cached;

	/*
	 * i-t-a entries do not actually exist in the index (if we're
	 * looking at its content)
	 */
	if (o->index_only &&
	    revs->diffopt.ita_invisible_in_index &&
	    idx && ce_intent_to_add(idx)) {
		idx = NULL;
		if (!tree)
			return;	/* nothing to diff.. */
	}

	/* if the entry is not checked out, don't examine work tree */
	cached = o->index_only ||
		(idx && ((idx->ce_flags & CE_VALID) || ce_skip_worktree(idx)));

	match_missing = revs->match_missing;

	if (cached && idx && ce_stage(idx)) {
		struct diff_filepair *pair;
		pair = diff_unmerge(&revs->diffopt, idx->name);
		if (tree)
			fill_filespec(pair->one, &tree->oid, 1,
				      tree->ce_mode);
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
		if (S_ISSPARSEDIR(tree->ce_mode)) {
			diff_tree_oid(&tree->oid, NULL, tree->name, &revs->diffopt);
			return;
		}

		diff_index_show_file(revs, "-", tree, &tree->oid, 1,
				     tree->ce_mode, 0);
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
static int oneway_diff(const struct cache_entry * const *src,
		       struct unpack_trees_options *o)
{
	const struct cache_entry *idx = src[0];
	const struct cache_entry *tree = src[1];
	struct rev_info *revs = o->unpack_data;

	/*
	 * Unpack-trees generates a DF/conflict entry if
	 * there was a directory in the index and a tree
	 * in the tree. From a diff standpoint, that's a
	 * delete of the tree and a create of the file.
	 */
	if (tree == o->df_conflict_entry)
		tree = NULL;

	if (ce_path_match(revs->diffopt.repo->index,
			  idx ? idx : tree,
			  &revs->prune_data, NULL)) {
		do_oneway_diff(o, idx, tree);
		if (diff_can_quit_early(&revs->diffopt)) {
			o->exiting_early = 1;
			return -1;
		}
	}

	return 0;
}

static int diff_cache(struct rev_info *revs,
		      const struct object_id *tree_oid,
		      const char *tree_name,
		      int cached)
{
	struct tree *tree;
	struct tree_desc t;
	struct unpack_trees_options opts;

	tree = parse_tree_indirect(tree_oid);
	if (!tree)
		return error("bad tree object %s",
			     tree_name ? tree_name : oid_to_hex(tree_oid));
	memset(&opts, 0, sizeof(opts));
	opts.head_idx = 1;
	opts.index_only = cached;
	opts.diff_index_cached = (cached &&
				  !revs->diffopt.flags.find_copies_harder);
	opts.merge = 1;
	opts.fn = oneway_diff;
	opts.unpack_data = revs;
	opts.src_index = revs->diffopt.repo->index;
	opts.dst_index = NULL;
	opts.pathspec = &revs->diffopt.pathspec;
	opts.pathspec->recursive = 1;

	init_tree_desc(&t, tree->buffer, tree->size);
	return unpack_trees(1, &t, &opts);
}

void diff_get_merge_base(const struct rev_info *revs, struct object_id *mb)
{
	int i;
	struct commit *mb_child[2] = {0};
	struct commit_list *merge_bases;

	for (i = 0; i < revs->pending.nr; i++) {
		struct object *obj = revs->pending.objects[i].item;
		if (obj->flags)
			die(_("--merge-base does not work with ranges"));
		if (obj->type != OBJ_COMMIT)
			die(_("--merge-base only works with commits"));
	}

	/*
	 * This check must go after the for loop above because A...B
	 * ranges produce three pending commits, resulting in a
	 * misleading error message.
	 */
	if (revs->pending.nr < 1 || revs->pending.nr > 2)
		BUG("unexpected revs->pending.nr: %d", revs->pending.nr);

	for (i = 0; i < revs->pending.nr; i++)
		mb_child[i] = lookup_commit_reference(the_repository, &revs->pending.objects[i].item->oid);
	if (revs->pending.nr == 1) {
		struct object_id oid;

		if (repo_get_oid(the_repository, "HEAD", &oid))
			die(_("unable to get HEAD"));

		mb_child[1] = lookup_commit_reference(the_repository, &oid);
	}

	merge_bases = repo_get_merge_bases(the_repository, mb_child[0], mb_child[1]);
	if (!merge_bases)
		die(_("no merge base found"));
	if (merge_bases->next)
		die(_("multiple merge bases found"));

	oidcpy(mb, &merge_bases->item->object.oid);

	free_commit_list(merge_bases);
}

int run_diff_index(struct rev_info *revs, unsigned int option)
{
	struct object_array_entry *ent;
	int cached = !!(option & DIFF_INDEX_CACHED);
	int merge_base = !!(option & DIFF_INDEX_MERGE_BASE);
	struct object_id oid;
	const char *name;
	char merge_base_hex[GIT_MAX_HEXSZ + 1];
	struct index_state *istate = revs->diffopt.repo->index;

	if (revs->pending.nr != 1)
		BUG("run_diff_index must be passed exactly one tree");

	trace_performance_enter();
	ent = revs->pending.objects;

	refresh_fsmonitor(istate);

	if (merge_base) {
		diff_get_merge_base(revs, &oid);
		name = oid_to_hex_r(merge_base_hex, &oid);
	} else {
		oidcpy(&oid, &ent->item->oid);
		name = ent->name;
	}

	if (diff_cache(revs, &oid, name, cached))
		exit(128);

	diff_set_mnemonic_prefix(&revs->diffopt, "c/", cached ? "i/" : "w/");
	diffcore_fix_diff_index();
	diffcore_std(&revs->diffopt);
	diff_flush(&revs->diffopt);
	trace_performance_leave("diff-index");
	return 0;
}

int do_diff_cache(const struct object_id *tree_oid, struct diff_options *opt)
{
	struct rev_info revs;

	repo_init_revisions(opt->repo, &revs, NULL);
	copy_pathspec(&revs.prune_data, &opt->pathspec);
	diff_setup_done(&revs.diffopt);
	revs.diffopt = *opt;

	if (diff_cache(&revs, tree_oid, NULL, 1))
		exit(128);
	release_revisions(&revs);
	return 0;
}

int index_differs_from(struct repository *r,
		       const char *def, const struct diff_flags *flags,
		       int ita_invisible_in_index)
{
	struct rev_info rev;
	struct setup_revision_opt opt;
	unsigned has_changes;

	repo_init_revisions(r, &rev, NULL);
	memset(&opt, 0, sizeof(opt));
	opt.def = def;
	setup_revisions(0, NULL, &rev, &opt);
	rev.diffopt.flags.quick = 1;
	rev.diffopt.flags.exit_with_status = 1;
	if (flags)
		diff_flags_or(&rev.diffopt.flags, flags);
	rev.diffopt.ita_invisible_in_index = ita_invisible_in_index;
	run_diff_index(&rev, 1);
	has_changes = rev.diffopt.flags.has_changes;
	release_revisions(&rev);
	return (has_changes != 0);
}

static struct strbuf *idiff_prefix_cb(struct diff_options *opt UNUSED, void *data)
{
	return data;
}

void show_interdiff(const struct object_id *oid1, const struct object_id *oid2,
		    int indent, struct diff_options *diffopt)
{
	struct diff_options opts;
	struct strbuf prefix = STRBUF_INIT;

	memcpy(&opts, diffopt, sizeof(opts));
	opts.output_format = DIFF_FORMAT_PATCH;
	opts.output_prefix = idiff_prefix_cb;
	strbuf_addchars(&prefix, ' ', indent);
	opts.output_prefix_data = &prefix;
	diff_setup_done(&opts);

	diff_tree_oid(oid1, oid2, "", &opts);
	diffcore_std(&opts);
	diff_flush(&opts);

	strbuf_release(&prefix);
}
