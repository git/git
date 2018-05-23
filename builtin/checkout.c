#include "builtin.h"
#include "config.h"
#include "checkout.h"
#include "lockfile.h"
#include "parse-options.h"
#include "refs.h"
#include "commit.h"
#include "tree.h"
#include "tree-walk.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "dir.h"
#include "run-command.h"
#include "merge-recursive.h"
#include "branch.h"
#include "diff.h"
#include "revision.h"
#include "remote.h"
#include "blob.h"
#include "xdiff-interface.h"
#include "ll-merge.h"
#include "resolve-undo.h"
#include "submodule-config.h"
#include "submodule.h"

static const char * const checkout_usage[] = {
	N_("git checkout [<options>] <branch>"),
	N_("git checkout [<options>] [<branch>] -- <file>..."),
	NULL,
};

struct checkout_opts {
	int patch_mode;
	int quiet;
	int merge;
	int force;
	int force_detach;
	int writeout_stage;
	int overwrite_ignore;
	int ignore_skipworktree;
	int ignore_other_worktrees;
	int show_progress;

	const char *new_branch;
	const char *new_branch_force;
	const char *new_orphan_branch;
	int new_branch_log;
	enum branch_track track;
	struct diff_options diff_options;

	int branch_exists;
	const char *prefix;
	struct pathspec pathspec;
	struct tree *source_tree;
};

static int post_checkout_hook(struct commit *old_commit, struct commit *new_commit,
			      int changed)
{
	return run_hook_le(NULL, "post-checkout",
			   oid_to_hex(old_commit ? &old_commit->object.oid : &null_oid),
			   oid_to_hex(new_commit ? &new_commit->object.oid : &null_oid),
			   changed ? "1" : "0", NULL);
	/* "new_commit" can be NULL when checking out from the index before
	   a commit exists. */

}

static int update_some(const struct object_id *oid, struct strbuf *base,
		const char *pathname, unsigned mode, int stage, void *context)
{
	int len;
	struct cache_entry *ce;
	int pos;

	if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	len = base->len + strlen(pathname);
	ce = xcalloc(1, cache_entry_size(len));
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
	pos = cache_name_pos(ce->name, ce->ce_namelen);
	if (pos >= 0) {
		struct cache_entry *old = active_cache[pos];
		if (ce->ce_mode == old->ce_mode &&
		    !oidcmp(&ce->oid, &old->oid)) {
			old->ce_flags |= CE_UPDATE;
			free(ce);
			return 0;
		}
	}

	add_cache_entry(ce, ADD_CACHE_OK_TO_ADD | ADD_CACHE_OK_TO_REPLACE);
	return 0;
}

static int read_tree_some(struct tree *tree, const struct pathspec *pathspec)
{
	read_tree_recursive(tree, "", 0, 0, pathspec, update_some, NULL);

	/* update the index with the given tree's info
	 * for all args, expanding wildcards, and exit
	 * with any non-zero return code.
	 */
	return 0;
}

static int skip_same_name(const struct cache_entry *ce, int pos)
{
	while (++pos < active_nr &&
	       !strcmp(active_cache[pos]->name, ce->name))
		; /* skip */
	return pos;
}

static int check_stage(int stage, const struct cache_entry *ce, int pos)
{
	while (pos < active_nr &&
	       !strcmp(active_cache[pos]->name, ce->name)) {
		if (ce_stage(active_cache[pos]) == stage)
			return 0;
		pos++;
	}
	if (stage == 2)
		return error(_("path '%s' does not have our version"), ce->name);
	else
		return error(_("path '%s' does not have their version"), ce->name);
}

static int check_stages(unsigned stages, const struct cache_entry *ce, int pos)
{
	unsigned seen = 0;
	const char *name = ce->name;

	while (pos < active_nr) {
		ce = active_cache[pos];
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
			  const struct checkout *state)
{
	while (pos < active_nr &&
	       !strcmp(active_cache[pos]->name, ce->name)) {
		if (ce_stage(active_cache[pos]) == stage)
			return checkout_entry(active_cache[pos], state, NULL);
		pos++;
	}
	if (stage == 2)
		return error(_("path '%s' does not have our version"), ce->name);
	else
		return error(_("path '%s' does not have their version"), ce->name);
}

static int checkout_merged(int pos, const struct checkout *state)
{
	struct cache_entry *ce = active_cache[pos];
	const char *path = ce->name;
	mmfile_t ancestor, ours, theirs;
	int status;
	struct object_id oid;
	mmbuffer_t result_buf;
	struct object_id threeway[3];
	unsigned mode = 0;

	memset(threeway, 0, sizeof(threeway));
	while (pos < active_nr) {
		int stage;
		stage = ce_stage(ce);
		if (!stage || strcmp(path, ce->name))
			break;
		oidcpy(&threeway[stage - 1], &ce->oid);
		if (stage == 2)
			mode = create_ce_mode(ce->ce_mode);
		pos++;
		ce = active_cache[pos];
	}
	if (is_null_oid(&threeway[1]) || is_null_oid(&threeway[2]))
		return error(_("path '%s' does not have necessary versions"), path);

	read_mmblob(&ancestor, &threeway[0]);
	read_mmblob(&ours, &threeway[1]);
	read_mmblob(&theirs, &threeway[2]);

	/*
	 * NEEDSWORK: re-create conflicts from merges with
	 * merge.renormalize set, too
	 */
	status = ll_merge(&result_buf, path, &ancestor, "base",
			  &ours, "ours", &theirs, "theirs", NULL);
	free(ancestor.ptr);
	free(ours.ptr);
	free(theirs.ptr);
	if (status < 0 || !result_buf.ptr) {
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
	if (write_object_file(result_buf.ptr, result_buf.size, blob_type, &oid))
		die(_("Unable to add merge result for '%s'"), path);
	free(result_buf.ptr);
	ce = make_cache_entry(mode, oid.hash, path, 2, 0);
	if (!ce)
		die(_("make_cache_entry failed for path '%s'"), path);
	status = checkout_entry(ce, state, NULL);
	free(ce);
	return status;
}

static int checkout_paths(const struct checkout_opts *opts,
			  const char *revision)
{
	int pos;
	struct checkout state = CHECKOUT_INIT;
	static char *ps_matched;
	struct object_id rev;
	struct commit *head;
	int errs = 0;
	struct lock_file lock_file = LOCK_INIT;

	if (opts->track != BRANCH_TRACK_UNSPECIFIED)
		die(_("'%s' cannot be used with updating paths"), "--track");

	if (opts->new_branch_log)
		die(_("'%s' cannot be used with updating paths"), "-l");

	if (opts->force && opts->patch_mode)
		die(_("'%s' cannot be used with updating paths"), "-f");

	if (opts->force_detach)
		die(_("'%s' cannot be used with updating paths"), "--detach");

	if (opts->merge && opts->patch_mode)
		die(_("'%s' cannot be used with %s"), "--merge", "--patch");

	if (opts->force && opts->merge)
		die(_("'%s' cannot be used with %s"), "-f", "-m");

	if (opts->new_branch)
		die(_("Cannot update paths and switch to branch '%s' at the same time."),
		    opts->new_branch);

	if (opts->patch_mode)
		return run_add_interactive(revision, "--patch=checkout",
					   &opts->pathspec);

	hold_locked_index(&lock_file, LOCK_DIE_ON_ERROR);
	if (read_cache_preload(&opts->pathspec) < 0)
		return error(_("index file corrupt"));

	if (opts->source_tree)
		read_tree_some(opts->source_tree, &opts->pathspec);

	ps_matched = xcalloc(opts->pathspec.nr, 1);

	/*
	 * Make sure all pathspecs participated in locating the paths
	 * to be checked out.
	 */
	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];
		ce->ce_flags &= ~CE_MATCHED;
		if (!opts->ignore_skipworktree && ce_skip_worktree(ce))
			continue;
		if (opts->source_tree && !(ce->ce_flags & CE_UPDATE))
			/*
			 * "git checkout tree-ish -- path", but this entry
			 * is in the original index; it will not be checked
			 * out to the working tree and it does not matter
			 * if pathspec matched this entry.  We will not do
			 * anything to this entry at all.
			 */
			continue;
		/*
		 * Either this entry came from the tree-ish we are
		 * checking the paths out of, or we are checking out
		 * of the index.
		 *
		 * If it comes from the tree-ish, we already know it
		 * matches the pathspec and could just stamp
		 * CE_MATCHED to it from update_some(). But we still
		 * need ps_matched and read_tree_recursive (and
		 * eventually tree_entry_interesting) cannot fill
		 * ps_matched yet. Once it can, we can avoid calling
		 * match_pathspec() for _all_ entries when
		 * opts->source_tree != NULL.
		 */
		if (ce_path_match(ce, &opts->pathspec, ps_matched))
			ce->ce_flags |= CE_MATCHED;
	}

	if (report_path_error(ps_matched, &opts->pathspec, opts->prefix)) {
		free(ps_matched);
		return 1;
	}
	free(ps_matched);

	/* "checkout -m path" to recreate conflicted state */
	if (opts->merge)
		unmerge_marked_index(&the_index);

	/* Any unmerged paths? */
	for (pos = 0; pos < active_nr; pos++) {
		const struct cache_entry *ce = active_cache[pos];
		if (ce->ce_flags & CE_MATCHED) {
			if (!ce_stage(ce))
				continue;
			if (opts->force) {
				warning(_("path '%s' is unmerged"), ce->name);
			} else if (opts->writeout_stage) {
				errs |= check_stage(opts->writeout_stage, ce, pos);
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
	state.force = 1;
	state.refresh_cache = 1;
	state.istate = &the_index;

	enable_delayed_checkout(&state);
	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];
		if (ce->ce_flags & CE_MATCHED) {
			if (!ce_stage(ce)) {
				errs |= checkout_entry(ce, &state, NULL);
				continue;
			}
			if (opts->writeout_stage)
				errs |= checkout_stage(opts->writeout_stage, ce, pos, &state);
			else if (opts->merge)
				errs |= checkout_merged(pos, &state);
			pos = skip_same_name(ce, pos) - 1;
		}
	}
	errs |= finish_delayed_checkout(&state);

	if (write_locked_index(&the_index, &lock_file, COMMIT_LOCK))
		die(_("unable to write new index file"));

	read_ref_full("HEAD", 0, &rev, NULL);
	head = lookup_commit_reference_gently(&rev, 1);

	errs |= post_checkout_hook(head, head, 0);
	return errs;
}

static void show_local_changes(struct object *head,
			       const struct diff_options *opts)
{
	struct rev_info rev;
	/* I think we want full paths, even if we're in a subdirectory. */
	init_revisions(&rev, NULL);
	rev.diffopt.flags = opts->flags;
	rev.diffopt.output_format |= DIFF_FORMAT_NAME_STATUS;
	diff_setup_done(&rev.diffopt);
	add_pending_object(&rev, head, NULL);
	run_diff_index(&rev, 0);
}

static void describe_detached_head(const char *msg, struct commit *commit)
{
	struct strbuf sb = STRBUF_INIT;

	if (!parse_commit(commit))
		pp_commit_easy(CMIT_FMT_ONELINE, commit, &sb);
	if (print_sha1_ellipsis()) {
		fprintf(stderr, "%s %s... %s\n", msg,
			find_unique_abbrev(&commit->object.oid, DEFAULT_ABBREV), sb.buf);
	} else {
		fprintf(stderr, "%s %s %s\n", msg,
			find_unique_abbrev(&commit->object.oid, DEFAULT_ABBREV), sb.buf);
	}
	strbuf_release(&sb);
}

static int reset_tree(struct tree *tree, const struct checkout_opts *o,
		      int worktree, int *writeout_error)
{
	struct unpack_trees_options opts;
	struct tree_desc tree_desc;

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = -1;
	opts.update = worktree;
	opts.skip_unmerged = !worktree;
	opts.reset = 1;
	opts.merge = 1;
	opts.fn = oneway_merge;
	opts.verbose_update = o->show_progress;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
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

struct branch_info {
	const char *name; /* The short name used */
	const char *path; /* The full name of a real branch */
	struct commit *commit; /* The named commit */
	/*
	 * if not null the branch is detached because it's already
	 * checked out in this checkout
	 */
	char *checkout;
};

static void setup_branch_path(struct branch_info *branch)
{
	struct strbuf buf = STRBUF_INIT;

	strbuf_branchname(&buf, branch->name, INTERPRET_BRANCH_LOCAL);
	if (strcmp(buf.buf, branch->name))
		branch->name = xstrdup(buf.buf);
	strbuf_splice(&buf, 0, 0, "refs/heads/", 11);
	branch->path = strbuf_detach(&buf, NULL);
}

static int merge_working_tree(const struct checkout_opts *opts,
			      struct branch_info *old_branch_info,
			      struct branch_info *new_branch_info,
			      int *writeout_error)
{
	int ret;
	struct lock_file lock_file = LOCK_INIT;

	hold_locked_index(&lock_file, LOCK_DIE_ON_ERROR);
	if (read_cache_preload(NULL) < 0)
		return error(_("index file corrupt"));

	resolve_undo_clear();
	if (opts->force) {
		ret = reset_tree(get_commit_tree(new_branch_info->commit),
				 opts, 1, writeout_error);
		if (ret)
			return ret;
	} else {
		struct tree_desc trees[2];
		struct tree *tree;
		struct unpack_trees_options topts;

		memset(&topts, 0, sizeof(topts));
		topts.head_idx = -1;
		topts.src_index = &the_index;
		topts.dst_index = &the_index;

		setup_unpack_trees_porcelain(&topts, "checkout");

		refresh_cache(REFRESH_QUIET);

		if (unmerged_cache()) {
			error(_("you need to resolve your current index first"));
			return 1;
		}

		/* 2-way merge to the new branch */
		topts.initial_checkout = is_cache_unborn();
		topts.update = 1;
		topts.merge = 1;
		topts.gently = opts->merge && old_branch_info->commit;
		topts.verbose_update = opts->show_progress;
		topts.fn = twoway_merge;
		if (opts->overwrite_ignore) {
			topts.dir = xcalloc(1, sizeof(*topts.dir));
			topts.dir->flags |= DIR_SHOW_IGNORED;
			setup_standard_excludes(topts.dir);
		}
		tree = parse_tree_indirect(old_branch_info->commit ?
					   &old_branch_info->commit->object.oid :
					   the_hash_algo->empty_tree);
		init_tree_desc(&trees[0], tree->buffer, tree->size);
		tree = parse_tree_indirect(&new_branch_info->commit->object.oid);
		init_tree_desc(&trees[1], tree->buffer, tree->size);

		ret = unpack_trees(2, trees, &topts);
		if (ret == -1) {
			/*
			 * Unpack couldn't do a trivial merge; either
			 * give up or do a real merge, depending on
			 * whether the merge flag was used.
			 */
			struct tree *result;
			struct tree *work;
			struct merge_options o;
			if (!opts->merge)
				return 1;

			/*
			 * Without old_branch_info->commit, the below is the same as
			 * the two-tree unpack we already tried and failed.
			 */
			if (!old_branch_info->commit)
				return 1;

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
			/*
			 * NEEDSWORK: carrying over local changes
			 * when branches have different end-of-line
			 * normalization (or clean+smudge rules) is
			 * a pain; plumb in an option to set
			 * o.renormalize?
			 */
			init_merge_options(&o);
			o.verbosity = 0;
			work = write_tree_from_memory(&o);

			ret = reset_tree(get_commit_tree(new_branch_info->commit),
					 opts, 1,
					 writeout_error);
			if (ret)
				return ret;
			o.ancestor = old_branch_info->name;
			o.branch1 = new_branch_info->name;
			o.branch2 = "local";
			ret = merge_trees(&o,
					  get_commit_tree(new_branch_info->commit),
					  work,
					  get_commit_tree(old_branch_info->commit),
					  &result);
			if (ret < 0)
				exit(128);
			ret = reset_tree(get_commit_tree(new_branch_info->commit),
					 opts, 0,
					 writeout_error);
			strbuf_release(&o.obuf);
			if (ret)
				return ret;
		}
	}

	if (!active_cache_tree)
		active_cache_tree = cache_tree();

	if (!cache_tree_fully_valid(active_cache_tree))
		cache_tree_update(&the_index, WRITE_TREE_SILENT | WRITE_TREE_REPAIR);

	if (write_locked_index(&the_index, &lock_file, COMMIT_LOCK))
		die(_("unable to write new index file"));

	if (!opts->force && !opts->quiet)
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

				ret = safe_create_reflog(refname, 1, &err);
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
			create_branch(opts->new_branch, new_branch_info->name,
				      opts->new_branch_force ? 1 : 0,
				      opts->new_branch_force ? 1 : 0,
				      opts->new_branch_log,
				      opts->quiet,
				      opts->track);
		new_branch_info->name = opts->new_branch;
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
		strbuf_insert(&msg, 0, reflog_msg, strlen(reflog_msg));

	if (!strcmp(new_branch_info->name, "HEAD") && !new_branch_info->path && !opts->force_detach) {
		/* Nothing to do. */
	} else if (opts->force_detach || !new_branch_info->path) {	/* No longer on any branch. */
		update_ref(msg.buf, "HEAD", &new_branch_info->commit->object.oid, NULL,
			   REF_NO_DEREF, UPDATE_REFS_DIE_ON_ERR);
		if (!opts->quiet) {
			if (old_branch_info->path &&
			    advice_detached_head && !opts->force_detach)
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
	remove_branch_state();
	strbuf_release(&msg);
	if (!opts->quiet &&
	    (new_branch_info->path || (!opts->force_detach && !strcmp(new_branch_info->name, "HEAD"))))
		report_tracking(new_branch_info);
}

static int add_pending_uninteresting_ref(const char *refname,
					 const struct object_id *oid,
					 int flags, void *cb_data)
{
	add_pending_oid(cb_data, refname, oid, UNINTERESTING);
	return 0;
}

static void describe_one_orphan(struct strbuf *sb, struct commit *commit)
{
	strbuf_addstr(sb, "  ");
	strbuf_add_unique_abbrev(sb, &commit->object.oid, DEFAULT_ABBREV);
	strbuf_addch(sb, ' ');
	if (!parse_commit(commit))
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

	if (advice_detached_head)
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
			find_unique_abbrev(&commit->object.oid, DEFAULT_ABBREV));
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

	init_revisions(&revs, NULL);
	setup_revisions(0, NULL, &revs, NULL);

	object->flags &= ~UNINTERESTING;
	add_pending_object(&revs, object, oid_to_hex(&object->oid));

	for_each_ref(add_pending_uninteresting_ref, &revs);
	add_pending_oid(&revs, "HEAD", &new_commit->object.oid, UNINTERESTING);

	if (prepare_revision_walk(&revs))
		die(_("internal error in revision walk"));
	if (!(old_commit->object.flags & UNINTERESTING))
		suggest_reattach(old_commit, &revs);
	else
		describe_detached_head(_("Previous HEAD position was"), old_commit);

	/* Clean up objects used, as they will be reused. */
	clear_commit_marks_all(ALL_REV_FLAGS);
}

static int switch_branches(const struct checkout_opts *opts,
			   struct branch_info *new_branch_info)
{
	int ret = 0;
	struct branch_info old_branch_info;
	void *path_to_free;
	struct object_id rev;
	int flag, writeout_error = 0;
	memset(&old_branch_info, 0, sizeof(old_branch_info));
	old_branch_info.path = path_to_free = resolve_refdup("HEAD", 0, &rev, &flag);
	if (old_branch_info.path)
		old_branch_info.commit = lookup_commit_reference_gently(&rev, 1);
	if (!(flag & REF_ISSYMREF))
		old_branch_info.path = NULL;

	if (old_branch_info.path)
		skip_prefix(old_branch_info.path, "refs/heads/", &old_branch_info.name);

	if (!new_branch_info->name) {
		new_branch_info->name = "HEAD";
		new_branch_info->commit = old_branch_info.commit;
		if (!new_branch_info->commit)
			die(_("You are on a branch yet to be born"));
		parse_commit_or_die(new_branch_info->commit);
	}

	ret = merge_working_tree(opts, &old_branch_info, new_branch_info, &writeout_error);
	if (ret) {
		free(path_to_free);
		return ret;
	}

	if (!opts->quiet && !old_branch_info.path && old_branch_info.commit && new_branch_info->commit != old_branch_info.commit)
		orphaned_commit_warning(old_branch_info.commit, new_branch_info->commit);

	update_refs_for_switch(opts, &old_branch_info, new_branch_info);

	ret = post_checkout_hook(old_branch_info.commit, new_branch_info->commit, 1);
	free(path_to_free);
	return ret || writeout_error;
}

static int git_checkout_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "diff.ignoresubmodules")) {
		struct checkout_opts *opts = cb;
		handle_ignore_submodules_arg(&opts->diff_options, value);
		return 0;
	}

	if (starts_with(var, "submodule."))
		return git_default_submodule_config(var, value, NULL);

	return git_xmerge_config(var, value, NULL);
}

static int parse_branchname_arg(int argc, const char **argv,
				int dwim_new_local_branch_ok,
				struct branch_info *new_branch_info,
				struct checkout_opts *opts,
				struct object_id *rev)
{
	struct tree **source_tree = &opts->source_tree;
	const char **new_branch = &opts->new_branch;
	int argcount = 0;
	struct object_id branch_rev;
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
	 *       or <something> is not a path, no -t or -b was given, and
	 *       and there is a tracking branch whose name is <something>
	 *       in one and only one remote, then this is a short-hand to
	 *       fork local <something> from that remote-tracking branch.
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

	arg = argv[0];
	dash_dash_pos = -1;
	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--")) {
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

	if (!strcmp(arg, "-"))
		arg = "@{-1}";

	if (get_oid_mb(arg, rev)) {
		/*
		 * Either case (3) or (4), with <something> not being
		 * a commit, or an attempt to use case (1) with an
		 * invalid ref.
		 *
		 * It's likely an error, but we need to find out if
		 * we should auto-create the branch, case (3).(b).
		 */
		int recover_with_dwim = dwim_new_local_branch_ok;

		if (!has_dash_dash &&
		    (check_filename(opts->prefix, arg) || !no_wildcard(arg)))
			recover_with_dwim = 0;
		/*
		 * Accept "git checkout foo" and "git checkout foo --"
		 * as candidates for dwim.
		 */
		if (!(argc == 1 && !has_dash_dash) &&
		    !(argc == 2 && has_dash_dash))
			recover_with_dwim = 0;

		if (recover_with_dwim) {
			const char *remote = unique_tracking_name(arg, rev);
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

	new_branch_info->name = arg;
	setup_branch_path(new_branch_info);

	if (!check_refname_format(new_branch_info->path, 0) &&
	    !read_ref(new_branch_info->path, &branch_rev))
		oidcpy(rev, &branch_rev);
	else
		new_branch_info->path = NULL; /* not an existing branch */

	new_branch_info->commit = lookup_commit_reference_gently(rev, 1);
	if (!new_branch_info->commit) {
		/* not a commit */
		*source_tree = parse_tree_indirect(rev);
	} else {
		parse_commit_or_die(new_branch_info->commit);
		*source_tree = get_commit_tree(new_branch_info->commit);
	}

	if (!*source_tree)                   /* case (1): want a tree */
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
	} else {
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

static int checkout_branch(struct checkout_opts *opts,
			   struct branch_info *new_branch_info)
{
	if (opts->pathspec.nr)
		die(_("paths cannot be used with switching branches"));

	if (opts->patch_mode)
		die(_("'%s' cannot be used with switching branches"),
		    "--patch");

	if (opts->writeout_stage)
		die(_("'%s' cannot be used with switching branches"),
		    "--ours/--theirs");

	if (opts->force && opts->merge)
		die(_("'%s' cannot be used with '%s'"), "-f", "-m");

	if (opts->force_detach && opts->new_branch)
		die(_("'%s' cannot be used with '%s'"),
		    "--detach", "-b/-B/--orphan");

	if (opts->new_orphan_branch) {
		if (opts->track != BRANCH_TRACK_UNSPECIFIED)
			die(_("'%s' cannot be used with '%s'"), "--orphan", "-t");
	} else if (opts->force_detach) {
		if (opts->track != BRANCH_TRACK_UNSPECIFIED)
			die(_("'%s' cannot be used with '%s'"), "--detach", "-t");
	} else if (opts->track == BRANCH_TRACK_UNSPECIFIED)
		opts->track = git_branch_track;

	if (new_branch_info->name && !new_branch_info->commit)
		die(_("Cannot switch branch to a non-commit '%s'"),
		    new_branch_info->name);

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

int cmd_checkout(int argc, const char **argv, const char *prefix)
{
	struct checkout_opts opts;
	struct branch_info new_branch_info;
	char *conflict_style = NULL;
	int dwim_new_local_branch = 1;
	struct option options[] = {
		OPT__QUIET(&opts.quiet, N_("suppress progress reporting")),
		OPT_STRING('b', NULL, &opts.new_branch, N_("branch"),
			   N_("create and checkout a new branch")),
		OPT_STRING('B', NULL, &opts.new_branch_force, N_("branch"),
			   N_("create/reset and checkout a branch")),
		OPT_BOOL('l', NULL, &opts.new_branch_log, N_("create reflog for new branch")),
		OPT_BOOL(0, "detach", &opts.force_detach, N_("detach HEAD at named commit")),
		OPT_SET_INT('t', "track",  &opts.track, N_("set upstream info for new branch"),
			BRANCH_TRACK_EXPLICIT),
		OPT_STRING(0, "orphan", &opts.new_orphan_branch, N_("new-branch"), N_("new unparented branch")),
		OPT_SET_INT('2', "ours", &opts.writeout_stage, N_("checkout our version for unmerged files"),
			    2),
		OPT_SET_INT('3', "theirs", &opts.writeout_stage, N_("checkout their version for unmerged files"),
			    3),
		OPT__FORCE(&opts.force, N_("force checkout (throw away local modifications)"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_BOOL('m', "merge", &opts.merge, N_("perform a 3-way merge with the new branch")),
		OPT_BOOL_F(0, "overwrite-ignore", &opts.overwrite_ignore,
			   N_("update ignored files (default)"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_STRING(0, "conflict", &conflict_style, N_("style"),
			   N_("conflict style (merge or diff3)")),
		OPT_BOOL('p', "patch", &opts.patch_mode, N_("select hunks interactively")),
		OPT_BOOL(0, "ignore-skip-worktree-bits", &opts.ignore_skipworktree,
			 N_("do not limit pathspecs to sparse entries only")),
		OPT_HIDDEN_BOOL(0, "guess", &dwim_new_local_branch,
				N_("second guess 'git checkout <no-such-branch>'")),
		OPT_BOOL(0, "ignore-other-worktrees", &opts.ignore_other_worktrees,
			 N_("do not check if another worktree is holding the given ref")),
		{ OPTION_CALLBACK, 0, "recurse-submodules", NULL,
			    "checkout", "control recursive updating of submodules",
			    PARSE_OPT_OPTARG, option_parse_recurse_submodules_worktree_updater },
		OPT_BOOL(0, "progress", &opts.show_progress, N_("force progress reporting")),
		OPT_END(),
	};

	memset(&opts, 0, sizeof(opts));
	memset(&new_branch_info, 0, sizeof(new_branch_info));
	opts.overwrite_ignore = 1;
	opts.prefix = prefix;
	opts.show_progress = -1;

	git_config(git_checkout_config, &opts);

	opts.track = BRANCH_TRACK_UNSPECIFIED;

	argc = parse_options(argc, argv, prefix, options, checkout_usage,
			     PARSE_OPT_KEEP_DASHDASH);

	if (opts.show_progress < 0) {
		if (opts.quiet)
			opts.show_progress = 0;
		else
			opts.show_progress = isatty(2);
	}

	if (conflict_style) {
		opts.merge = 1; /* implied */
		git_xmerge_config("merge.conflictstyle", conflict_style, NULL);
	}

	if ((!!opts.new_branch + !!opts.new_branch_force + !!opts.new_orphan_branch) > 1)
		die(_("-b, -B and --orphan are mutually exclusive"));

	/*
	 * From here on, new_branch will contain the branch to be checked out,
	 * and new_branch_force and new_orphan_branch will tell us which one of
	 * -b/-B/--orphan is being used.
	 */
	if (opts.new_branch_force)
		opts.new_branch = opts.new_branch_force;

	if (opts.new_orphan_branch)
		opts.new_branch = opts.new_orphan_branch;

	/* --track without -b/-B/--orphan should DWIM */
	if (opts.track != BRANCH_TRACK_UNSPECIFIED && !opts.new_branch) {
		const char *argv0 = argv[0];
		if (!argc || !strcmp(argv0, "--"))
			die (_("--track needs a branch name"));
		skip_prefix(argv0, "refs/", &argv0);
		skip_prefix(argv0, "remotes/", &argv0);
		argv0 = strchr(argv0, '/');
		if (!argv0 || !argv0[1])
			die (_("Missing branch name; try -b"));
		opts.new_branch = argv0 + 1;
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
	if (argc) {
		struct object_id rev;
		int dwim_ok =
			!opts.patch_mode &&
			dwim_new_local_branch &&
			opts.track == BRANCH_TRACK_UNSPECIFIED &&
			!opts.new_branch;
		int n = parse_branchname_arg(argc, argv, dwim_ok,
					     &new_branch_info, &opts, &rev);
		argv += n;
		argc -= n;
	}

	if (argc) {
		parse_pathspec(&opts.pathspec, 0,
			       opts.patch_mode ? PATHSPEC_PREFIX_ORIGIN : 0,
			       prefix, argv);

		if (!opts.pathspec.nr)
			die(_("invalid path specification"));

		/*
		 * Try to give more helpful suggestion.
		 * new_branch && argc > 1 will be caught later.
		 */
		if (opts.new_branch && argc == 1)
			die(_("'%s' is not a commit and a branch '%s' cannot be created from it"),
				argv[0], opts.new_branch);

		if (opts.force_detach)
			die(_("git checkout: --detach does not take a path argument '%s'"),
			    argv[0]);

		if (1 < !!opts.writeout_stage + !!opts.force + !!opts.merge)
			die(_("git checkout: --ours/--theirs, --force and --merge are incompatible when\n"
			      "checking out of the index."));
	}

	if (opts.new_branch) {
		struct strbuf buf = STRBUF_INIT;

		if (opts.new_branch_force)
			opts.branch_exists = validate_branchname(opts.new_branch, &buf);
		else
			opts.branch_exists =
				validate_new_branchname(opts.new_branch, &buf, 0);
		strbuf_release(&buf);
	}

	UNLEAK(opts);
	if (opts.patch_mode || opts.pathspec.nr)
		return checkout_paths(&opts, new_branch_info.name);
	else
		return checkout_branch(&opts, &new_branch_info);
}
