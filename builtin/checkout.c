#define USE_THE_INDEX_VARIABLE
#include "builtin.h"
#include "advice.h"
#include "blob.h"
#include "branch.h"
#include "cache-tree.h"
#include "checkout.h"
#include "commit.h"
#include "config.h"
#include "diff.h"
#include "dir.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "hook.h"
#include "ll-merge.h"
#include "lockfile.h"
#include "mem-pool.h"
#include "merge-recursive.h"
#include "object-name.h"
#include "object-store.h"
#include "parse-options.h"
#include "refs.h"
#include "remote.h"
#include "resolve-undo.h"
#include "revision.h"
#include "run-command.h"
#include "setup.h"
#include "submodule.h"
#include "submodule-config.h"
#include "trace2.h"
#include "tree.h"
#include "tree-walk.h"
#include "unpack-trees.h"
#include "wt-status.h"
#include "xdiff-interface.h"
#include "entry.h"
#include "parallel-checkout.h"
#include "add-interactive.h"

static const char * const checkout_usage[] = {
	N_("git checkout [<options>] <branch>"),
	N_("git checkout [<options>] [<branch>] -- <file>..."),
	NULL,
};

static const char * const switch_branch_usage[] = {
	N_("git switch [<options>] [<branch>]"),
	NULL,
};

static const char * const restore_usage[] = {
	N_("git restore [<options>] [--source=<branch>] <file>..."),
	NULL,
};

struct checkout_opts {
	int patch_mode;
	int quiet;
	int merge;
	int force;
	int force_detach;
	int implicit_detach;
	int writeout_stage;
	int overwrite_ignore;
	int ignore_skipworktree;
	int ignore_other_worktrees;
	int show_progress;
	int count_checkout_paths;
	int overlay_mode;
	int dwim_new_local_branch;
	int discard_changes;
	int accept_ref;
	int accept_pathspec;
	int switch_branch_doing_nothing_is_ok;
	int only_merge_on_switching_branches;
	int can_switch_when_in_progress;
	int orphan_from_empty_tree;
	int empty_pathspec_ok;
	int checkout_index;
	int checkout_worktree;
	const char *ignore_unmerged_opt;
	int ignore_unmerged;
	int pathspec_file_nul;
	char *pathspec_from_file;

	const char *new_branch;
	const char *new_branch_force;
	const char *new_orphan_branch;
	int new_branch_log;
	enum branch_track track;
	struct diff_options diff_options;
	char *conflict_style;

	int branch_exists;
	const char *prefix;
	struct pathspec pathspec;
	const char *from_treeish;
	struct tree *source_tree;
};

struct branch_info {
	char *name; /* The short name used */
	char *path; /* The full name of a real branch */
	struct commit *commit; /* The named commit */
	char *refname; /* The full name of the ref being checked out. */
	struct object_id oid; /* The object ID of the commit being checked out. */
	/*
	 * if not null the branch is detached because it's already
	 * checked out in this checkout
	 */
	char *checkout;
};

static void branch_info_release(struct branch_info *info)
{
	free(info->name);
	free(info->path);
	free(info->refname);
	free(info->checkout);
}

static int post_checkout_hook(struct commit *old_commit, struct commit *new_commit,
			      int changed)
{
	return run_hooks_l("post-checkout",
			   oid_to_hex(old_commit ? &old_commit->object.oid : null_oid()),
			   oid_to_hex(new_commit ? &new_commit->object.oid : null_oid()),
			   changed ? "1" : "0", NULL);
	/* "new_commit" can be NULL when checking out from the index before
	   a commit exists. */

}

static int update_some(const struct object_id *oid, struct strbuf *base,
		       const char *pathname, unsigned mode, void *context UNUSED)
{
	int len;
	struct cache_entry *ce;
	int pos;

	if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	len = base->len + strlen(pathname);
	ce = make_empty_cache_entry(&the_index, len);
	oidcpy(&ce->oid, oid);
	memcpy(ce->name, base->buf, base->len);
	memcpy(ce->name + base->len, pathname, len - base->len);
	ce->ce_flags = create_ce_flags(0) | CE_UPDATE;
	ce->ce_namelen = len;
	ce->ce_mode = create_ce_mode(mode);

	/*
	 * If the entry is the same as the current index, we can leave the old
	 * entry in place. Whether it is UPTODATE or not, checkout_entry will
	 * do the right thing.
	 */
	pos = index_name_pos(&the_index, ce->name, ce->ce_namelen);
	if (pos >= 0) {
		struct cache_entry *old = the_index.cache[pos];
		if (ce->ce_mode == old->ce_mode &&
		    !ce_intent_to_add(old) &&
		    oideq(&ce->oid, &old->oid)) {
			old->ce_flags |= CE_UPDATE;
			discard_cache_entry(ce);
			return 0;
		}
	}

	add_index_entry(&the_index, ce,
			ADD_CACHE_OK_TO_ADD | ADD_CACHE_OK_TO_REPLACE);
	return 0;
}

static int read_tree_some(struct tree *tree, const struct pathspec *pathspec)
{
	read_tree(the_repository, tree,
		  pathspec, update_some, NULL);

	/* update the index with the given tree's info
	 * for all args, expanding wildcards, and exit
	 * with any non-zero return code.
	 */
	return 0;
}

static int skip_same_name(const struct cache_entry *ce, int pos)
{
	while (++pos < the_index.cache_nr &&
	       !strcmp(the_index.cache[pos]->name, ce->name))
		; /* skip */
	return pos;
}

static int check_stage(int stage, const struct cache_entry *ce, int pos,
		       int overlay_mode)
{
	while (pos < the_index.cache_nr &&
	       !strcmp(the_index.cache[pos]->name, ce->name)) {
		if (ce_stage(the_index.cache[pos]) == stage)
			return 0;
		pos++;
	}
	if (!overlay_mode)
		return 0;
	if (stage == 2)
		return error(_("path '%s' does not have our version"), ce->name);
	else
		return error(_("path '%s' does not have their version"), ce->name);
}

static int check_stages(unsigned stages, const struct cache_entry *ce, int pos)
{
	unsigned seen = 0;
	const char *name = ce->name;

	while (pos < the_index.cache_nr) {
		ce = the_index.cache[pos];
		if (strcmp(name, ce->name))
			break;
		seen |= (1 << ce_stage(ce));
		pos++;
	}
	if ((stages & seen) != stages)
		return error(_("path '%s' does not have all necessary versions"),
			     name);
	return 0;
}

static int checkout_stage(int stage, const struct cache_entry *ce, int pos,
			  const struct checkout *state, int *nr_checkouts,
			  int overlay_mode)
{
	while (pos < the_index.cache_nr &&
	       !strcmp(the_index.cache[pos]->name, ce->name)) {
		if (ce_stage(the_index.cache[pos]) == stage)
			return checkout_entry(the_index.cache[pos], state,
					      NULL, nr_checkouts);
		pos++;
	}
	if (!overlay_mode) {
		unlink_entry(ce, NULL);
		return 0;
	}
	if (stage == 2)
		return error(_("path '%s' does not have our version"), ce->name);
	else
		return error(_("path '%s' does not have their version"), ce->name);
}

static int checkout_merged(int pos, const struct checkout *state,
			   int *nr_checkouts, struct mem_pool *ce_mem_pool)
{
	struct cache_entry *ce = the_index.cache[pos];
	const char *path = ce->name;
	mmfile_t ancestor, ours, theirs;
	enum ll_merge_result merge_status;
	int status;
	struct object_id oid;
	mmbuffer_t result_buf;
	struct object_id threeway[3];
	unsigned mode = 0;
	struct ll_merge_options ll_opts;
	int renormalize = 0;

	memset(threeway, 0, sizeof(threeway));
	while (pos < the_index.cache_nr) {
		int stage;
		stage = ce_stage(ce);
		if (!stage || strcmp(path, ce->name))
			break;
		oidcpy(&threeway[stage - 1], &ce->oid);
		if (stage == 2)
			mode = create_ce_mode(ce->ce_mode);
		pos++;
		ce = the_index.cache[pos];
	}
	if (is_null_oid(&threeway[1]) || is_null_oid(&threeway[2]))
		return error(_("path '%s' does not have necessary versions"), path);

	read_mmblob(&ancestor, &threeway[0]);
	read_mmblob(&ours, &threeway[1]);
	read_mmblob(&theirs, &threeway[2]);

	memset(&ll_opts, 0, sizeof(ll_opts));
	git_config_get_bool("merge.renormalize", &renormalize);
	ll_opts.renormalize = renormalize;
	merge_status = ll_merge(&result_buf, path, &ancestor, "base",
				&ours, "ours", &theirs, "theirs",
				state->istate, &ll_opts);
	free(ancestor.ptr);
	free(ours.ptr);
	free(theirs.ptr);
	if (merge_status == LL_MERGE_BINARY_CONFLICT)
		warning("Cannot merge binary files: %s (%s vs. %s)",
			path, "ours", "theirs");
	if (merge_status < 0 || !result_buf.ptr) {
		free(result_buf.ptr);
		return error(_("path '%s': cannot merge"), path);
	}

	/*
	 * NEEDSWORK:
	 * There is absolutely no reason to write this as a blob object
	 * and create a phony cache entry.  This hack is primarily to get
	 * to the write_entry() machinery that massages the contents to
	 * work-tree format and writes out which only allows it for a
	 * cache entry.  The code in write_entry() needs to be refactored
	 * to allow us to feed a <buffer, size, mode> instead of a cache
	 * entry.  Such a refactoring would help merge_recursive as well
	 * (it also writes the merge result to the object database even
	 * when it may contain conflicts).
	 */
	if (write_object_file(result_buf.ptr, result_buf.size, OBJ_BLOB, &oid))
		die(_("Unable to add merge result for '%s'"), path);
	free(result_buf.ptr);
	ce = make_transient_cache_entry(mode, &oid, path, 2, ce_mem_pool);
	if (!ce)
		die(_("make_cache_entry failed for path '%s'"), path);
	status = checkout_entry(ce, state, NULL, nr_checkouts);
	return status;
}

static void mark_ce_for_checkout_overlay(struct cache_entry *ce,
					 char *ps_matched,
					 const struct checkout_opts *opts)
{
	ce->ce_flags &= ~CE_MATCHED;
	if (!opts->ignore_skipworktree && ce_skip_worktree(ce))
		return;
	if (opts->source_tree && !(ce->ce_flags & CE_UPDATE))
		/*
		 * "git checkout tree-ish -- path", but this entry
		 * is in the original index but is not in tree-ish
		 * or does not match the pathspec; it will not be
		 * checked out to the working tree.  We will not do
		 * anything to this entry at all.
		 */
		return;
	/*
	 * Either this entry came from the tree-ish we are
	 * checking the paths out of, or we are checking out
	 * of the index.
	 *
	 * If it comes from the tree-ish, we already know it
	 * matches the pathspec and could just stamp
	 * CE_MATCHED to it from update_some(). But we still
	 * need ps_matched and read_tree (and
	 * eventually tree_entry_interesting) cannot fill
	 * ps_matched yet. Once it can, we can avoid calling
	 * match_pathspec() for _all_ entries when
	 * opts->source_tree != NULL.
	 */
	if (ce_path_match(&the_index, ce, &opts->pathspec, ps_matched))
		ce->ce_flags |= CE_MATCHED;
}

static void mark_ce_for_checkout_no_overlay(struct cache_entry *ce,
					    char *ps_matched,
					    const struct checkout_opts *opts)
{
	ce->ce_flags &= ~CE_MATCHED;
	if (!opts->ignore_skipworktree && ce_skip_worktree(ce))
		return;
	if (ce_path_match(&the_index, ce, &opts->pathspec, ps_matched)) {
		ce->ce_flags |= CE_MATCHED;
		if (opts->source_tree && !(ce->ce_flags & CE_UPDATE))
			/*
			 * In overlay mode, but the path is not in
			 * tree-ish, which means we should remove it
			 * from the index and the working tree.
			 */
			ce->ce_flags |= CE_REMOVE | CE_WT_REMOVE;
	}
}

static int checkout_worktree(const struct checkout_opts *opts,
			     const struct branch_info *info)
{
	struct checkout state = CHECKOUT_INIT;
	int nr_checkouts = 0, nr_unmerged = 0;
	int errs = 0;
	int pos;
	int pc_workers, pc_threshold;
	struct mem_pool ce_mem_pool;

	state.force = 1;
	state.refresh_cache = 1;
	state.istate = &the_index;

	mem_pool_init(&ce_mem_pool, 0);
	get_parallel_checkout_configs(&pc_workers, &pc_threshold);
	init_checkout_metadata(&state.meta, info->refname,
			       info->commit ? &info->commit->object.oid : &info->oid,
			       NULL);

	enable_delayed_checkout(&state);

	if (pc_workers > 1)
		init_parallel_checkout();

	for (pos = 0; pos < the_index.cache_nr; pos++) {
		struct cache_entry *ce = the_index.cache[pos];
		if (ce->ce_flags & CE_MATCHED) {
			if (!ce_stage(ce)) {
				errs |= checkout_entry(ce, &state,
						       NULL, &nr_checkouts);
				continue;
			}
			if (opts->writeout_stage)
				errs |= checkout_stage(opts->writeout_stage,
						       ce, pos,
						       &state,
						       &nr_checkouts, opts->overlay_mode);
			else if (opts->merge)
				errs |= checkout_merged(pos, &state,
							&nr_unmerged,
							&ce_mem_pool);
			pos = skip_same_name(ce, pos) - 1;
		}
	}
	if (pc_workers > 1)
		errs |= run_parallel_checkout(&state, pc_workers, pc_threshold,
					      NULL, NULL);
	mem_pool_discard(&ce_mem_pool, should_validate_cache_entries());
	remove_marked_cache_entries(&the_index, 1);
	remove_scheduled_dirs();
	errs |= finish_delayed_checkout(&state, opts->show_progress);

	if (opts->count_checkout_paths) {
		if (nr_unmerged)
			fprintf_ln(stderr, Q_("Recreated %d merge conflict",
					      "Recreated %d merge conflicts",
					      nr_unmerged),
				   nr_unmerged);
		if (opts->source_tree)
			fprintf_ln(stderr, Q_("Updated %d path from %s",
					      "Updated %d paths from %s",
					      nr_checkouts),
				   nr_checkouts,
				   repo_find_unique_abbrev(the_repository, &opts->source_tree->object.oid,
							   DEFAULT_ABBREV));
		else if (!nr_unmerged || nr_checkouts)
			fprintf_ln(stderr, Q_("Updated %d path from the index",
					      "Updated %d paths from the index",
					      nr_checkouts),
				   nr_checkouts);
	}

	return errs;
}

static int checkout_paths(const struct checkout_opts *opts,
			  const struct branch_info *new_branch_info)
{
	int pos;
	static char *ps_matched;
	struct object_id rev;
	struct commit *head;
	int errs = 0;
	struct lock_file lock_file = LOCK_INIT;
	int checkout_index;

	trace2_cmd_mode(opts->patch_mode ? "patch" : "path");

	if (opts->track != BRANCH_TRACK_UNSPECIFIED)
		die(_("'%s' cannot be used with updating paths"), "--track");

	if (opts->new_branch_log)
		die(_("'%s' cannot be used with updating paths"), "-l");

	if (opts->ignore_unmerged && opts->patch_mode)
		die(_("'%s' cannot be used with updating paths"),
		    opts->ignore_unmerged_opt);

	if (opts->force_detach)
		die(_("'%s' cannot be used with updating paths"), "--detach");

	if (opts->merge && opts->patch_mode)
		die(_("options '%s' and '%s' cannot be used together"), "--merge", "--patch");

	if (opts->ignore_unmerged && opts->merge)
		die(_("options '%s' and '%s' cannot be used together"),
		    opts->ignore_unmerged_opt, "-m");

	if (opts->new_branch)
		die(_("Cannot update paths and switch to branch '%s' at the same time."),
		    opts->new_branch);

	if (!opts->checkout_worktree && !opts->checkout_index)
		die(_("neither '%s' or '%s' is specified"),
		    "--staged", "--worktree");

	if (!opts->checkout_worktree && !opts->from_treeish)
		die(_("'%s' must be used when '%s' is not specified"),
		    "--worktree", "--source");

	/*
	 * Reject --staged option to the restore command when combined with
	 * merge-related options. Use the accept_ref flag to distinguish it
	 * from the checkout command, which does not accept --staged anyway.
	 *
	 * `restore --ours|--theirs --worktree --staged` could mean resolving
	 * conflicted paths to one side in both the worktree and the index,
	 * but does not currently.
	 *
	 * `restore --merge|--conflict=<style>` already recreates conflicts
	 * in both the worktree and the index, so adding --staged would be
	 * meaningless.
	 */
	if (!opts->accept_ref && opts->checkout_index) {
		if (opts->writeout_stage)
			die(_("'%s' or '%s' cannot be used with %s"),
			    "--ours", "--theirs", "--staged");

		if (opts->merge)
			die(_("'%s' or '%s' cannot be used with %s"),
			    "--merge", "--conflict", "--staged");
	}

	if (opts->patch_mode) {
		enum add_p_mode patch_mode;
		const char *rev = new_branch_info->name;
		char rev_oid[GIT_MAX_HEXSZ + 1];

		/*
		 * Since rev can be in the form of `<a>...<b>` (which is not
		 * recognized by diff-index), we will always replace the name
		 * with the hex of the commit (whether it's in `...` form or
		 * not) for the run_add_interactive() machinery to work
		 * properly. However, there is special logic for the HEAD case
		 * so we mustn't replace that.  Also, when we were given a
		 * tree-object, new_branch_info->commit would be NULL, but we
		 * do not have to do any replacement, either.
		 */
		if (rev && new_branch_info->commit && strcmp(rev, "HEAD"))
			rev = oid_to_hex_r(rev_oid, &new_branch_info->commit->object.oid);

		if (opts->checkout_index && opts->checkout_worktree)
			patch_mode = ADD_P_CHECKOUT;
		else if (opts->checkout_index && !opts->checkout_worktree)
			patch_mode = ADD_P_RESET;
		else if (!opts->checkout_index && opts->checkout_worktree)
			patch_mode = ADD_P_WORKTREE;
		else
			BUG("either flag must have been set, worktree=%d, index=%d",
			    opts->checkout_worktree, opts->checkout_index);
		return !!run_add_p(the_repository, patch_mode, rev,
				   &opts->pathspec);
	}

	repo_hold_locked_index(the_repository, &lock_file, LOCK_DIE_ON_ERROR);
	if (repo_read_index_preload(the_repository, &opts->pathspec, 0) < 0)
		return error(_("index file corrupt"));

	if (opts->source_tree)
		read_tree_some(opts->source_tree, &opts->pathspec);

	ps_matched = xcalloc(opts->pathspec.nr, 1);

	/*
	 * Make sure all pathspecs participated in locating the paths
	 * to be checked out.
	 */
	for (pos = 0; pos < the_index.cache_nr; pos++)
		if (opts->overlay_mode)
			mark_ce_for_checkout_overlay(the_index.cache[pos],
						     ps_matched,
						     opts);
		else
			mark_ce_for_checkout_no_overlay(the_index.cache[pos],
							ps_matched,
							opts);

	if (report_path_error(ps_matched, &opts->pathspec)) {
		free(ps_matched);
		return 1;
	}
	free(ps_matched);

	/* "checkout -m path" to recreate conflicted state */
	if (opts->merge)
		unmerge_marked_index(&the_index);

	/* Any unmerged paths? */
	for (pos = 0; pos < the_index.cache_nr; pos++) {
		const struct cache_entry *ce = the_index.cache[pos];
		if (ce->ce_flags & CE_MATCHED) {
			if (!ce_stage(ce))
				continue;
			if (opts->ignore_unmerged) {
				if (!opts->quiet)
					warning(_("path '%s' is unmerged"), ce->name);
			} else if (opts->writeout_stage) {
				errs |= check_stage(opts->writeout_stage, ce, pos, opts->overlay_mode);
			} else if (opts->merge) {
				errs |= check_stages((1<<2) | (1<<3), ce, pos);
			} else {
				errs = 1;
				error(_("path '%s' is unmerged"), ce->name);
			}
			pos = skip_same_name(ce, pos) - 1;
		}
	}
	if (errs)
		return 1;

	/* Now we are committed to check them out */
	if (opts->checkout_worktree)
		errs |= checkout_worktree(opts, new_branch_info);
	else
		remove_marked_cache_entries(&the_index, 1);

	/*
	 * Allow updating the index when checking out from the index.
	 * This is to save new stat info.
	 */
	if (opts->checkout_worktree && !opts->checkout_index && !opts->source_tree)
		checkout_index = 1;
	else
		checkout_index = opts->checkout_index;

	if (checkout_index) {
		if (write_locked_index(&the_index, &lock_file, COMMIT_LOCK))
			die(_("unable to write new index file"));
	} else {
		/*
		 * NEEDSWORK: if --worktree is not specified, we
		 * should save stat info of checked out files in the
		 * index to avoid the next (potentially costly)
		 * refresh. But it's a bit tricker to do...
		 */
		rollback_lock_file(&lock_file);
	}

	read_ref_full("HEAD", 0, &rev, NULL);
	head = lookup_commit_reference_gently(the_repository, &rev, 1);

	errs |= post_checkout_hook(head, head, 0);
	return errs;
}

static void show_local_changes(struct object *head,
			       const struct diff_options *opts)
{
	struct rev_info rev;
	/* I think we want full paths, even if we're in a subdirectory. */
	repo_init_revisions(the_repository, &rev, NULL);
	rev.diffopt.flags = opts->flags;
	rev.diffopt.output_format |= DIFF_FORMAT_NAME_STATUS;
	rev.diffopt.flags.recursive = 1;
	diff_setup_done(&rev.diffopt);
	add_pending_object(&rev, head, NULL);
	run_diff_index(&rev, 0);
	release_revisions(&rev);
}

static void describe_detached_head(const char *msg, struct commit *commit)
{
	struct strbuf sb = STRBUF_INIT;

	if (!repo_parse_commit(the_repository, commit))
		pp_commit_easy(CMIT_FMT_ONELINE, commit, &sb);
	if (print_sha1_ellipsis()) {
		fprintf(stderr, "%s %s... %s\n", msg,
			repo_find_unique_abbrev(the_repository, &commit->object.oid, DEFAULT_ABBREV),
			sb.buf);
	} else {
		fprintf(stderr, "%s %s %s\n", msg,
			repo_find_unique_abbrev(the_repository, &commit->object.oid, DEFAULT_ABBREV),
			sb.buf);
	}
	strbuf_release(&sb);
}

static int reset_tree(struct tree *tree, const struct checkout_opts *o,
		      int worktree, int *writeout_error,
		      struct branch_info *info)
{
	struct unpack_trees_options opts;
	struct tree_desc tree_desc;

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = -1;
	opts.update = worktree;
	opts.skip_unmerged = !worktree;
	opts.reset = o->force ? UNPACK_RESET_OVERWRITE_UNTRACKED :
				UNPACK_RESET_PROTECT_UNTRACKED;
	opts.preserve_ignored = (!o->force && !o->overwrite_ignore);
	opts.merge = 1;
	opts.fn = oneway_merge;
	opts.verbose_update = o->show_progress;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
	init_checkout_metadata(&opts.meta, info->refname,
			       info->commit ? &info->commit->object.oid : null_oid(),
			       NULL);
	parse_tree(tree);
	init_tree_desc(&tree_desc, tree->buffer, tree->size);
	switch (unpack_trees(1, &tree_desc, &opts)) {
	case -2:
		*writeout_error = 1;
		/*
		 * We return 0 nevertheless, as the index is all right
		 * and more importantly we have made best efforts to
		 * update paths in the work tree, and we cannot revert
		 * them.
		 */
		/* fallthrough */
	case 0:
		return 0;
	default:
		return 128;
	}
}

static void setup_branch_path(struct branch_info *branch)
{
	struct strbuf buf = STRBUF_INIT;

	/*
	 * If this is a ref, resolve it; otherwise, look up the OID for our
	 * expression.  Failure here is okay.
	 */
	if (!repo_dwim_ref(the_repository, branch->name, strlen(branch->name),
			   &branch->oid, &branch->refname, 0))
		repo_get_oid_committish(the_repository, branch->name, &branch->oid);

	strbuf_branchname(&buf, branch->name, INTERPRET_BRANCH_LOCAL);
	if (strcmp(buf.buf, branch->name)) {
		free(branch->name);
		branch->name = xstrdup(buf.buf);
	}
	strbuf_splice(&buf, 0, 0, "refs/heads/", 11);
	free(branch->path);
	branch->path = strbuf_detach(&buf, NULL);
}

static void init_topts(struct unpack_trees_options *topts, int merge,
		       int show_progress, int overwrite_ignore,
		       struct commit *old_commit)
{
	memset(topts, 0, sizeof(*topts));
	topts->head_idx = -1;
	topts->src_index = &the_index;
	topts->dst_index = &the_index;

	setup_unpack_trees_porcelain(topts, "checkout");

	topts->initial_checkout = is_index_unborn(&the_index);
	topts->update = 1;
	topts->merge = 1;
	topts->quiet = merge && old_commit;
	topts->verbose_update = show_progress;
	topts->fn = twoway_merge;
	topts->preserve_ignored = !overwrite_ignore;
}

static int merge_working_tree(const struct checkout_opts *opts,
			      struct branch_info *old_branch_info,
			      struct branch_info *new_branch_info,
			      int *writeout_error)
{
	int ret;
	struct lock_file lock_file = LOCK_INIT;
	struct tree *new_tree;

	repo_hold_locked_index(the_repository, &lock_file, LOCK_DIE_ON_ERROR);
	if (repo_read_index_preload(the_repository, NULL, 0) < 0)
		return error(_("index file corrupt"));

	resolve_undo_clear_index(&the_index);
	if (opts->new_orphan_branch && opts->orphan_from_empty_tree) {
		if (new_branch_info->commit)
			BUG("'switch --orphan' should never accept a commit as starting point");
		new_tree = parse_tree_indirect(the_hash_algo->empty_tree);
	} else
		new_tree = repo_get_commit_tree(the_repository,
						new_branch_info->commit);
	if (opts->discard_changes) {
		ret = reset_tree(new_tree, opts, 1, writeout_error, new_branch_info);
		if (ret)
			return ret;
	} else {
		struct tree_desc trees[2];
		struct tree *tree;
		struct unpack_trees_options topts;
		const struct object_id *old_commit_oid;

		refresh_index(&the_index, REFRESH_QUIET, NULL, NULL, NULL);

		if (unmerged_index(&the_index)) {
			error(_("you need to resolve your current index first"));
			return 1;
		}

		/* 2-way merge to the new branch */
		init_topts(&topts, opts->merge, opts->show_progress,
			   opts->overwrite_ignore, old_branch_info->commit);
		init_checkout_metadata(&topts.meta, new_branch_info->refname,
				       new_branch_info->commit ?
				       &new_branch_info->commit->object.oid :
				       &new_branch_info->oid, NULL);

		old_commit_oid = old_branch_info->commit ?
			&old_branch_info->commit->object.oid :
			the_hash_algo->empty_tree;
		tree = parse_tree_indirect(old_commit_oid);
		if (!tree)
			die(_("unable to parse commit %s"),
				oid_to_hex(old_commit_oid));

		init_tree_desc(&trees[0], tree->buffer, tree->size);
		parse_tree(new_tree);
		tree = new_tree;
		init_tree_desc(&trees[1], tree->buffer, tree->size);

		ret = unpack_trees(2, trees, &topts);
		clear_unpack_trees_porcelain(&topts);
		if (ret == -1) {
			/*
			 * Unpack couldn't do a trivial merge; either
			 * give up or do a real merge, depending on
			 * whether the merge flag was used.
			 */
			struct tree *work;
			struct tree *old_tree;
			struct merge_options o;
			struct strbuf sb = STRBUF_INIT;
			struct strbuf old_commit_shortname = STRBUF_INIT;

			if (!opts->merge)
				return 1;

			/*
			 * Without old_branch_info->commit, the below is the same as
			 * the two-tree unpack we already tried and failed.
			 */
			if (!old_branch_info->commit)
				return 1;
			old_tree = repo_get_commit_tree(the_repository,
							old_branch_info->commit);

			if (repo_index_has_changes(the_repository, old_tree, &sb))
				die(_("cannot continue with staged changes in "
				      "the following files:\n%s"), sb.buf);
			strbuf_release(&sb);

			/* Do more real merge */

			/*
			 * We update the index fully, then write the
			 * tree from the index, then merge the new
			 * branch with the current tree, with the old
			 * branch as the base. Then we reset the index
			 * (but not the working tree) to the new
			 * branch, leaving the working tree as the
			 * merged version, but skipping unmerged
			 * entries in the index.
			 */

			add_files_to_cache(NULL, NULL, 0);
			init_merge_options(&o, the_repository);
			o.verbosity = 0;
			work = write_in_core_index_as_tree(the_repository);

			ret = reset_tree(new_tree,
					 opts, 1,
					 writeout_error, new_branch_info);
			if (ret)
				return ret;
			o.ancestor = old_branch_info->name;
			if (!old_branch_info->name) {
				strbuf_add_unique_abbrev(&old_commit_shortname,
							 &old_branch_info->commit->object.oid,
							 DEFAULT_ABBREV);
				o.ancestor = old_commit_shortname.buf;
			}
			o.branch1 = new_branch_info->name;
			o.branch2 = "local";
			ret = merge_trees(&o,
					  new_tree,
					  work,
					  old_tree);
			if (ret < 0)
				exit(128);
			ret = reset_tree(new_tree,
					 opts, 0,
					 writeout_error, new_branch_info);
			strbuf_release(&o.obuf);
			strbuf_release(&old_commit_shortname);
			if (ret)
				return ret;
		}
	}

	if (!cache_tree_fully_valid(the_index.cache_tree))
		cache_tree_update(&the_index, WRITE_TREE_SILENT | WRITE_TREE_REPAIR);

	if (write_locked_index(&the_index, &lock_file, COMMIT_LOCK))
		die(_("unable to write new index file"));

	if (!opts->discard_changes && !opts->quiet && new_branch_info->commit)
		show_local_changes(&new_branch_info->commit->object, &opts->diff_options);

	return 0;
}

static void report_tracking(struct branch_info *new_branch_info)
{
	struct strbuf sb = STRBUF_INIT;
	struct branch *branch = branch_get(new_branch_info->name);

	if (!format_tracking_info(branch, &sb, AHEAD_BEHIND_FULL))
		return;
	fputs(sb.buf, stdout);
	strbuf_release(&sb);
}

static void update_refs_for_switch(const struct checkout_opts *opts,
				   struct branch_info *old_branch_info,
				   struct branch_info *new_branch_info)
{
	struct strbuf msg = STRBUF_INIT;
	const char *old_desc, *reflog_msg;
	if (opts->new_branch) {
		if (opts->new_orphan_branch) {
			char *refname;

			refname = mkpathdup("refs/heads/%s", opts->new_orphan_branch);
			if (opts->new_branch_log &&
			    !should_autocreate_reflog(refname)) {
				int ret;
				struct strbuf err = STRBUF_INIT;

				ret = safe_create_reflog(refname, &err);
				if (ret) {
					fprintf(stderr, _("Can not do reflog for '%s': %s\n"),
						opts->new_orphan_branch, err.buf);
					strbuf_release(&err);
					free(refname);
					return;
				}
				strbuf_release(&err);
			}
			free(refname);
		}
		else
			create_branch(the_repository,
				      opts->new_branch, new_branch_info->name,
				      opts->new_branch_force ? 1 : 0,
				      opts->new_branch_force ? 1 : 0,
				      opts->new_branch_log,
				      opts->quiet,
				      opts->track,
				      0);
		free(new_branch_info->name);
		free(new_branch_info->refname);
		new_branch_info->name = xstrdup(opts->new_branch);
		setup_branch_path(new_branch_info);
	}

	old_desc = old_branch_info->name;
	if (!old_desc && old_branch_info->commit)
		old_desc = oid_to_hex(&old_branch_info->commit->object.oid);

	reflog_msg = getenv("GIT_REFLOG_ACTION");
	if (!reflog_msg)
		strbuf_addf(&msg, "checkout: moving from %s to %s",
			old_desc ? old_desc : "(invalid)", new_branch_info->name);
	else
		strbuf_insertstr(&msg, 0, reflog_msg);

	if (!strcmp(new_branch_info->name, "HEAD") && !new_branch_info->path && !opts->force_detach) {
		/* Nothing to do. */
	} else if (opts->force_detach || !new_branch_info->path) {	/* No longer on any branch. */
		update_ref(msg.buf, "HEAD", &new_branch_info->commit->object.oid, NULL,
			   REF_NO_DEREF, UPDATE_REFS_DIE_ON_ERR);
		if (!opts->quiet) {
			if (old_branch_info->path &&
			    advice_enabled(ADVICE_DETACHED_HEAD) && !opts->force_detach)
				detach_advice(new_branch_info->name);
			describe_detached_head(_("HEAD is now at"), new_branch_info->commit);
		}
	} else if (new_branch_info->path) {	/* Switch branches. */
		if (create_symref("HEAD", new_branch_info->path, msg.buf) < 0)
			die(_("unable to update HEAD"));
		if (!opts->quiet) {
			if (old_branch_info->path && !strcmp(new_branch_info->path, old_branch_info->path)) {
				if (opts->new_branch_force)
					fprintf(stderr, _("Reset branch '%s'\n"),
						new_branch_info->name);
				else
					fprintf(stderr, _("Already on '%s'\n"),
						new_branch_info->name);
			} else if (opts->new_branch) {
				if (opts->branch_exists)
					fprintf(stderr, _("Switched to and reset branch '%s'\n"), new_branch_info->name);
				else
					fprintf(stderr, _("Switched to a new branch '%s'\n"), new_branch_info->name);
			} else {
				fprintf(stderr, _("Switched to branch '%s'\n"),
					new_branch_info->name);
			}
		}
		if (old_branch_info->path && old_branch_info->name) {
			if (!ref_exists(old_branch_info->path) && reflog_exists(old_branch_info->path))
				delete_reflog(old_branch_info->path);
		}
	}
	remove_branch_state(the_repository, !opts->quiet);
	strbuf_release(&msg);
	if (!opts->quiet &&
	    (new_branch_info->path || (!opts->force_detach && !strcmp(new_branch_info->name, "HEAD"))))
		report_tracking(new_branch_info);
}

static int add_pending_uninteresting_ref(const char *refname,
					 const struct object_id *oid,
					 int flags UNUSED, void *cb_data)
{
	add_pending_oid(cb_data, refname, oid, UNINTERESTING);
	return 0;
}

static void describe_one_orphan(struct strbuf *sb, struct commit *commit)
{
	strbuf_addstr(sb, "  ");
	strbuf_add_unique_abbrev(sb, &commit->object.oid, DEFAULT_ABBREV);
	strbuf_addch(sb, ' ');
	if (!repo_parse_commit(the_repository, commit))
		pp_commit_easy(CMIT_FMT_ONELINE, commit, sb);
	strbuf_addch(sb, '\n');
}

#define ORPHAN_CUTOFF 4
static void suggest_reattach(struct commit *commit, struct rev_info *revs)
{
	struct commit *c, *last = NULL;
	struct strbuf sb = STRBUF_INIT;
	int lost = 0;
	while ((c = get_revision(revs)) != NULL) {
		if (lost < ORPHAN_CUTOFF)
			describe_one_orphan(&sb, c);
		last = c;
		lost++;
	}
	if (ORPHAN_CUTOFF < lost) {
		int more = lost - ORPHAN_CUTOFF;
		if (more == 1)
			describe_one_orphan(&sb, last);
		else
			strbuf_addf(&sb, _(" ... and %d more.\n"), more);
	}

	fprintf(stderr,
		Q_(
		/* The singular version */
		"Warning: you are leaving %d commit behind, "
		"not connected to\n"
		"any of your branches:\n\n"
		"%s\n",
		/* The plural version */
		"Warning: you are leaving %d commits behind, "
		"not connected to\n"
		"any of your branches:\n\n"
		"%s\n",
		/* Give ngettext() the count */
		lost),
		lost,
		sb.buf);
	strbuf_release(&sb);

	if (advice_enabled(ADVICE_DETACHED_HEAD))
		fprintf(stderr,
			Q_(
			/* The singular version */
			"If you want to keep it by creating a new branch, "
			"this may be a good time\nto do so with:\n\n"
			" git branch <new-branch-name> %s\n\n",
			/* The plural version */
			"If you want to keep them by creating a new branch, "
			"this may be a good time\nto do so with:\n\n"
			" git branch <new-branch-name> %s\n\n",
			/* Give ngettext() the count */
			lost),
			repo_find_unique_abbrev(the_repository, &commit->object.oid, DEFAULT_ABBREV));
}

/*
 * We are about to leave commit that was at the tip of a detached
 * HEAD.  If it is not reachable from any ref, this is the last chance
 * for the user to do so without resorting to reflog.
 */
static void orphaned_commit_warning(struct commit *old_commit, struct commit *new_commit)
{
	struct rev_info revs;
	struct object *object = &old_commit->object;

	repo_init_revisions(the_repository, &revs, NULL);
	setup_revisions(0, NULL, &revs, NULL);

	object->flags &= ~UNINTERESTING;
	add_pending_object(&revs, object, oid_to_hex(&object->oid));

	for_each_ref(add_pending_uninteresting_ref, &revs);
	if (new_commit)
		add_pending_oid(&revs, "HEAD",
				&new_commit->object.oid,
				UNINTERESTING);

	if (prepare_revision_walk(&revs))
		die(_("internal error in revision walk"));
	if (!(old_commit->object.flags & UNINTERESTING))
		suggest_reattach(old_commit, &revs);
	else
		describe_detached_head(_("Previous HEAD position was"), old_commit);

	/* Clean up objects used, as they will be reused. */
	repo_clear_commit_marks(the_repository, ALL_REV_FLAGS);
	release_revisions(&revs);
}

static int switch_branches(const struct checkout_opts *opts,
			   struct branch_info *new_branch_info)
{
	int ret = 0;
	struct branch_info old_branch_info = { 0 };
	struct object_id rev;
	int flag, writeout_error = 0;
	int do_merge = 1;

	trace2_cmd_mode("branch");

	memset(&old_branch_info, 0, sizeof(old_branch_info));
	old_branch_info.path = resolve_refdup("HEAD", 0, &rev, &flag);
	if (old_branch_info.path)
		old_branch_info.commit = lookup_commit_reference_gently(the_repository, &rev, 1);
	if (!(flag & REF_ISSYMREF))
		FREE_AND_NULL(old_branch_info.path);

	if (old_branch_info.path) {
		const char *const prefix = "refs/heads/";
		const char *p;
		if (skip_prefix(old_branch_info.path, prefix, &p))
			old_branch_info.name = xstrdup(p);
	}

	if (opts->new_orphan_branch && opts->orphan_from_empty_tree) {
		if (new_branch_info->name)
			BUG("'switch --orphan' should never accept a commit as starting point");
		new_branch_info->commit = NULL;
		new_branch_info->name = xstrdup("(empty)");
		do_merge = 1;
	}

	if (!new_branch_info->name) {
		new_branch_info->name = xstrdup("HEAD");
		new_branch_info->commit = old_branch_info.commit;
		if (!new_branch_info->commit)
			die(_("You are on a branch yet to be born"));
		parse_commit_or_die(new_branch_info->commit);

		if (opts->only_merge_on_switching_branches)
			do_merge = 0;
	}

	if (do_merge) {
		ret = merge_working_tree(opts, &old_branch_info, new_branch_info, &writeout_error);
		if (ret) {
			branch_info_release(&old_branch_info);
			return ret;
		}
	}

	if (!opts->quiet && !old_branch_info.path && old_branch_info.commit && new_branch_info->commit != old_branch_info.commit)
		orphaned_commit_warning(old_branch_info.commit, new_branch_info->commit);

	update_refs_for_switch(opts, &old_branch_info, new_branch_info);

	ret = post_checkout_hook(old_branch_info.commit, new_branch_info->commit, 1);
	branch_info_release(&old_branch_info);

	return ret || writeout_error;
}

static int git_checkout_config(const char *var, const char *value, void *cb)
{
	struct checkout_opts *opts = cb;

	if (!strcmp(var, "diff.ignoresubmodules")) {
		handle_ignore_submodules_arg(&opts->diff_options, value);
		return 0;
	}
	if (!strcmp(var, "checkout.guess")) {
		opts->dwim_new_local_branch = git_config_bool(var, value);
		return 0;
	}

	if (starts_with(var, "submodule."))
		return git_default_submodule_config(var, value, NULL);

	return git_xmerge_config(var, value, NULL);
}

static void setup_new_branch_info_and_source_tree(
	struct branch_info *new_branch_info,
	struct checkout_opts *opts,
	struct object_id *rev,
	const char *arg)
{
	struct tree **source_tree = &opts->source_tree;
	struct object_id branch_rev;

	new_branch_info->name = xstrdup(arg);
	setup_branch_path(new_branch_info);

	if (!check_refname_format(new_branch_info->path, 0) &&
	    !read_ref(new_branch_info->path, &branch_rev))
		oidcpy(rev, &branch_rev);
	else
		/* not an existing branch */
		FREE_AND_NULL(new_branch_info->path);

	new_branch_info->commit = lookup_commit_reference_gently(the_repository, rev, 1);
	if (!new_branch_info->commit) {
		/* not a commit */
		*source_tree = parse_tree_indirect(rev);
	} else {
		parse_commit_or_die(new_branch_info->commit);
		*source_tree = repo_get_commit_tree(the_repository,
						    new_branch_info->commit);
	}
}

static const char *parse_remote_branch(const char *arg,
				       struct object_id *rev,
				       int could_be_checkout_paths)
{
	int num_matches = 0;
	const char *remote = unique_tracking_name(arg, rev, &num_matches);

	if (remote && could_be_checkout_paths) {
		die(_("'%s' could be both a local file and a tracking branch.\n"
			"Please use -- (and optionally --no-guess) to disambiguate"),
		    arg);
	}

	if (!remote && num_matches > 1) {
	    if (advice_enabled(ADVICE_CHECKOUT_AMBIGUOUS_REMOTE_BRANCH_NAME)) {
		    advise(_("If you meant to check out a remote tracking branch on, e.g. 'origin',\n"
			     "you can do so by fully qualifying the name with the --track option:\n"
			     "\n"
			     "    git checkout --track origin/<name>\n"
			     "\n"
			     "If you'd like to always have checkouts of an ambiguous <name> prefer\n"
			     "one remote, e.g. the 'origin' remote, consider setting\n"
			     "checkout.defaultRemote=origin in your config."));
	    }

	    die(_("'%s' matched multiple (%d) remote tracking branches"),
		arg, num_matches);
	}

	return remote;
}

static int parse_branchname_arg(int argc, const char **argv,
				int dwim_new_local_branch_ok,
				struct branch_info *new_branch_info,
				struct checkout_opts *opts,
				struct object_id *rev)
{
	const char **new_branch = &opts->new_branch;
	int argcount = 0;
	const char *arg;
	int dash_dash_pos;
	int has_dash_dash = 0;
	int i;

	/*
	 * case 1: git checkout <ref> -- [<paths>]
	 *
	 *   <ref> must be a valid tree, everything after the '--' must be
	 *   a path.
	 *
	 * case 2: git checkout -- [<paths>]
	 *
	 *   everything after the '--' must be paths.
	 *
	 * case 3: git checkout <something> [--]
	 *
	 *   (a) If <something> is a commit, that is to
	 *       switch to the branch or detach HEAD at it.  As a special case,
	 *       if <something> is A...B (missing A or B means HEAD but you can
	 *       omit at most one side), and if there is a unique merge base
	 *       between A and B, A...B names that merge base.
	 *
	 *   (b) If <something> is _not_ a commit, either "--" is present
	 *       or <something> is not a path, no -t or -b was given,
	 *       and there is a tracking branch whose name is <something>
	 *       in one and only one remote (or if the branch exists on the
	 *       remote named in checkout.defaultRemote), then this is a
	 *       short-hand to fork local <something> from that
	 *       remote-tracking branch.
	 *
	 *   (c) Otherwise, if "--" is present, treat it like case (1).
	 *
	 *   (d) Otherwise :
	 *       - if it's a reference, treat it like case (1)
	 *       - else if it's a path, treat it like case (2)
	 *       - else: fail.
	 *
	 * case 4: git checkout <something> <paths>
	 *
	 *   The first argument must not be ambiguous.
	 *   - If it's *only* a reference, treat it like case (1).
	 *   - If it's only a path, treat it like case (2).
	 *   - else: fail.
	 *
	 */
	if (!argc)
		return 0;

	if (!opts->accept_pathspec) {
		if (argc > 1)
			die(_("only one reference expected"));
		has_dash_dash = 1; /* helps disambiguate */
	}

	arg = argv[0];
	dash_dash_pos = -1;
	for (i = 0; i < argc; i++) {
		if (opts->accept_pathspec && !strcmp(argv[i], "--")) {
			dash_dash_pos = i;
			break;
		}
	}
	if (dash_dash_pos == 0)
		return 1; /* case (2) */
	else if (dash_dash_pos == 1)
		has_dash_dash = 1; /* case (3) or (1) */
	else if (dash_dash_pos >= 2)
		die(_("only one reference expected, %d given."), dash_dash_pos);
	opts->count_checkout_paths = !opts->quiet && !has_dash_dash;

	if (!strcmp(arg, "-"))
		arg = "@{-1}";

	if (repo_get_oid_mb(the_repository, arg, rev)) {
		/*
		 * Either case (3) or (4), with <something> not being
		 * a commit, or an attempt to use case (1) with an
		 * invalid ref.
		 *
		 * It's likely an error, but we need to find out if
		 * we should auto-create the branch, case (3).(b).
		 */
		int recover_with_dwim = dwim_new_local_branch_ok;

		int could_be_checkout_paths = !has_dash_dash &&
			check_filename(opts->prefix, arg);

		if (!has_dash_dash && !no_wildcard(arg))
			recover_with_dwim = 0;

		/*
		 * Accept "git checkout foo", "git checkout foo --"
		 * and "git switch foo" as candidates for dwim.
		 */
		if (!(argc == 1 && !has_dash_dash) &&
		    !(argc == 2 && has_dash_dash) &&
		    opts->accept_pathspec)
			recover_with_dwim = 0;

		if (recover_with_dwim) {
			const char *remote = parse_remote_branch(arg, rev,
								 could_be_checkout_paths);
			if (remote) {
				*new_branch = arg;
				arg = remote;
				/* DWIMmed to create local branch, case (3).(b) */
			} else {
				recover_with_dwim = 0;
			}
		}

		if (!recover_with_dwim) {
			if (has_dash_dash)
				die(_("invalid reference: %s"), arg);
			return argcount;
		}
	}

	/* we can't end up being in (2) anymore, eat the argument */
	argcount++;
	argv++;
	argc--;

	setup_new_branch_info_and_source_tree(new_branch_info, opts, rev, arg);

	if (!opts->source_tree)                   /* case (1): want a tree */
		die(_("reference is not a tree: %s"), arg);

	if (!has_dash_dash) {	/* case (3).(d) -> (1) */
		/*
		 * Do not complain the most common case
		 *	git checkout branch
		 * even if there happen to be a file called 'branch';
		 * it would be extremely annoying.
		 */
		if (argc)
			verify_non_filename(opts->prefix, arg);
	} else if (opts->accept_pathspec) {
		argcount++;
		argv++;
		argc--;
	}

	return argcount;
}

static int switch_unborn_to_new_branch(const struct checkout_opts *opts)
{
	int status;
	struct strbuf branch_ref = STRBUF_INIT;

	trace2_cmd_mode("unborn");

	if (!opts->new_branch)
		die(_("You are on a branch yet to be born"));
	strbuf_addf(&branch_ref, "refs/heads/%s", opts->new_branch);
	status = create_symref("HEAD", branch_ref.buf, "checkout -b");
	strbuf_release(&branch_ref);
	if (!opts->quiet)
		fprintf(stderr, _("Switched to a new branch '%s'\n"),
			opts->new_branch);
	return status;
}

static void die_expecting_a_branch(const struct branch_info *branch_info)
{
	struct object_id oid;
	char *to_free;
	int code;

	if (repo_dwim_ref(the_repository, branch_info->name,
			  strlen(branch_info->name), &oid, &to_free, 0) == 1) {
		const char *ref = to_free;

		if (skip_prefix(ref, "refs/tags/", &ref))
			code = die_message(_("a branch is expected, got tag '%s'"), ref);
		else if (skip_prefix(ref, "refs/remotes/", &ref))
			code = die_message(_("a branch is expected, got remote branch '%s'"), ref);
		else
			code = die_message(_("a branch is expected, got '%s'"), ref);
	}
	else if (branch_info->commit)
		code = die_message(_("a branch is expected, got commit '%s'"), branch_info->name);
	else
		/*
		 * This case should never happen because we already die() on
		 * non-commit, but just in case.
		 */
		code = die_message(_("a branch is expected, got '%s'"), branch_info->name);

	if (advice_enabled(ADVICE_SUGGEST_DETACHING_HEAD))
		advise(_("If you want to detach HEAD at the commit, try again with the --detach option."));

	exit(code);
}

static void die_if_some_operation_in_progress(void)
{
	struct wt_status_state state;

	memset(&state, 0, sizeof(state));
	wt_status_get_state(the_repository, &state, 0);

	if (state.merge_in_progress)
		die(_("cannot switch branch while merging\n"
		      "Consider \"git merge --quit\" "
		      "or \"git worktree add\"."));
	if (state.am_in_progress)
		die(_("cannot switch branch in the middle of an am session\n"
		      "Consider \"git am --quit\" "
		      "or \"git worktree add\"."));
	if (state.rebase_interactive_in_progress || state.rebase_in_progress)
		die(_("cannot switch branch while rebasing\n"
		      "Consider \"git rebase --quit\" "
		      "or \"git worktree add\"."));
	if (state.cherry_pick_in_progress)
		die(_("cannot switch branch while cherry-picking\n"
		      "Consider \"git cherry-pick --quit\" "
		      "or \"git worktree add\"."));
	if (state.revert_in_progress)
		die(_("cannot switch branch while reverting\n"
		      "Consider \"git revert --quit\" "
		      "or \"git worktree add\"."));
	if (state.bisect_in_progress)
		warning(_("you are switching branch while bisecting"));

	wt_status_state_free_buffers(&state);
}

static int checkout_branch(struct checkout_opts *opts,
			   struct branch_info *new_branch_info)
{
	if (opts->pathspec.nr)
		die(_("paths cannot be used with switching branches"));

	if (opts->patch_mode)
		die(_("'%s' cannot be used with switching branches"),
		    "--patch");

	if (opts->overlay_mode != -1)
		die(_("'%s' cannot be used with switching branches"),
		    "--[no]-overlay");

	if (opts->writeout_stage)
		die(_("'%s' cannot be used with switching branches"),
		    "--ours/--theirs");

	if (opts->force && opts->merge)
		die(_("'%s' cannot be used with '%s'"), "-f", "-m");

	if (opts->discard_changes && opts->merge)
		die(_("'%s' cannot be used with '%s'"), "--discard-changes", "--merge");

	if (opts->force_detach && opts->new_branch)
		die(_("'%s' cannot be used with '%s'"),
		    "--detach", "-b/-B/--orphan");

	if (opts->new_orphan_branch) {
		if (opts->track != BRANCH_TRACK_UNSPECIFIED)
			die(_("'%s' cannot be used with '%s'"), "--orphan", "-t");
		if (opts->orphan_from_empty_tree && new_branch_info->name)
			die(_("'%s' cannot take <start-point>"), "--orphan");
	} else if (opts->force_detach) {
		if (opts->track != BRANCH_TRACK_UNSPECIFIED)
			die(_("'%s' cannot be used with '%s'"), "--detach", "-t");
	} else if (opts->track == BRANCH_TRACK_UNSPECIFIED)
		opts->track = git_branch_track;

	if (new_branch_info->name && !new_branch_info->commit)
		die(_("Cannot switch branch to a non-commit '%s'"),
		    new_branch_info->name);

	if (!opts->switch_branch_doing_nothing_is_ok &&
	    !new_branch_info->name &&
	    !opts->new_branch &&
	    !opts->force_detach)
		die(_("missing branch or commit argument"));

	if (!opts->implicit_detach &&
	    !opts->force_detach &&
	    !opts->new_branch &&
	    !opts->new_branch_force &&
	    new_branch_info->name &&
	    !new_branch_info->path)
		die_expecting_a_branch(new_branch_info);

	if (!opts->can_switch_when_in_progress)
		die_if_some_operation_in_progress();

	if (new_branch_info->path && !opts->force_detach && !opts->new_branch &&
	    !opts->ignore_other_worktrees) {
		int flag;
		char *head_ref = resolve_refdup("HEAD", 0, NULL, &flag);
		if (head_ref &&
		    (!(flag & REF_ISSYMREF) || strcmp(head_ref, new_branch_info->path)))
			die_if_checked_out(new_branch_info->path, 1);
		free(head_ref);
	}

	if (!new_branch_info->commit && opts->new_branch) {
		struct object_id rev;
		int flag;

		if (!read_ref_full("HEAD", 0, &rev, &flag) &&
		    (flag & REF_ISSYMREF) && is_null_oid(&rev))
			return switch_unborn_to_new_branch(opts);
	}
	return switch_branches(opts, new_branch_info);
}

static struct option *add_common_options(struct checkout_opts *opts,
					 struct option *prevopts)
{
	struct option options[] = {
		OPT__QUIET(&opts->quiet, N_("suppress progress reporting")),
		OPT_CALLBACK_F(0, "recurse-submodules", NULL,
			    "checkout", "control recursive updating of submodules",
			    PARSE_OPT_OPTARG, option_parse_recurse_submodules_worktree_updater),
		OPT_BOOL(0, "progress", &opts->show_progress, N_("force progress reporting")),
		OPT_BOOL('m', "merge", &opts->merge, N_("perform a 3-way merge with the new branch")),
		OPT_STRING(0, "conflict", &opts->conflict_style, N_("style"),
			   N_("conflict style (merge, diff3, or zdiff3)")),
		OPT_END()
	};
	struct option *newopts = parse_options_concat(prevopts, options);
	free(prevopts);
	return newopts;
}

static struct option *add_common_switch_branch_options(
	struct checkout_opts *opts, struct option *prevopts)
{
	struct option options[] = {
		OPT_BOOL('d', "detach", &opts->force_detach, N_("detach HEAD at named commit")),
		OPT_CALLBACK_F('t', "track",  &opts->track, "(direct|inherit)",
			N_("set branch tracking configuration"),
			PARSE_OPT_OPTARG,
			parse_opt_tracking_mode),
		OPT__FORCE(&opts->force, N_("force checkout (throw away local modifications)"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_STRING(0, "orphan", &opts->new_orphan_branch, N_("new-branch"), N_("new unparented branch")),
		OPT_BOOL_F(0, "overwrite-ignore", &opts->overwrite_ignore,
			   N_("update ignored files (default)"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_BOOL(0, "ignore-other-worktrees", &opts->ignore_other_worktrees,
			 N_("do not check if another worktree is holding the given ref")),
		OPT_END()
	};
	struct option *newopts = parse_options_concat(prevopts, options);
	free(prevopts);
	return newopts;
}

static struct option *add_checkout_path_options(struct checkout_opts *opts,
						struct option *prevopts)
{
	struct option options[] = {
		OPT_SET_INT_F('2', "ours", &opts->writeout_stage,
			      N_("checkout our version for unmerged files"),
			      2, PARSE_OPT_NONEG),
		OPT_SET_INT_F('3', "theirs", &opts->writeout_stage,
			      N_("checkout their version for unmerged files"),
			      3, PARSE_OPT_NONEG),
		OPT_BOOL('p', "patch", &opts->patch_mode, N_("select hunks interactively")),
		OPT_BOOL(0, "ignore-skip-worktree-bits", &opts->ignore_skipworktree,
			 N_("do not limit pathspecs to sparse entries only")),
		OPT_PATHSPEC_FROM_FILE(&opts->pathspec_from_file),
		OPT_PATHSPEC_FILE_NUL(&opts->pathspec_file_nul),
		OPT_END()
	};
	struct option *newopts = parse_options_concat(prevopts, options);
	free(prevopts);
	return newopts;
}

/* create-branch option (either b or c) */
static char cb_option = 'b';

static int checkout_main(int argc, const char **argv, const char *prefix,
			 struct checkout_opts *opts, struct option *options,
			 const char * const usagestr[],
			 struct branch_info *new_branch_info)
{
	int parseopt_flags = 0;

	opts->overwrite_ignore = 1;
	opts->prefix = prefix;
	opts->show_progress = -1;

	git_config(git_checkout_config, opts);
	if (the_repository->gitdir) {
		prepare_repo_settings(the_repository);
		the_repository->settings.command_requires_full_index = 0;
	}

	opts->track = BRANCH_TRACK_UNSPECIFIED;

	if (!opts->accept_pathspec && !opts->accept_ref)
		BUG("make up your mind, you need to take _something_");
	if (opts->accept_pathspec && opts->accept_ref)
		parseopt_flags = PARSE_OPT_KEEP_DASHDASH;

	argc = parse_options(argc, argv, prefix, options,
			     usagestr, parseopt_flags);

	if (opts->show_progress < 0) {
		if (opts->quiet)
			opts->show_progress = 0;
		else
			opts->show_progress = isatty(2);
	}

	if (opts->conflict_style) {
		opts->merge = 1; /* implied */
		git_xmerge_config("merge.conflictstyle", opts->conflict_style, NULL);
	}
	if (opts->force) {
		opts->discard_changes = 1;
		opts->ignore_unmerged_opt = "--force";
		opts->ignore_unmerged = 1;
	}

	if ((!!opts->new_branch + !!opts->new_branch_force + !!opts->new_orphan_branch) > 1)
		die(_("options '-%c', '-%c', and '%s' cannot be used together"),
			cb_option, toupper(cb_option), "--orphan");

	if (opts->overlay_mode == 1 && opts->patch_mode)
		die(_("options '%s' and '%s' cannot be used together"), "-p", "--overlay");

	if (opts->checkout_index >= 0 || opts->checkout_worktree >= 0) {
		if (opts->checkout_index < 0)
			opts->checkout_index = 0;
		if (opts->checkout_worktree < 0)
			opts->checkout_worktree = 0;
	} else {
		if (opts->checkout_index < 0)
			opts->checkout_index = -opts->checkout_index - 1;
		if (opts->checkout_worktree < 0)
			opts->checkout_worktree = -opts->checkout_worktree - 1;
	}
	if (opts->checkout_index < 0 || opts->checkout_worktree < 0)
		BUG("these flags should be non-negative by now");
	/*
	 * convenient shortcut: "git restore --staged [--worktree]" equals
	 * "git restore --staged [--worktree] --source HEAD"
	 */
	if (!opts->from_treeish && opts->checkout_index)
		opts->from_treeish = "HEAD";

	/*
	 * From here on, new_branch will contain the branch to be checked out,
	 * and new_branch_force and new_orphan_branch will tell us which one of
	 * -b/-B/-c/-C/--orphan is being used.
	 */
	if (opts->new_branch_force)
		opts->new_branch = opts->new_branch_force;

	if (opts->new_orphan_branch)
		opts->new_branch = opts->new_orphan_branch;

	/* --track without -c/-C/-b/-B/--orphan should DWIM */
	if (opts->track != BRANCH_TRACK_UNSPECIFIED && !opts->new_branch) {
		const char *argv0 = argv[0];
		if (!argc || !strcmp(argv0, "--"))
			die(_("--track needs a branch name"));
		skip_prefix(argv0, "refs/", &argv0);
		skip_prefix(argv0, "remotes/", &argv0);
		argv0 = strchr(argv0, '/');
		if (!argv0 || !argv0[1])
			die(_("missing branch name; try -%c"), cb_option);
		opts->new_branch = argv0 + 1;
	}

	/*
	 * Extract branch name from command line arguments, so
	 * all that is left is pathspecs.
	 *
	 * Handle
	 *
	 *  1) git checkout <tree> -- [<paths>]
	 *  2) git checkout -- [<paths>]
	 *  3) git checkout <something> [<paths>]
	 *
	 * including "last branch" syntax and DWIM-ery for names of
	 * remote branches, erroring out for invalid or ambiguous cases.
	 */
	if (argc && opts->accept_ref) {
		struct object_id rev;
		int dwim_ok =
			!opts->patch_mode &&
			opts->dwim_new_local_branch &&
			opts->track == BRANCH_TRACK_UNSPECIFIED &&
			!opts->new_branch;
		int n = parse_branchname_arg(argc, argv, dwim_ok,
					     new_branch_info, opts, &rev);
		argv += n;
		argc -= n;
	} else if (!opts->accept_ref && opts->from_treeish) {
		struct object_id rev;

		if (repo_get_oid_mb(the_repository, opts->from_treeish, &rev))
			die(_("could not resolve %s"), opts->from_treeish);

		setup_new_branch_info_and_source_tree(new_branch_info,
						      opts, &rev,
						      opts->from_treeish);

		if (!opts->source_tree)
			die(_("reference is not a tree: %s"), opts->from_treeish);
	}

	if (argc) {
		parse_pathspec(&opts->pathspec, 0,
			       opts->patch_mode ? PATHSPEC_PREFIX_ORIGIN : 0,
			       prefix, argv);

		if (!opts->pathspec.nr)
			die(_("invalid path specification"));

		/*
		 * Try to give more helpful suggestion.
		 * new_branch && argc > 1 will be caught later.
		 */
		if (opts->new_branch && argc == 1 && !new_branch_info->commit)
			die(_("'%s' is not a commit and a branch '%s' cannot be created from it"),
				argv[0], opts->new_branch);

		if (opts->force_detach)
			die(_("git checkout: --detach does not take a path argument '%s'"),
			    argv[0]);
	}

	if (opts->pathspec_from_file) {
		if (opts->pathspec.nr)
			die(_("'%s' and pathspec arguments cannot be used together"), "--pathspec-from-file");

		if (opts->force_detach)
			die(_("options '%s' and '%s' cannot be used together"), "--pathspec-from-file", "--detach");

		if (opts->patch_mode)
			die(_("options '%s' and '%s' cannot be used together"), "--pathspec-from-file", "--patch");

		parse_pathspec_file(&opts->pathspec, 0,
				    0,
				    prefix, opts->pathspec_from_file, opts->pathspec_file_nul);
	} else if (opts->pathspec_file_nul) {
		die(_("the option '%s' requires '%s'"), "--pathspec-file-nul", "--pathspec-from-file");
	}

	opts->pathspec.recursive = 1;

	if (opts->pathspec.nr) {
		if (1 < !!opts->writeout_stage + !!opts->force + !!opts->merge)
			die(_("git checkout: --ours/--theirs, --force and --merge are incompatible when\n"
			      "checking out of the index."));
	} else {
		if (opts->accept_pathspec && !opts->empty_pathspec_ok &&
		    !opts->patch_mode)	/* patch mode is special */
			die(_("you must specify path(s) to restore"));
	}

	if (opts->new_branch) {
		struct strbuf buf = STRBUF_INIT;

		if (opts->new_branch_force)
			opts->branch_exists = validate_branchname(opts->new_branch, &buf);
		else
			opts->branch_exists =
				validate_new_branchname(opts->new_branch, &buf, 0);
		strbuf_release(&buf);
	}

	if (opts->patch_mode || opts->pathspec.nr)
		return checkout_paths(opts, new_branch_info);
	else
		return checkout_branch(opts, new_branch_info);
}

int cmd_checkout(int argc, const char **argv, const char *prefix)
{
	struct checkout_opts opts;
	struct option *options;
	struct option checkout_options[] = {
		OPT_STRING('b', NULL, &opts.new_branch, N_("branch"),
			   N_("create and checkout a new branch")),
		OPT_STRING('B', NULL, &opts.new_branch_force, N_("branch"),
			   N_("create/reset and checkout a branch")),
		OPT_BOOL('l', NULL, &opts.new_branch_log, N_("create reflog for new branch")),
		OPT_BOOL(0, "guess", &opts.dwim_new_local_branch,
			 N_("second guess 'git checkout <no-such-branch>' (default)")),
		OPT_BOOL(0, "overlay", &opts.overlay_mode, N_("use overlay mode (default)")),
		OPT_END()
	};
	int ret;
	struct branch_info new_branch_info = { 0 };

	memset(&opts, 0, sizeof(opts));
	opts.dwim_new_local_branch = 1;
	opts.switch_branch_doing_nothing_is_ok = 1;
	opts.only_merge_on_switching_branches = 0;
	opts.accept_ref = 1;
	opts.accept_pathspec = 1;
	opts.implicit_detach = 1;
	opts.can_switch_when_in_progress = 1;
	opts.orphan_from_empty_tree = 0;
	opts.empty_pathspec_ok = 1;
	opts.overlay_mode = -1;
	opts.checkout_index = -2;    /* default on */
	opts.checkout_worktree = -2; /* default on */

	if (argc == 3 && !strcmp(argv[1], "-b")) {
		/*
		 * User ran 'git checkout -b <branch>' and expects
		 * the same behavior as 'git switch -c <branch>'.
		 */
		opts.switch_branch_doing_nothing_is_ok = 0;
		opts.only_merge_on_switching_branches = 1;
	}

	options = parse_options_dup(checkout_options);
	options = add_common_options(&opts, options);
	options = add_common_switch_branch_options(&opts, options);
	options = add_checkout_path_options(&opts, options);

	ret = checkout_main(argc, argv, prefix, &opts,
			    options, checkout_usage, &new_branch_info);
	branch_info_release(&new_branch_info);
	clear_pathspec(&opts.pathspec);
	free(opts.pathspec_from_file);
	FREE_AND_NULL(options);
	return ret;
}

int cmd_switch(int argc, const char **argv, const char *prefix)
{
	struct checkout_opts opts;
	struct option *options = NULL;
	struct option switch_options[] = {
		OPT_STRING('c', "create", &opts.new_branch, N_("branch"),
			   N_("create and switch to a new branch")),
		OPT_STRING('C', "force-create", &opts.new_branch_force, N_("branch"),
			   N_("create/reset and switch to a branch")),
		OPT_BOOL(0, "guess", &opts.dwim_new_local_branch,
			 N_("second guess 'git switch <no-such-branch>'")),
		OPT_BOOL(0, "discard-changes", &opts.discard_changes,
			 N_("throw away local modifications")),
		OPT_END()
	};
	int ret;
	struct branch_info new_branch_info = { 0 };

	memset(&opts, 0, sizeof(opts));
	opts.dwim_new_local_branch = 1;
	opts.accept_ref = 1;
	opts.accept_pathspec = 0;
	opts.switch_branch_doing_nothing_is_ok = 0;
	opts.only_merge_on_switching_branches = 1;
	opts.implicit_detach = 0;
	opts.can_switch_when_in_progress = 0;
	opts.orphan_from_empty_tree = 1;
	opts.overlay_mode = -1;

	options = parse_options_dup(switch_options);
	options = add_common_options(&opts, options);
	options = add_common_switch_branch_options(&opts, options);

	cb_option = 'c';

	ret = checkout_main(argc, argv, prefix, &opts,
			    options, switch_branch_usage, &new_branch_info);
	branch_info_release(&new_branch_info);
	FREE_AND_NULL(options);
	return ret;
}

int cmd_restore(int argc, const char **argv, const char *prefix)
{
	struct checkout_opts opts;
	struct option *options;
	struct option restore_options[] = {
		OPT_STRING('s', "source", &opts.from_treeish, "<tree-ish>",
			   N_("which tree-ish to checkout from")),
		OPT_BOOL('S', "staged", &opts.checkout_index,
			   N_("restore the index")),
		OPT_BOOL('W', "worktree", &opts.checkout_worktree,
			   N_("restore the working tree (default)")),
		OPT_BOOL(0, "ignore-unmerged", &opts.ignore_unmerged,
			 N_("ignore unmerged entries")),
		OPT_BOOL(0, "overlay", &opts.overlay_mode, N_("use overlay mode")),
		OPT_END()
	};
	int ret;
	struct branch_info new_branch_info = { 0 };

	memset(&opts, 0, sizeof(opts));
	opts.accept_ref = 0;
	opts.accept_pathspec = 1;
	opts.empty_pathspec_ok = 0;
	opts.overlay_mode = 0;
	opts.checkout_index = -1;    /* default off */
	opts.checkout_worktree = -2; /* default on */
	opts.ignore_unmerged_opt = "--ignore-unmerged";

	options = parse_options_dup(restore_options);
	options = add_common_options(&opts, options);
	options = add_checkout_path_options(&opts, options);

	ret = checkout_main(argc, argv, prefix, &opts,
			    options, restore_usage, &new_branch_info);
	branch_info_release(&new_branch_info);
	FREE_AND_NULL(options);
	return ret;
}
