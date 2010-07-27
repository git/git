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

static const char * const checkout_usage[] = {
	"git checkout [options] <branch>",
	"git checkout [options] [<branch>] -- <file>...",
	NULL,
};

struct checkout_opts {
	int quiet;
	int merge;
	int force;
	int writeout_stage;
	int writeout_error;

	const char *new_branch;
	const char *new_orphan_branch;
	int new_branch_log;
	enum branch_track track;
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
	ce->ce_flags = create_ce_flags(len, 0);
	ce->ce_mode = create_ce_mode(mode);
	add_cache_entry(ce, ADD_CACHE_OK_TO_ADD | ADD_CACHE_OK_TO_REPLACE);
	return 0;
}

static int read_tree_some(struct tree *tree, const char **pathspec)
{
	read_tree_recursive(tree, "", 0, 0, pathspec, update_some, NULL);

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
	return error("path '%s' does not have %s version",
		     ce->name,
		     (stage == 2) ? "our" : "their");
}

static int check_all_stages(struct cache_entry *ce, int pos)
{
	if (ce_stage(ce) != 1 ||
	    active_nr <= pos + 2 ||
	    strcmp(active_cache[pos+1]->name, ce->name) ||
	    ce_stage(active_cache[pos+1]) != 2 ||
	    strcmp(active_cache[pos+2]->name, ce->name) ||
	    ce_stage(active_cache[pos+2]) != 3)
		return error("path '%s' does not have all three versions",
			     ce->name);
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
	return error("path '%s' does not have %s version",
		     ce->name,
		     (stage == 2) ? "our" : "their");
}

static int checkout_merged(int pos, struct checkout *state)
{
	struct cache_entry *ce = active_cache[pos];
	const char *path = ce->name;
	mmfile_t ancestor, ours, theirs;
	int status;
	unsigned char sha1[20];
	mmbuffer_t result_buf;

	if (ce_stage(ce) != 1 ||
	    active_nr <= pos + 2 ||
	    strcmp(active_cache[pos+1]->name, path) ||
	    ce_stage(active_cache[pos+1]) != 2 ||
	    strcmp(active_cache[pos+2]->name, path) ||
	    ce_stage(active_cache[pos+2]) != 3)
		return error("path '%s' does not have all 3 versions", path);

	read_mmblob(&ancestor, active_cache[pos]->sha1);
	read_mmblob(&ours, active_cache[pos+1]->sha1);
	read_mmblob(&theirs, active_cache[pos+2]->sha1);

	status = ll_merge(&result_buf, path, &ancestor, "base",
			  &ours, "ours", &theirs, "theirs", 0);
	free(ancestor.ptr);
	free(ours.ptr);
	free(theirs.ptr);
	if (status < 0 || !result_buf.ptr) {
		free(result_buf.ptr);
		return error("path '%s': cannot merge", path);
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
		die("Unable to add merge result for '%s'", path);
	ce = make_cache_entry(create_ce_mode(active_cache[pos+1]->ce_mode),
			      sha1,
			      path, 2, 0);
	if (!ce)
		die("make_cache_entry failed for path '%s'", path);
	status = checkout_entry(ce, state, NULL);
	return status;
}

static int checkout_paths(struct tree *source_tree, const char **pathspec,
			  struct checkout_opts *opts)
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
	struct lock_file *lock_file = xcalloc(1, sizeof(struct lock_file));

	newfd = hold_locked_index(lock_file, 1);
	if (read_cache_preload(pathspec) < 0)
		return error("corrupt index file");

	if (source_tree)
		read_tree_some(source_tree, pathspec);

	for (pos = 0; pathspec[pos]; pos++)
		;
	ps_matched = xcalloc(1, pos);

	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];
		match_pathspec(pathspec, ce->name, ce_namelen(ce), 0, ps_matched);
	}

	if (report_path_error(ps_matched, pathspec, 0))
		return 1;

	/* "checkout -m path" to recreate conflicted state */
	if (opts->merge)
		unmerge_cache(pathspec);

	/* Any unmerged paths? */
	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];
		if (match_pathspec(pathspec, ce->name, ce_namelen(ce), 0, NULL)) {
			if (!ce_stage(ce))
				continue;
			if (opts->force) {
				warning("path '%s' is unmerged", ce->name);
			} else if (stage) {
				errs |= check_stage(stage, ce, pos);
			} else if (opts->merge) {
				errs |= check_all_stages(ce, pos);
			} else {
				errs = 1;
				error("path '%s' is unmerged", ce->name);
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
		if (match_pathspec(pathspec, ce->name, ce_namelen(ce), 0, NULL)) {
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
		die("unable to write new index file");

	resolve_ref("HEAD", rev, 0, &flag);
	head = lookup_commit_reference_gently(rev, 1);

	errs |= post_checkout_hook(head, head, 0);
	return errs;
}

static void show_local_changes(struct object *head)
{
	struct rev_info rev;
	/* I think we want full paths, even if we're in a subdirectory. */
	init_revisions(&rev, NULL);
	rev.diffopt.output_format |= DIFF_FORMAT_NAME_STATUS;
	if (diff_setup_done(&rev.diffopt) < 0)
		die("diff_setup_done failed");
	add_pending_object(&rev, head, NULL);
	run_diff_index(&rev, 0);
}

static void describe_detached_head(char *msg, struct commit *commit)
{
	struct strbuf sb = STRBUF_INIT;
	struct pretty_print_context ctx = {0};
	parse_commit(commit);
	pretty_print_commit(CMIT_FMT_ONELINE, commit, &sb, &ctx);
	fprintf(stderr, "%s %s... %s\n", msg,
		find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV), sb.buf);
	strbuf_release(&sb);
}

static int reset_tree(struct tree *tree, struct checkout_opts *o, int worktree)
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
	opts.verbose_update = !o->quiet;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
	parse_tree(tree);
	init_tree_desc(&tree_desc, tree->buffer, tree->size);
	switch (unpack_trees(1, &tree_desc, &opts)) {
	case -2:
		o->writeout_error = 1;
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

static int merge_working_tree(struct checkout_opts *opts,
			      struct branch_info *old, struct branch_info *new)
{
	int ret;
	struct lock_file *lock_file = xcalloc(1, sizeof(struct lock_file));
	int newfd = hold_locked_index(lock_file, 1);

	if (read_cache_preload(NULL) < 0)
		return error("corrupt index file");

	resolve_undo_clear();
	if (opts->force) {
		ret = reset_tree(new->commit->tree, opts, 1);
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

		topts.msgs.not_uptodate_file = "You have local changes to '%s'; cannot switch branches.";

		refresh_cache(REFRESH_QUIET);

		if (unmerged_cache()) {
			error("you need to resolve your current index first");
			return 1;
		}

		/* 2-way merge to the new branch */
		topts.initial_checkout = is_cache_unborn();
		topts.update = 1;
		topts.merge = 1;
		topts.gently = opts->merge && old->commit;
		topts.verbose_update = !opts->quiet;
		topts.fn = twoway_merge;
		topts.dir = xcalloc(1, sizeof(*topts.dir));
		topts.dir->flags |= DIR_SHOW_IGNORED;
		topts.dir->exclude_per_dir = ".gitignore";
		tree = parse_tree_indirect(old->commit ?
					   old->commit->object.sha1 :
					   (unsigned char *)EMPTY_TREE_SHA1_BIN);
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
			init_merge_options(&o);
			o.verbosity = 0;
			work = write_tree_from_memory(&o);

			ret = reset_tree(new->commit->tree, opts, 1);
			if (ret)
				return ret;
			o.ancestor = old->name;
			o.branch1 = new->name;
			o.branch2 = "local";
			merge_trees(&o, new->commit->tree, work,
				old->commit->tree, &result);
			ret = reset_tree(new->commit->tree, opts, 0);
			if (ret)
				return ret;
		}
	}

	if (write_cache(newfd, active_cache, active_nr) ||
	    commit_locked_index(lock_file))
		die("unable to write new index file");

	if (!opts->force && !opts->quiet)
		show_local_changes(&new->commit->object);

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

static void detach_advice(const char *old_path, const char *new_name)
{
	const char fmt[] =
	"Note: checking out '%s'.\n\n"
	"You are in 'detached HEAD' state. You can look around, make experimental\n"
	"changes and commit them, and you can discard any commits you make in this\n"
	"state without impacting any branches by performing another checkout.\n\n"
	"If you want to create a new branch to retain commits you create, you may\n"
	"do so (now or later) by using -b with the checkout command again. Example:\n\n"
	"  git checkout -b new_branch_name\n\n";

	fprintf(stderr, fmt, new_name);
}

static void update_refs_for_switch(struct checkout_opts *opts,
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
					fprintf(stderr, "Can not do reflog for '%s'\n",
					    opts->new_orphan_branch);
					log_all_ref_updates = temp;
					return;
				}
				log_all_ref_updates = temp;
			}
		}
		else
			create_branch(old->name, opts->new_branch, new->name, 0,
				      opts->new_branch_log, opts->track);
		new->name = opts->new_branch;
		setup_branch_path(new);
	}

	old_desc = old->name;
	if (!old_desc && old->commit)
		old_desc = sha1_to_hex(old->commit->object.sha1);
	strbuf_addf(&msg, "checkout: moving from %s to %s",
		    old_desc ? old_desc : "(invalid)", new->name);

	if (new->path) {
		create_symref("HEAD", new->path, msg.buf);
		if (!opts->quiet) {
			if (old->path && !strcmp(new->path, old->path))
				fprintf(stderr, "Already on '%s'\n",
					new->name);
			else
				fprintf(stderr, "Switched to%s branch '%s'\n",
					opts->new_branch ? " a new" : "",
					new->name);
		}
		if (old->path && old->name) {
			char log_file[PATH_MAX], ref_file[PATH_MAX];

			git_snpath(log_file, sizeof(log_file), "logs/%s", old->path);
			git_snpath(ref_file, sizeof(ref_file), "%s", old->path);
			if (!file_exists(ref_file) && file_exists(log_file))
				remove_path(log_file);
		}
	} else if (strcmp(new->name, "HEAD")) {
		update_ref(msg.buf, "HEAD", new->commit->object.sha1, NULL,
			   REF_NODEREF, DIE_ON_ERR);
		if (!opts->quiet) {
			if (old->path && advice_detached_head)
				detach_advice(old->path, new->name);
			describe_detached_head("HEAD is now at", new->commit);
		}
	}
	remove_branch_state();
	strbuf_release(&msg);
	if (!opts->quiet && (new->path || !strcmp(new->name, "HEAD")))
		report_tracking(new);
}

static int switch_branches(struct checkout_opts *opts, struct branch_info *new)
{
	int ret = 0;
	struct branch_info old;
	unsigned char rev[20];
	int flag;
	memset(&old, 0, sizeof(old));
	old.path = resolve_ref("HEAD", rev, 0, &flag);
	old.commit = lookup_commit_reference_gently(rev, 1);
	if (!(flag & REF_ISSYMREF))
		old.path = NULL;

	if (old.path && !prefixcmp(old.path, "refs/heads/"))
		old.name = old.path + strlen("refs/heads/");

	if (!new->name) {
		new->name = "HEAD";
		new->commit = old.commit;
		if (!new->commit)
			die("You are on a branch yet to be born");
		parse_commit(new->commit);
	}

	ret = merge_working_tree(opts, &old, new);
	if (ret)
		return ret;

	/*
	 * If we were on a detached HEAD, but have now moved to
	 * a new commit, we want to mention the old commit once more
	 * to remind the user that it might be lost.
	 */
	if (!opts->quiet && !old.path && old.commit && new->commit != old.commit)
		describe_detached_head("Previous HEAD position was", old.commit);

	update_refs_for_switch(opts, &old, new);

	ret = post_checkout_hook(old.commit, new->commit, 1);
	return ret || opts->writeout_error;
}

static int git_checkout_config(const char *var, const char *value, void *cb)
{
	return git_xmerge_config(var, value, cb);
}

static int interactive_checkout(const char *revision, const char **pathspec,
				struct checkout_opts *opts)
{
	return run_add_interactive(revision, "--patch=checkout", pathspec);
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

int cmd_checkout(int argc, const char **argv, const char *prefix)
{
	struct checkout_opts opts;
	unsigned char rev[20];
	const char *arg;
	struct branch_info new;
	struct tree *source_tree = NULL;
	char *conflict_style = NULL;
	int patch_mode = 0;
	int dwim_new_local_branch = 1;
	struct option options[] = {
		OPT__QUIET(&opts.quiet),
		OPT_STRING('b', NULL, &opts.new_branch, "new branch", "branch"),
		OPT_BOOLEAN('l', NULL, &opts.new_branch_log, "log for new branch"),
		OPT_SET_INT('t', "track",  &opts.track, "track",
			BRANCH_TRACK_EXPLICIT),
		OPT_STRING(0, "orphan", &opts.new_orphan_branch, "new branch", "new unparented branch"),
		OPT_SET_INT('2', "ours", &opts.writeout_stage, "stage",
			    2),
		OPT_SET_INT('3', "theirs", &opts.writeout_stage, "stage",
			    3),
		OPT_BOOLEAN('f', "force", &opts.force, "force"),
		OPT_BOOLEAN('m', "merge", &opts.merge, "merge"),
		OPT_STRING(0, "conflict", &conflict_style, "style",
			   "conflict style (merge or diff3)"),
		OPT_BOOLEAN('p', "patch", &patch_mode, "select hunks interactively"),
		{ OPTION_BOOLEAN, 0, "guess", &dwim_new_local_branch, NULL,
		  "second guess 'git checkout no-such-branch'",
		  PARSE_OPT_NOARG | PARSE_OPT_HIDDEN },
		OPT_END(),
	};
	int has_dash_dash;

	memset(&opts, 0, sizeof(opts));
	memset(&new, 0, sizeof(new));

	git_config(git_checkout_config, NULL);

	opts.track = BRANCH_TRACK_UNSPECIFIED;

	argc = parse_options(argc, argv, prefix, options, checkout_usage,
			     PARSE_OPT_KEEP_DASHDASH);

	if (patch_mode && (opts.track > 0 || opts.new_branch
			   || opts.new_branch_log || opts.merge || opts.force))
		die ("--patch is incompatible with all other options");

	/* --track without -b should DWIM */
	if (0 < opts.track && !opts.new_branch) {
		const char *argv0 = argv[0];
		if (!argc || !strcmp(argv0, "--"))
			die ("--track needs a branch name");
		if (!prefixcmp(argv0, "refs/"))
			argv0 += 5;
		if (!prefixcmp(argv0, "remotes/"))
			argv0 += 8;
		argv0 = strchr(argv0, '/');
		if (!argv0 || !argv0[1])
			die ("Missing branch name; try -b");
		opts.new_branch = argv0 + 1;
	}

	if (opts.new_orphan_branch) {
		if (opts.new_branch)
			die("--orphan and -b are mutually exclusive");
		if (opts.track > 0)
			die("--orphan cannot be used with -t");
		opts.new_branch = opts.new_orphan_branch;
	}

	if (conflict_style) {
		opts.merge = 1; /* implied */
		git_xmerge_config("merge.conflictstyle", conflict_style, NULL);
	}

	if (opts.force && opts.merge)
		die("git checkout: -f and -m are incompatible");

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
	 *   to fork local <something> from that remote tracking branch.
	 *
	 *   Otherwise <something> shall not be ambiguous.
	 *   - If it's *only* a reference, treat it like case (1).
	 *   - If it's only a path, treat it like case (2).
	 *   - else: fail.
	 *
	 */
	if (argc) {
		if (!strcmp(argv[0], "--")) {       /* case (2) */
			argv++;
			argc--;
			goto no_reference;
		}

		arg = argv[0];
		has_dash_dash = (argc > 1) && !strcmp(argv[1], "--");

		if (!strcmp(arg, "-"))
			arg = "@{-1}";

		if (get_sha1_mb(arg, rev)) {
			if (has_dash_dash)          /* case (1) */
				die("invalid reference: %s", arg);
			if (!patch_mode &&
			    dwim_new_local_branch &&
			    opts.track == BRANCH_TRACK_UNSPECIFIED &&
			    !opts.new_branch &&
			    !check_filename(NULL, arg) &&
			    argc == 1) {
				const char *remote = unique_tracking_name(arg);
				if (!remote || get_sha1(remote, rev))
					goto no_reference;
				opts.new_branch = arg;
				arg = remote;
				/* DWIMmed to create local branch */
			}
			else
				goto no_reference;
		}

		/* we can't end up being in (2) anymore, eat the argument */
		argv++;
		argc--;

		new.name = arg;
		if ((new.commit = lookup_commit_reference_gently(rev, 1))) {
			setup_branch_path(&new);

			if ((check_ref_format(new.path) == CHECK_REF_FORMAT_OK) &&
			    resolve_ref(new.path, rev, 1, NULL))
				;
			else
				new.path = NULL;
			parse_commit(new.commit);
			source_tree = new.commit->tree;
		} else
			source_tree = parse_tree_indirect(rev);

		if (!source_tree)                   /* case (1): want a tree */
			die("reference is not a tree: %s", arg);
		if (!has_dash_dash) {/* case (3 -> 1) */
			/*
			 * Do not complain the most common case
			 *	git checkout branch
			 * even if there happen to be a file called 'branch';
			 * it would be extremely annoying.
			 */
			if (argc)
				verify_non_filename(NULL, arg);
		}
		else {
			argv++;
			argc--;
		}
	}

no_reference:

	if (opts.track == BRANCH_TRACK_UNSPECIFIED)
		opts.track = git_branch_track;

	if (argc) {
		const char **pathspec = get_pathspec(prefix, argv);

		if (!pathspec)
			die("invalid path specification");

		if (patch_mode)
			return interactive_checkout(new.name, pathspec, &opts);

		/* Checkout paths */
		if (opts.new_branch) {
			if (argc == 1) {
				die("git checkout: updating paths is incompatible with switching branches.\nDid you intend to checkout '%s' which can not be resolved as commit?", argv[0]);
			} else {
				die("git checkout: updating paths is incompatible with switching branches.");
			}
		}

		if (1 < !!opts.writeout_stage + !!opts.force + !!opts.merge)
			die("git checkout: --ours/--theirs, --force and --merge are incompatible when\nchecking out of the index.");

		return checkout_paths(source_tree, pathspec, &opts);
	}

	if (patch_mode)
		return interactive_checkout(new.name, NULL, &opts);

	if (opts.new_branch) {
		struct strbuf buf = STRBUF_INIT;
		if (strbuf_check_branch_ref(&buf, opts.new_branch))
			die("git checkout: we do not like '%s' as a branch name.",
			    opts.new_branch);
		if (!get_sha1(buf.buf, rev))
			die("git checkout: branch %s already exists", opts.new_branch);
		strbuf_release(&buf);
	}

	if (new.name && !new.commit) {
		die("Cannot switch branch to a non-commit.");
	}
	if (opts.writeout_stage)
		die("--ours/--theirs is incompatible with switching branches.");

	return switch_branches(&opts, &new);
}
