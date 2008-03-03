#include "cache.h"
#include "builtin.h"
#include "parse-options.h"
#include "refs.h"
#include "commit.h"
#include "tree.h"
#include "tree-walk.h"
#include "unpack-trees.h"
#include "dir.h"
#include "run-command.h"
#include "merge-recursive.h"
#include "branch.h"
#include "diff.h"
#include "revision.h"
#include "remote.h"

static const char * const checkout_usage[] = {
	"git checkout [options] <branch>",
	"git checkout [options] [<branch>] -- <file>...",
	NULL,
};

static int post_checkout_hook(struct commit *old, struct commit *new,
			      int changed)
{
	struct child_process proc;
	const char *name = git_path("hooks/post-checkout");
	const char *argv[5];

	if (access(name, X_OK) < 0)
		return 0;

	memset(&proc, 0, sizeof(proc));
	argv[0] = name;
	argv[1] = xstrdup(sha1_to_hex(old->object.sha1));
	argv[2] = xstrdup(sha1_to_hex(new->object.sha1));
	argv[3] = changed ? "1" : "0";
	argv[4] = NULL;
	proc.argv = argv;
	proc.no_stdin = 1;
	proc.stdout_to_stderr = 1;
	return run_command(&proc);
}

static int update_some(const unsigned char *sha1, const char *base, int baselen,
		       const char *pathname, unsigned mode, int stage)
{
	int len;
	struct cache_entry *ce;

	if (S_ISGITLINK(mode))
		return 0;

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
	read_tree_recursive(tree, "", 0, 0, pathspec, update_some);

	/* update the index with the given tree's info
	 * for all args, expanding wildcards, and exit
	 * with any non-zero return code.
	 */
	return 0;
}

static int checkout_paths(struct tree *source_tree, const char **pathspec)
{
	int pos;
	struct checkout state;
	static char *ps_matched;
	unsigned char rev[20];
	int flag;
	struct commit *head;

	int newfd;
	struct lock_file *lock_file = xcalloc(1, sizeof(struct lock_file));

	newfd = hold_locked_index(lock_file, 1);
	read_cache();

	if (source_tree)
		read_tree_some(source_tree, pathspec);

	for (pos = 0; pathspec[pos]; pos++)
		;
	ps_matched = xcalloc(1, pos);

	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];
		pathspec_match(pathspec, ps_matched, ce->name, 0);
	}

	if (report_path_error(ps_matched, pathspec, 0))
		return 1;

	memset(&state, 0, sizeof(state));
	state.force = 1;
	state.refresh_cache = 1;
	for (pos = 0; pos < active_nr; pos++) {
		struct cache_entry *ce = active_cache[pos];
		if (pathspec_match(pathspec, NULL, ce->name, 0)) {
			checkout_entry(ce, &state, NULL);
		}
	}

	if (write_cache(newfd, active_cache, active_nr) ||
	    commit_locked_index(lock_file))
		die("unable to write new index file");

	resolve_ref("HEAD", rev, 0, &flag);
	head = lookup_commit_reference_gently(rev, 1);

	return post_checkout_hook(head, head, 0);
}

static void show_local_changes(struct object *head)
{
	struct rev_info rev;
	/* I think we want full paths, even if we're in a subdirectory. */
	init_revisions(&rev, NULL);
	rev.abbrev = 0;
	rev.diffopt.output_format |= DIFF_FORMAT_NAME_STATUS;
	add_pending_object(&rev, head, NULL);
	run_diff_index(&rev, 0);
}

static void describe_detached_head(char *msg, struct commit *commit)
{
	struct strbuf sb;
	strbuf_init(&sb, 0);
	parse_commit(commit);
	pretty_print_commit(CMIT_FMT_ONELINE, commit, &sb, 0, "", "", 0, 0);
	fprintf(stderr, "%s %s... %s\n", msg,
		find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV), sb.buf);
	strbuf_release(&sb);
}

static int reset_to_new(struct tree *tree, int quiet)
{
	struct unpack_trees_options opts;
	struct tree_desc tree_desc;
	memset(&opts, 0, sizeof(opts));
	opts.head_idx = -1;
	opts.update = 1;
	opts.reset = 1;
	opts.merge = 1;
	opts.fn = oneway_merge;
	opts.verbose_update = !quiet;
	parse_tree(tree);
	init_tree_desc(&tree_desc, tree->buffer, tree->size);
	if (unpack_trees(1, &tree_desc, &opts))
		return 128;
	return 0;
}

static void reset_clean_to_new(struct tree *tree, int quiet)
{
	struct unpack_trees_options opts;
	struct tree_desc tree_desc;
	memset(&opts, 0, sizeof(opts));
	opts.head_idx = -1;
	opts.skip_unmerged = 1;
	opts.reset = 1;
	opts.merge = 1;
	opts.fn = oneway_merge;
	opts.verbose_update = !quiet;
	parse_tree(tree);
	init_tree_desc(&tree_desc, tree->buffer, tree->size);
	if (unpack_trees(1, &tree_desc, &opts))
		exit(128);
}

struct checkout_opts {
	int quiet;
	int merge;
	int force;

	char *new_branch;
	int new_branch_log;
	enum branch_track track;
};

struct branch_info {
	const char *name; /* The short name used */
	const char *path; /* The full name of a real branch */
	struct commit *commit; /* The named commit */
};

static void setup_branch_path(struct branch_info *branch)
{
	struct strbuf buf;
	strbuf_init(&buf, 0);
	strbuf_addstr(&buf, "refs/heads/");
	strbuf_addstr(&buf, branch->name);
	branch->path = strbuf_detach(&buf, NULL);
}

static int merge_working_tree(struct checkout_opts *opts,
			      struct branch_info *old, struct branch_info *new)
{
	int ret;
	struct lock_file *lock_file = xcalloc(1, sizeof(struct lock_file));
	int newfd = hold_locked_index(lock_file, 1);
	read_cache();

	if (opts->force) {
		ret = reset_to_new(new->commit->tree, opts->quiet);
		if (ret)
			return ret;
	} else {
		struct tree_desc trees[2];
		struct tree *tree;
		struct unpack_trees_options topts;
		memset(&topts, 0, sizeof(topts));
		topts.head_idx = -1;

		refresh_cache(REFRESH_QUIET);

		if (unmerged_cache()) {
			error("you need to resolve your current index first");
			return 1;
		}

		/* 2-way merge to the new branch */
		topts.update = 1;
		topts.merge = 1;
		topts.gently = opts->merge;
		topts.verbose_update = !opts->quiet;
		topts.fn = twoway_merge;
		topts.dir = xcalloc(1, sizeof(*topts.dir));
		topts.dir->show_ignored = 1;
		topts.dir->exclude_per_dir = ".gitignore";
		tree = parse_tree_indirect(old->commit->object.sha1);
		init_tree_desc(&trees[0], tree->buffer, tree->size);
		tree = parse_tree_indirect(new->commit->object.sha1);
		init_tree_desc(&trees[1], tree->buffer, tree->size);

		if (unpack_trees(2, trees, &topts)) {
			/*
			 * Unpack couldn't do a trivial merge; either
			 * give up or do a real merge, depending on
			 * whether the merge flag was used.
			 */
			struct tree *result;
			struct tree *work;
			if (!opts->merge)
				return 1;
			parse_commit(old->commit);

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

			add_files_to_cache(0, NULL, NULL);
			work = write_tree_from_memory();

			ret = reset_to_new(new->commit->tree, opts->quiet);
			if (ret)
				return ret;
			merge_trees(new->commit->tree, work, old->commit->tree,
				    new->name, "local", &result);
			reset_clean_to_new(new->commit->tree, opts->quiet);
		}
	}

	if (write_cache(newfd, active_cache, active_nr) ||
	    commit_locked_index(lock_file))
		die("unable to write new index file");

	if (!opts->force)
		show_local_changes(&new->commit->object);

	return 0;
}

static void report_tracking(struct branch_info *new, struct checkout_opts *opts)
{
	/*
	 * We have switched to a new branch; is it building on
	 * top of another branch, and if so does that other branch
	 * have changes we do not have yet?
	 */
	char *base;
	unsigned char sha1[20];
	struct commit *ours, *theirs;
	char symmetric[84];
	struct rev_info revs;
	const char *rev_argv[10];
	int rev_argc;
	int num_ours, num_theirs;
	const char *remote_msg;
	struct branch *branch = branch_get(new->name);

	/*
	 * Nothing to report unless we are marked to build on top of
	 * somebody else.
	 */
	if (!branch || !branch->merge || !branch->merge[0] || !branch->merge[0]->dst)
		return;

	/*
	 * If what we used to build on no longer exists, there is
	 * nothing to report.
	 */
	base = branch->merge[0]->dst;
	if (!resolve_ref(base, sha1, 1, NULL))
		return;

	theirs = lookup_commit(sha1);
	ours = new->commit;
	if (!hashcmp(sha1, ours->object.sha1))
		return; /* we are the same */

	/* Run "rev-list --left-right ours...theirs" internally... */
	rev_argc = 0;
	rev_argv[rev_argc++] = NULL;
	rev_argv[rev_argc++] = "--left-right";
	rev_argv[rev_argc++] = symmetric;
	rev_argv[rev_argc++] = "--";
	rev_argv[rev_argc] = NULL;

	strcpy(symmetric, sha1_to_hex(ours->object.sha1));
	strcpy(symmetric + 40, "...");
	strcpy(symmetric + 43, sha1_to_hex(theirs->object.sha1));

	init_revisions(&revs, NULL);
	setup_revisions(rev_argc, rev_argv, &revs, NULL);
	prepare_revision_walk(&revs);

	/* ... and count the commits on each side. */
	num_ours = 0;
	num_theirs = 0;
	while (1) {
		struct commit *c = get_revision(&revs);
		if (!c)
			break;
		if (c->object.flags & SYMMETRIC_LEFT)
			num_ours++;
		else
			num_theirs++;
	}

	if (!prefixcmp(base, "refs/remotes/")) {
		remote_msg = " remote";
		base += strlen("refs/remotes/");
	} else {
		remote_msg = "";
	}

	if (!num_theirs)
		printf("Your branch is ahead of the tracked%s branch '%s' "
		       "by %d commit%s.\n",
		       remote_msg, base,
		       num_ours, (num_ours == 1) ? "" : "s");
	else if (!num_ours)
		printf("Your branch is behind the tracked%s branch '%s' "
		       "by %d commit%s,\n"
		       "and can be fast-forwarded.\n",
		       remote_msg, base,
		       num_theirs, (num_theirs == 1) ? "" : "s");
	else
		printf("Your branch and the tracked%s branch '%s' "
		       "have diverged,\nand respectively "
		       "have %d and %d different commit(s) each.\n",
		       remote_msg, base,
		       num_ours, num_theirs);
}

static void update_refs_for_switch(struct checkout_opts *opts,
				   struct branch_info *old,
				   struct branch_info *new)
{
	struct strbuf msg;
	const char *old_desc;
	if (opts->new_branch) {
		create_branch(old->name, opts->new_branch, new->name, 0,
			      opts->new_branch_log, opts->track);
		new->name = opts->new_branch;
		setup_branch_path(new);
	}

	strbuf_init(&msg, 0);
	old_desc = old->name;
	if (!old_desc)
		old_desc = sha1_to_hex(old->commit->object.sha1);
	strbuf_addf(&msg, "checkout: moving from %s to %s",
		    old_desc, new->name);

	if (new->path) {
		create_symref("HEAD", new->path, msg.buf);
		if (!opts->quiet) {
			if (old->path && !strcmp(new->path, old->path))
				fprintf(stderr, "Already on \"%s\"\n",
					new->name);
			else
				fprintf(stderr, "Switched to%s branch \"%s\"\n",
					opts->new_branch ? " a new" : "",
					new->name);
		}
	} else if (strcmp(new->name, "HEAD")) {
		update_ref(msg.buf, "HEAD", new->commit->object.sha1, NULL,
			   REF_NODEREF, DIE_ON_ERR);
		if (!opts->quiet) {
			if (old->path)
				fprintf(stderr, "Note: moving to \"%s\" which isn't a local branch\nIf you want to create a new branch from this checkout, you may do so\n(now or later) by using -b with the checkout command again. Example:\n  git checkout -b <new_branch_name>\n", new->name);
			describe_detached_head("HEAD is now at", new->commit);
		}
	}
	remove_branch_state();
	strbuf_release(&msg);
	if (!opts->quiet && (new->path || !strcmp(new->name, "HEAD")))
		report_tracking(new, opts);
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

	/*
	 * If the new thing isn't a branch and isn't HEAD and we're
	 * not starting a new branch, and we want messages, and we
	 * weren't on a branch, and we're moving to a new commit,
	 * describe the old commit.
	 */
	if (!new->path && strcmp(new->name, "HEAD") && !opts->new_branch &&
	    !opts->quiet && !old.path && new->commit != old.commit)
		describe_detached_head("Previous HEAD position was", old.commit);

	if (!old.commit) {
		if (!opts->quiet) {
			fprintf(stderr, "warning: You appear to be on a branch yet to be born.\n");
			fprintf(stderr, "warning: Forcing checkout of %s.\n", new->name);
		}
		opts->force = 1;
	}

	ret = merge_working_tree(opts, &old, new);
	if (ret)
		return ret;

	update_refs_for_switch(opts, &old, new);

	return post_checkout_hook(old.commit, new->commit, 1);
}

int cmd_checkout(int argc, const char **argv, const char *prefix)
{
	struct checkout_opts opts;
	unsigned char rev[20];
	const char *arg;
	struct branch_info new;
	struct tree *source_tree = NULL;
	struct option options[] = {
		OPT__QUIET(&opts.quiet),
		OPT_STRING('b', NULL, &opts.new_branch, "new branch", "branch"),
		OPT_BOOLEAN('l', NULL, &opts.new_branch_log, "log for new branch"),
		OPT_SET_INT( 0 , "track",  &opts.track, "track",
			BRANCH_TRACK_EXPLICIT),
		OPT_BOOLEAN('f', NULL, &opts.force, "force"),
		OPT_BOOLEAN('m', NULL, &opts.merge, "merge"),
		OPT_END(),
	};

	memset(&opts, 0, sizeof(opts));
	memset(&new, 0, sizeof(new));

	git_config(git_default_config);

	opts.track = git_branch_track;

	argc = parse_options(argc, argv, options, checkout_usage, 0);
	if (argc) {
		arg = argv[0];
		if (get_sha1(arg, rev))
			;
		else if ((new.commit = lookup_commit_reference_gently(rev, 1))) {
			new.name = arg;
			setup_branch_path(&new);
			if (resolve_ref(new.path, rev, 1, NULL))
				new.commit = lookup_commit_reference(rev);
			else
				new.path = NULL;
			parse_commit(new.commit);
			source_tree = new.commit->tree;
			argv++;
			argc--;
		} else if ((source_tree = parse_tree_indirect(rev))) {
			argv++;
			argc--;
		}
	}

	if (argc && !strcmp(argv[0], "--")) {
		argv++;
		argc--;
	}

	if (!opts.new_branch && (opts.track != git_branch_track))
		die("git checkout: --track and --no-track require -b");

	if (opts.force && opts.merge)
		die("git checkout: -f and -m are incompatible");

	if (argc) {
		const char **pathspec = get_pathspec(prefix, argv);

		if (!pathspec)
			die("invalid path specification");

		/* Checkout paths */
		if (opts.new_branch || opts.force || opts.merge) {
			if (argc == 1) {
				die("git checkout: updating paths is incompatible with switching branches/forcing\nDid you intend to checkout '%s' which can not be resolved as commit?", argv[0]);
			} else {
				die("git checkout: updating paths is incompatible with switching branches/forcing");
			}
		}

		return checkout_paths(source_tree, pathspec);
	}

	if (new.name && !new.commit) {
		die("Cannot switch branch to a non-commit.");
	}

	return switch_branches(&opts, &new);
}
