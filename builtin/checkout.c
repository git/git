#include "cache.h"
#include "builtin.h"
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
#include "submodule.h"
#include "argv-array.h"

static const char * const checkout_usage[] = {
	N_("git checkout [options] <branch>"),
	N_("git checkout [options] [<branch>] -- <file>..."),
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

	const char *new_branch;
	const char *new_branch_force;
	const char *new_orphan_branch;
	int new_branch_log;
	enum branch_track track;
	struct diff_options diff_options;

	int branch_exists;
	const char *prefix;
	const char **pathspec;
	struct tree *source_tree;
};

static int post_checkout_hook(struct commit *old, struct commit *new,
			      int changed)
{
	return run_hook(NULL, "post-checkout",
			sha1_to_hex(old ? old->object.sha1 : null_sha1),
			sha1_to_hex(new ? new->object.sha1 : null_sha1),
			changed ? "1" : "0", NULL);
	/* "new" can be NULL when checking out from the index before
	   a commit exists. */

}

static int update_some(const unsigned char *sha1, const char *base, int baselen,
		const char *pathname, unsigned mode, int stage, void *context)
{
	int len;
	struct cache_entry *ce;

	if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	len = baselen + strlen(pathname);
	ce = xcalloc(1, cache_entry_size(len));
	hashcpy(ce->sha1, sha1);
	memcpy(ce->name, base, baselen);
	memcpy(ce->name + baselen, pathname, len - baselen);
	ce->ce_flags = create_ce_flags(0) | CE_UPDATE;
	ce->ce_namelen = len;
	ce->ce_mode = create_ce_mode(mode);
	add_cache_entry(ce, ADD_CACHE_OK_TO_ADD | ADD_CACHE_OK_TO_REPLACE);
	return 0;
}

static int read_tree_some(struct tree *tree, const char **pathspec)
{
	struct pathspec ps;
	init_pathspec(&ps, pathspec);
	read_tree_recursive(tree, "", 0, 0, &ps, update_some, NULL);
	free_pathspec(&ps);

	/* update the index with the given tree's info
	 * for all args, expanding wildcards, and exit
	 * with any non-zero return code.
	 */
	return 0;
}

static int skip_same_name(struct cache_entry *ce, int pos)
{
	while (++pos < active_nr &&
	       !strcmp(active_cache[pos]->name, ce->name))
		; /* skip */
	return pos;
}

static int check_stage(int stage, struct cache_entry *ce, int pos)
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

static int check_stages(unsigned stages, struct cache_entry *ce, int pos)
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

static int checkout_stage(int stage, struct cache_entry *ce, int pos,
			  struct checkout *state)
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

static int checkout_merged(int pos, struct checkout *state)
{
	struct cache_entry *ce = active_cache[pos];
	const char *path = ce->name;
	mmfile_t ancestor, ours, theirs;
	int status;
	unsigned char sha1[20];
	mmbuffer_t result_buf;
	unsigned char threeway[3][20];
	unsigned mode = 0;

	memset(threeway, 0, sizeof(threeway));
	while (pos < active_nr) {
		int stage;
		stage = ce_stage(ce);
		if (!stage || strcmp(path, ce->name))
			break;
		hashcpy(threeway[stage - 1], ce->sha1);
		if (stage == 2)
			mode = create_ce_mode(ce->ce_mode);
		pos++;
		ce = active_cache[pos];
	}
	if (is_null_sha1(threeway[1]) || is_null_sha1(threeway[2]))
		return error(_("path '%s' does not have necessary versions"), path);

	read_mmblob(&ancestor, threeway[0]);
	read_mmblob(&ours, threeway[1]);
	read_mmblob(&theirs, threeway[2]);

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
	 * and create a phony cache entry just to leak.  This hack is
	 * primarily to get to the write_entry() machinery that massages
	 * the contents to work-tree format and writes out which only
	 * allows it for a cache entry.  The code in write_entry() needs
	 * to be refactored to allow us to feed a <buffer, size, mode>
	 * instead of a cache entry.  Such a refactoring would help
	 * merge_recursive as well (it also writes the merge result to the
	 * object database even when it may contain conflicts).
	 */
	if (write_sha1_file(result_buf.ptr, result_buf.size,
			    blob_type, sha1))
		die(_("Unable to add merge result for '%s'"), path);
	ce = make_cache_entry(mode, sha1, path, 2, 0);
	if (!ce)
		die(_("make_cache_entry failed for path '%s'"), path);
	status = checkout_entry(ce, state, NULL);
	return status;
}

static int checkout_paths(const struct checkout_opts *opts,
			  const char *revision)
{
	int pos;
	struct checkout state;
	static char *ps_matched;
	unsigned char rev[20];
	int flag;
	struct commit *head;
	int errs = 0;
	int stage = opts->writeout_stage;
	int merge = opts->merge;
	int newfd;
	struct lock_file *lock_file;

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
					   opts->pathspec);

	lock_file = xcalloc(1, sizeof(struct lock_file));

	newfd = hold_locked_index(lock_file, 1);
	if (read_cache_preload(opts->pathspec) < 0)
		return error(_("corrupt index file"));

	if (opts->source_tree)
		read_tree_some(opts->source_tree, opts->pathspec);

	for (pos = 0; opts->pathspec[pos]; pos++)
		;
	ps_matched = xcalloc(1, pos);

	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];
		if (opts->source_tree && !(ce->ce_flags & CE_UPDATE))
			continue;
		match_pathspec(opts->pathspec, ce->name, ce_namelen(ce), 0, ps_matched);
	}

	if (report_path_error(ps_matched, opts->pathspec, opts->prefix))
		return 1;

	/* "checkout -m path" to recreate conflicted state */
	if (opts->merge)
		unmerge_cache(opts->pathspec);

	/* Any unmerged paths? */
	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];
		if (match_pathspec(opts->pathspec, ce->name, ce_namelen(ce), 0, NULL)) {
			if (!ce_stage(ce))
				continue;
			if (opts->force) {
				warning(_("path '%s' is unmerged"), ce->name);
			} else if (stage) {
				errs |= check_stage(stage, ce, pos);
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
	memset(&state, 0, sizeof(state));
	state.force = 1;
	state.refresh_cache = 1;
	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];
		if (opts->source_tree && !(ce->ce_flags & CE_UPDATE))
			continue;
		if (match_pathspec(opts->pathspec, ce->name, ce_namelen(ce), 0, NULL)) {
			if (!ce_stage(ce)) {
				errs |= checkout_entry(ce, &state, NULL);
				continue;
			}
			if (stage)
				errs |= checkout_stage(stage, ce, pos, &state);
			else if (merge)
				errs |= checkout_merged(pos, &state);
			pos = skip_same_name(ce, pos) - 1;
		}
	}

	if (write_cache(newfd, active_cache, active_nr) ||
	    commit_locked_index(lock_file))
		die(_("unable to write new index file"));

	read_ref_full("HEAD", rev, 0, &flag);
	head = lookup_commit_reference_gently(rev, 1);

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
	parse_commit(commit);
	pp_commit_easy(CMIT_FMT_ONELINE, commit, &sb);
	fprintf(stderr, "%s %s... %s\n", msg,
		find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV), sb.buf);
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
	opts.verbose_update = !o->quiet && isatty(2);
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
};

static void setup_branch_path(struct branch_info *branch)
{
	struct strbuf buf = STRBUF_INIT;

	strbuf_branchname(&buf, branch->name);
	if (strcmp(buf.buf, branch->name))
		branch->name = xstrdup(buf.buf);
	strbuf_splice(&buf, 0, 0, "refs/heads/", 11);
	branch->path = strbuf_detach(&buf, NULL);
}

static int merge_working_tree(const struct checkout_opts *opts,
			      struct branch_info *old,
			      struct branch_info *new,
			      int *writeout_error)
{
	int ret;
	struct lock_file *lock_file = xcalloc(1, sizeof(struct lock_file));
	int newfd = hold_locked_index(lock_file, 1);

	if (read_cache_preload(NULL) < 0)
		return error(_("corrupt index file"));

	resolve_undo_clear();
	if (opts->force) {
		ret = reset_tree(new->commit->tree, opts, 1, writeout_error);
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
		topts.gently = opts->merge && old->commit;
		topts.verbose_update = !opts->quiet && isatty(2);
		topts.fn = twoway_merge;
		if (opts->overwrite_ignore) {
			topts.dir = xcalloc(1, sizeof(*topts.dir));
			topts.dir->flags |= DIR_SHOW_IGNORED;
			setup_standard_excludes(topts.dir);
		}
		tree = parse_tree_indirect(old->commit ?
					   old->commit->object.sha1 :
					   EMPTY_TREE_SHA1_BIN);
		init_tree_desc(&trees[0], tree->buffer, tree->size);
		tree = parse_tree_indirect(new->commit->object.sha1);
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
			 * Without old->commit, the below is the same as
			 * the two-tree unpack we already tried and failed.
			 */
			if (!old->commit)
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

			ret = reset_tree(new->commit->tree, opts, 1,
					 writeout_error);
			if (ret)
				return ret;
			o.ancestor = old->name;
			o.branch1 = new->name;
			o.branch2 = "local";
			merge_trees(&o, new->commit->tree, work,
				old->commit->tree, &result);
			ret = reset_tree(new->commit->tree, opts, 0,
					 writeout_error);
			if (ret)
				return ret;
		}
	}

	if (write_cache(newfd, active_cache, active_nr) ||
	    commit_locked_index(lock_file))
		die(_("unable to write new index file"));

	if (!opts->force && !opts->quiet)
		show_local_changes(&new->commit->object, &opts->diff_options);

	return 0;
}

static void report_tracking(struct branch_info *new)
{
	struct strbuf sb = STRBUF_INIT;
	struct branch *branch = branch_get(new->name);

	if (!format_tracking_info(branch, &sb))
		return;
	fputs(sb.buf, stdout);
	strbuf_release(&sb);
}

static void update_refs_for_switch(const struct checkout_opts *opts,
				   struct branch_info *old,
				   struct branch_info *new)
{
	struct strbuf msg = STRBUF_INIT;
	const char *old_desc;
	if (opts->new_branch) {
		if (opts->new_orphan_branch) {
			if (opts->new_branch_log && !log_all_ref_updates) {
				int temp;
				char log_file[PATH_MAX];
				char *ref_name = mkpath("refs/heads/%s", opts->new_orphan_branch);

				temp = log_all_ref_updates;
				log_all_ref_updates = 1;
				if (log_ref_setup(ref_name, log_file, sizeof(log_file))) {
					fprintf(stderr, _("Can not do reflog for '%s'\n"),
					    opts->new_orphan_branch);
					log_all_ref_updates = temp;
					return;
				}
				log_all_ref_updates = temp;
			}
		}
		else
			create_branch(old->name, opts->new_branch, new->name,
				      opts->new_branch_force ? 1 : 0,
				      opts->new_branch_log,
				      opts->new_branch_force ? 1 : 0,
				      opts->quiet,
				      opts->track);
		new->name = opts->new_branch;
		setup_branch_path(new);
	}

	old_desc = old->name;
	if (!old_desc && old->commit)
		old_desc = sha1_to_hex(old->commit->object.sha1);
	strbuf_addf(&msg, "checkout: moving from %s to %s",
		    old_desc ? old_desc : "(invalid)", new->name);

	if (!strcmp(new->name, "HEAD") && !new->path && !opts->force_detach) {
		/* Nothing to do. */
	} else if (opts->force_detach || !new->path) {	/* No longer on any branch. */
		update_ref(msg.buf, "HEAD", new->commit->object.sha1, NULL,
			   REF_NODEREF, DIE_ON_ERR);
		if (!opts->quiet) {
			if (old->path && advice_detached_head)
				detach_advice(new->name);
			describe_detached_head(_("HEAD is now at"), new->commit);
		}
	} else if (new->path) {	/* Switch branches. */
		create_symref("HEAD", new->path, msg.buf);
		if (!opts->quiet) {
			if (old->path && !strcmp(new->path, old->path)) {
				if (opts->new_branch_force)
					fprintf(stderr, _("Reset branch '%s'\n"),
						new->name);
				else
					fprintf(stderr, _("Already on '%s'\n"),
						new->name);
			} else if (opts->new_branch) {
				if (opts->branch_exists)
					fprintf(stderr, _("Switched to and reset branch '%s'\n"), new->name);
				else
					fprintf(stderr, _("Switched to a new branch '%s'\n"), new->name);
			} else {
				fprintf(stderr, _("Switched to branch '%s'\n"),
					new->name);
			}
		}
		if (old->path && old->name) {
			char log_file[PATH_MAX], ref_file[PATH_MAX];

			git_snpath(log_file, sizeof(log_file), "logs/%s", old->path);
			git_snpath(ref_file, sizeof(ref_file), "%s", old->path);
			if (!file_exists(ref_file) && file_exists(log_file))
				remove_path(log_file);
		}
	}
	remove_branch_state();
	strbuf_release(&msg);
	if (!opts->quiet &&
	    (new->path || (!opts->force_detach && !strcmp(new->name, "HEAD"))))
		report_tracking(new);
}

static int add_pending_uninteresting_ref(const char *refname,
					 const unsigned char *sha1,
					 int flags, void *cb_data)
{
	add_pending_sha1(cb_data, refname, sha1, UNINTERESTING);
	return 0;
}

static void describe_one_orphan(struct strbuf *sb, struct commit *commit)
{
	parse_commit(commit);
	strbuf_addstr(sb, "  ");
	strbuf_addstr(sb,
		find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV));
	strbuf_addch(sb, ' ');
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
			_(
			"If you want to keep them by creating a new branch, "
			"this may be a good time\nto do so with:\n\n"
			" git branch new_branch_name %s\n\n"),
			sha1_to_hex(commit->object.sha1));
}

/*
 * We are about to leave commit that was at the tip of a detached
 * HEAD.  If it is not reachable from any ref, this is the last chance
 * for the user to do so without resorting to reflog.
 */
static void orphaned_commit_warning(struct commit *old, struct commit *new)
{
	struct rev_info revs;
	struct object *object = &old->object;
	struct object_array refs;

	init_revisions(&revs, NULL);
	setup_revisions(0, NULL, &revs, NULL);

	object->flags &= ~UNINTERESTING;
	add_pending_object(&revs, object, sha1_to_hex(object->sha1));

	for_each_ref(add_pending_uninteresting_ref, &revs);
	add_pending_sha1(&revs, "HEAD", new->object.sha1, UNINTERESTING);

	refs = revs.pending;
	revs.leak_pending = 1;

	if (prepare_revision_walk(&revs))
		die(_("internal error in revision walk"));
	if (!(old->object.flags & UNINTERESTING))
		suggest_reattach(old, &revs);
	else
		describe_detached_head(_("Previous HEAD position was"), old);

	clear_commit_marks_for_object_array(&refs, ALL_REV_FLAGS);
	free(refs.objects);
}

static int switch_branches(const struct checkout_opts *opts,
			   struct branch_info *new)
{
	int ret = 0;
	struct branch_info old;
	void *path_to_free;
	unsigned char rev[20];
	int flag, writeout_error = 0;
	memset(&old, 0, sizeof(old));
	old.path = path_to_free = resolve_refdup("HEAD", rev, 0, &flag);
	old.commit = lookup_commit_reference_gently(rev, 1);
	if (!(flag & REF_ISSYMREF))
		old.path = NULL;

	if (old.path && !prefixcmp(old.path, "refs/heads/"))
		old.name = old.path + strlen("refs/heads/");

	if (!new->name) {
		new->name = "HEAD";
		new->commit = old.commit;
		if (!new->commit)
			die(_("You are on a branch yet to be born"));
		parse_commit(new->commit);
	}

	ret = merge_working_tree(opts, &old, new, &writeout_error);
	if (ret) {
		free(path_to_free);
		return ret;
	}

	if (!opts->quiet && !old.path && old.commit && new->commit != old.commit)
		orphaned_commit_warning(old.commit, new->commit);

	update_refs_for_switch(opts, &old, new);

	ret = post_checkout_hook(old.commit, new->commit, 1);
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

	if (!prefixcmp(var, "submodule."))
		return parse_submodule_config_option(var, value);

	return git_xmerge_config(var, value, NULL);
}

struct tracking_name_data {
	const char *name;
	char *remote;
	int unique;
};

static int check_tracking_name(const char *refname, const unsigned char *sha1,
			       int flags, void *cb_data)
{
	struct tracking_name_data *cb = cb_data;
	const char *slash;

	if (prefixcmp(refname, "refs/remotes/"))
		return 0;
	slash = strchr(refname + 13, '/');
	if (!slash || strcmp(slash + 1, cb->name))
		return 0;
	if (cb->remote) {
		cb->unique = 0;
		return 0;
	}
	cb->remote = xstrdup(refname);
	return 0;
}

static const char *unique_tracking_name(const char *name)
{
	struct tracking_name_data cb_data = { NULL, NULL, 1 };
	cb_data.name = name;
	for_each_ref(check_tracking_name, &cb_data);
	if (cb_data.unique)
		return cb_data.remote;
	free(cb_data.remote);
	return NULL;
}

static int parse_branchname_arg(int argc, const char **argv,
				int dwim_new_local_branch_ok,
				struct branch_info *new,
				struct tree **source_tree,
				unsigned char rev[20],
				const char **new_branch)
{
	int argcount = 0;
	unsigned char branch_rev[20];
	const char *arg;
	int has_dash_dash;

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
	 * case 3: git checkout <something> [<paths>]
	 *
	 *   With no paths, if <something> is a commit, that is to
	 *   switch to the branch or detach HEAD at it.  As a special case,
	 *   if <something> is A...B (missing A or B means HEAD but you can
	 *   omit at most one side), and if there is a unique merge base
	 *   between A and B, A...B names that merge base.
	 *
	 *   With no paths, if <something> is _not_ a commit, no -t nor -b
	 *   was given, and there is a tracking branch whose name is
	 *   <something> in one and only one remote, then this is a short-hand
	 *   to fork local <something> from that remote-tracking branch.
	 *
	 *   Otherwise <something> shall not be ambiguous.
	 *   - If it's *only* a reference, treat it like case (1).
	 *   - If it's only a path, treat it like case (2).
	 *   - else: fail.
	 *
	 */
	if (!argc)
		return 0;

	if (!strcmp(argv[0], "--"))	/* case (2) */
		return 1;

	arg = argv[0];
	has_dash_dash = (argc > 1) && !strcmp(argv[1], "--");

	if (!strcmp(arg, "-"))
		arg = "@{-1}";

	if (get_sha1_mb(arg, rev)) {
		if (has_dash_dash)          /* case (1) */
			die(_("invalid reference: %s"), arg);
		if (dwim_new_local_branch_ok &&
		    !check_filename(NULL, arg) &&
		    argc == 1) {
			const char *remote = unique_tracking_name(arg);
			if (!remote || get_sha1(remote, rev))
				return argcount;
			*new_branch = arg;
			arg = remote;
			/* DWIMmed to create local branch */
		} else {
			return argcount;
		}
	}

	/* we can't end up being in (2) anymore, eat the argument */
	argcount++;
	argv++;
	argc--;

	new->name = arg;
	setup_branch_path(new);

	if (!check_refname_format(new->path, 0) &&
	    !read_ref(new->path, branch_rev))
		hashcpy(rev, branch_rev);
	else
		new->path = NULL; /* not an existing branch */

	new->commit = lookup_commit_reference_gently(rev, 1);
	if (!new->commit) {
		/* not a commit */
		*source_tree = parse_tree_indirect(rev);
	} else {
		parse_commit(new->commit);
		*source_tree = new->commit->tree;
	}

	if (!*source_tree)                   /* case (1): want a tree */
		die(_("reference is not a tree: %s"), arg);
	if (!has_dash_dash) {/* case (3 -> 1) */
		/*
		 * Do not complain the most common case
		 *	git checkout branch
		 * even if there happen to be a file called 'branch';
		 * it would be extremely annoying.
		 */
		if (argc)
			verify_non_filename(NULL, arg);
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
			   struct branch_info *new)
{
	if (opts->pathspec)
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

	if (new->name && !new->commit)
		die(_("Cannot switch branch to a non-commit '%s'"),
		    new->name);

	if (!new->commit && opts->new_branch) {
		unsigned char rev[20];
		int flag;

		if (!read_ref_full("HEAD", rev, 0, &flag) &&
		    (flag & REF_ISSYMREF) && is_null_sha1(rev))
			return switch_unborn_to_new_branch(opts);
	}
	return switch_branches(opts, new);
}

int cmd_checkout(int argc, const char **argv, const char *prefix)
{
	struct checkout_opts opts;
	struct branch_info new;
	char *conflict_style = NULL;
	int dwim_new_local_branch = 1;
	struct option options[] = {
		OPT__QUIET(&opts.quiet, N_("suppress progress reporting")),
		OPT_STRING('b', NULL, &opts.new_branch, N_("branch"),
			   N_("create and checkout a new branch")),
		OPT_STRING('B', NULL, &opts.new_branch_force, N_("branch"),
			   N_("create/reset and checkout a branch")),
		OPT_BOOLEAN('l', NULL, &opts.new_branch_log, N_("create reflog for new branch")),
		OPT_BOOLEAN(0, "detach", &opts.force_detach, N_("detach the HEAD at named commit")),
		OPT_SET_INT('t', "track",  &opts.track, N_("set upstream info for new branch"),
			BRANCH_TRACK_EXPLICIT),
		OPT_STRING(0, "orphan", &opts.new_orphan_branch, N_("new branch"), N_("new unparented branch")),
		OPT_SET_INT('2', "ours", &opts.writeout_stage, N_("checkout our version for unmerged files"),
			    2),
		OPT_SET_INT('3', "theirs", &opts.writeout_stage, N_("checkout their version for unmerged files"),
			    3),
		OPT__FORCE(&opts.force, N_("force checkout (throw away local modifications)")),
		OPT_BOOLEAN('m', "merge", &opts.merge, N_("perform a 3-way merge with the new branch")),
		OPT_BOOLEAN(0, "overwrite-ignore", &opts.overwrite_ignore, N_("update ignored files (default)")),
		OPT_STRING(0, "conflict", &conflict_style, N_("style"),
			   N_("conflict style (merge or diff3)")),
		OPT_BOOLEAN('p', "patch", &opts.patch_mode, N_("select hunks interactively")),
		{ OPTION_BOOLEAN, 0, "guess", &dwim_new_local_branch, NULL,
		  N_("second guess 'git checkout no-such-branch'"),
		  PARSE_OPT_NOARG | PARSE_OPT_HIDDEN },
		OPT_END(),
	};

	memset(&opts, 0, sizeof(opts));
	memset(&new, 0, sizeof(new));
	opts.overwrite_ignore = 1;
	opts.prefix = prefix;

	gitmodules_config();
	git_config(git_checkout_config, &opts);

	opts.track = BRANCH_TRACK_UNSPECIFIED;

	argc = parse_options(argc, argv, prefix, options, checkout_usage,
			     PARSE_OPT_KEEP_DASHDASH);

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
		if (!prefixcmp(argv0, "refs/"))
			argv0 += 5;
		if (!prefixcmp(argv0, "remotes/"))
			argv0 += 8;
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
		unsigned char rev[20];
		int dwim_ok =
			!opts.patch_mode &&
			dwim_new_local_branch &&
			opts.track == BRANCH_TRACK_UNSPECIFIED &&
			!opts.new_branch;
		int n = parse_branchname_arg(argc, argv, dwim_ok,
					     &new, &opts.source_tree,
					     rev, &opts.new_branch);
		argv += n;
		argc -= n;
	}

	if (argc) {
		opts.pathspec = get_pathspec(prefix, argv);

		if (!opts.pathspec)
			die(_("invalid path specification"));

		/*
		 * Try to give more helpful suggestion.
		 * new_branch && argc > 1 will be caught later.
		 */
		if (opts.new_branch && argc == 1)
			die(_("Cannot update paths and switch to branch '%s' at the same time.\n"
			      "Did you intend to checkout '%s' which can not be resolved as commit?"),
			    opts.new_branch, argv[0]);

		if (opts.force_detach)
			die(_("git checkout: --detach does not take a path argument '%s'"),
			    argv[0]);

		if (1 < !!opts.writeout_stage + !!opts.force + !!opts.merge)
			die(_("git checkout: --ours/--theirs, --force and --merge are incompatible when\n"
			      "checking out of the index."));
	}

	if (opts.new_branch) {
		struct strbuf buf = STRBUF_INIT;

		opts.branch_exists =
			validate_new_branchname(opts.new_branch, &buf,
						!!opts.new_branch_force,
						!!opts.new_branch_force);

		strbuf_release(&buf);
	}

	if (opts.patch_mode || opts.pathspec)
		return checkout_paths(&opts, new.name);
	else
		return checkout_branch(&opts, &new);
}
