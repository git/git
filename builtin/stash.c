#define USE_THE_INDEX_VARIABLE
#include "builtin.h"
#include "config.h"
#include "hex.h"
#include "parse-options.h"
#include "refs.h"
#include "lockfile.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "merge-recursive.h"
#include "merge-ort-wrappers.h"
#include "strvec.h"
#include "run-command.h"
#include "dir.h"
#include "entry.h"
#include "rerere.h"
#include "revision.h"
#include "log-tree.h"
#include "diffcore.h"
#include "exec-cmd.h"
#include "reflog.h"
#include "add-interactive.h"

#define INCLUDE_ALL_FILES 2

#define BUILTIN_STASH_LIST_USAGE \
	N_("git stash list [<log-options>]")
#define BUILTIN_STASH_SHOW_USAGE \
	N_("git stash show [-u | --include-untracked | --only-untracked] [<diff-options>] [<stash>]")
#define BUILTIN_STASH_DROP_USAGE \
	N_("git stash drop [-q | --quiet] [<stash>]")
#define BUILTIN_STASH_POP_USAGE \
	N_("git stash pop [--index] [-q | --quiet] [<stash>]")
#define BUILTIN_STASH_APPLY_USAGE \
	N_("git stash apply [--index] [-q | --quiet] [<stash>]")
#define BUILTIN_STASH_BRANCH_USAGE \
	N_("git stash branch <branchname> [<stash>]")
#define BUILTIN_STASH_STORE_USAGE \
	N_("git stash store [(-m | --message) <message>] [-q | --quiet] <commit>")
#define BUILTIN_STASH_PUSH_USAGE \
	N_("git stash [push [-p | --patch] [-S | --staged] [-k | --[no-]keep-index] [-q | --quiet]\n" \
	   "          [-u | --include-untracked] [-a | --all] [(-m | --message) <message>]\n" \
	   "          [--pathspec-from-file=<file> [--pathspec-file-nul]]\n" \
	   "          [--] [<pathspec>...]]")
#define BUILTIN_STASH_SAVE_USAGE \
	N_("git stash save [-p | --patch] [-S | --staged] [-k | --[no-]keep-index] [-q | --quiet]\n" \
	   "          [-u | --include-untracked] [-a | --all] [<message>]")
#define BUILTIN_STASH_CREATE_USAGE \
	N_("git stash create [<message>]")
#define BUILTIN_STASH_CLEAR_USAGE \
	"git stash clear"

static const char * const git_stash_usage[] = {
	BUILTIN_STASH_LIST_USAGE,
	BUILTIN_STASH_SHOW_USAGE,
	BUILTIN_STASH_DROP_USAGE,
	BUILTIN_STASH_POP_USAGE,
	BUILTIN_STASH_APPLY_USAGE,
	BUILTIN_STASH_BRANCH_USAGE,
	BUILTIN_STASH_PUSH_USAGE,
	BUILTIN_STASH_SAVE_USAGE,
	BUILTIN_STASH_CLEAR_USAGE,
	BUILTIN_STASH_CREATE_USAGE,
	BUILTIN_STASH_STORE_USAGE,
	NULL
};

static const char * const git_stash_list_usage[] = {
	BUILTIN_STASH_LIST_USAGE,
	NULL
};

static const char * const git_stash_show_usage[] = {
	BUILTIN_STASH_SHOW_USAGE,
	NULL
};

static const char * const git_stash_drop_usage[] = {
	BUILTIN_STASH_DROP_USAGE,
	NULL
};

static const char * const git_stash_pop_usage[] = {
	BUILTIN_STASH_POP_USAGE,
	NULL
};

static const char * const git_stash_apply_usage[] = {
	BUILTIN_STASH_APPLY_USAGE,
	NULL
};

static const char * const git_stash_branch_usage[] = {
	BUILTIN_STASH_BRANCH_USAGE,
	NULL
};

static const char * const git_stash_clear_usage[] = {
	BUILTIN_STASH_CLEAR_USAGE,
	NULL
};

static const char * const git_stash_store_usage[] = {
	BUILTIN_STASH_STORE_USAGE,
	NULL
};

static const char * const git_stash_push_usage[] = {
	BUILTIN_STASH_PUSH_USAGE,
	NULL
};

static const char * const git_stash_save_usage[] = {
	BUILTIN_STASH_SAVE_USAGE,
	NULL
};

static const char ref_stash[] = "refs/stash";
static struct strbuf stash_index_path = STRBUF_INIT;

/*
 * w_commit is set to the commit containing the working tree
 * b_commit is set to the base commit
 * i_commit is set to the commit containing the index tree
 * u_commit is set to the commit containing the untracked files tree
 * w_tree is set to the working tree
 * b_tree is set to the base tree
 * i_tree is set to the index tree
 * u_tree is set to the untracked files tree
 */
struct stash_info {
	struct object_id w_commit;
	struct object_id b_commit;
	struct object_id i_commit;
	struct object_id u_commit;
	struct object_id w_tree;
	struct object_id b_tree;
	struct object_id i_tree;
	struct object_id u_tree;
	struct strbuf revision;
	int is_stash_ref;
	int has_u;
};

#define STASH_INFO_INIT { \
	.revision = STRBUF_INIT, \
}

static void free_stash_info(struct stash_info *info)
{
	strbuf_release(&info->revision);
}

static void assert_stash_like(struct stash_info *info, const char *revision)
{
	if (get_oidf(&info->b_commit, "%s^1", revision) ||
	    get_oidf(&info->w_tree, "%s:", revision) ||
	    get_oidf(&info->b_tree, "%s^1:", revision) ||
	    get_oidf(&info->i_tree, "%s^2:", revision))
		die(_("'%s' is not a stash-like commit"), revision);
}

static int get_stash_info(struct stash_info *info, int argc, const char **argv)
{
	int ret;
	char *end_of_rev;
	char *expanded_ref;
	const char *revision;
	const char *commit = NULL;
	struct object_id dummy;
	struct strbuf symbolic = STRBUF_INIT;

	if (argc > 1) {
		int i;
		struct strbuf refs_msg = STRBUF_INIT;

		for (i = 0; i < argc; i++)
			strbuf_addf(&refs_msg, " '%s'", argv[i]);

		fprintf_ln(stderr, _("Too many revisions specified:%s"),
			   refs_msg.buf);
		strbuf_release(&refs_msg);

		return -1;
	}

	if (argc == 1)
		commit = argv[0];

	if (!commit) {
		if (!ref_exists(ref_stash)) {
			fprintf_ln(stderr, _("No stash entries found."));
			return -1;
		}

		strbuf_addf(&info->revision, "%s@{0}", ref_stash);
	} else if (strspn(commit, "0123456789") == strlen(commit)) {
		strbuf_addf(&info->revision, "%s@{%s}", ref_stash, commit);
	} else {
		strbuf_addstr(&info->revision, commit);
	}

	revision = info->revision.buf;

	if (get_oid(revision, &info->w_commit))
		return error(_("%s is not a valid reference"), revision);

	assert_stash_like(info, revision);

	info->has_u = !get_oidf(&info->u_tree, "%s^3:", revision);

	end_of_rev = strchrnul(revision, '@');
	strbuf_add(&symbolic, revision, end_of_rev - revision);

	ret = dwim_ref(symbolic.buf, symbolic.len, &dummy, &expanded_ref, 0);
	strbuf_release(&symbolic);
	switch (ret) {
	case 0: /* Not found, but valid ref */
		info->is_stash_ref = 0;
		break;
	case 1:
		info->is_stash_ref = !strcmp(expanded_ref, ref_stash);
		break;
	default: /* Invalid or ambiguous */
		break;
	}

	free(expanded_ref);
	return !(ret == 0 || ret == 1);
}

static int do_clear_stash(void)
{
	struct object_id obj;
	if (get_oid(ref_stash, &obj))
		return 0;

	return delete_ref(NULL, ref_stash, &obj, 0);
}

static int clear_stash(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_clear_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc)
		return error(_("git stash clear with arguments is "
			       "unimplemented"));

	return do_clear_stash();
}

static int reset_tree(struct object_id *i_tree, int update, int reset)
{
	int nr_trees = 1;
	struct unpack_trees_options opts;
	struct tree_desc t[MAX_UNPACK_TREES];
	struct tree *tree;
	struct lock_file lock_file = LOCK_INIT;

	repo_read_index_preload(the_repository, NULL, 0);
	if (refresh_index(&the_index, REFRESH_QUIET, NULL, NULL, NULL))
		return -1;

	repo_hold_locked_index(the_repository, &lock_file, LOCK_DIE_ON_ERROR);

	memset(&opts, 0, sizeof(opts));

	tree = parse_tree_indirect(i_tree);
	if (parse_tree(tree))
		return -1;

	init_tree_desc(t, tree->buffer, tree->size);

	opts.head_idx = 1;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
	opts.merge = 1;
	opts.reset = reset ? UNPACK_RESET_PROTECT_UNTRACKED : 0;
	opts.update = update;
	if (update)
		opts.preserve_ignored = 0; /* FIXME: !overwrite_ignore */
	opts.fn = oneway_merge;

	if (unpack_trees(nr_trees, t, &opts))
		return -1;

	if (write_locked_index(&the_index, &lock_file, COMMIT_LOCK))
		return error(_("unable to write new index file"));

	return 0;
}

static int diff_tree_binary(struct strbuf *out, struct object_id *w_commit)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	const char *w_commit_hex = oid_to_hex(w_commit);

	/*
	 * Diff-tree would not be very hard to replace with a native function,
	 * however it should be done together with apply_cached.
	 */
	cp.git_cmd = 1;
	strvec_pushl(&cp.args, "diff-tree", "--binary", NULL);
	strvec_pushf(&cp.args, "%s^2^..%s^2", w_commit_hex, w_commit_hex);

	return pipe_command(&cp, NULL, 0, out, 0, NULL, 0);
}

static int apply_cached(struct strbuf *out)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	/*
	 * Apply currently only reads either from stdin or a file, thus
	 * apply_all_patches would have to be updated to optionally take a
	 * buffer.
	 */
	cp.git_cmd = 1;
	strvec_pushl(&cp.args, "apply", "--cached", NULL);
	return pipe_command(&cp, out->buf, out->len, NULL, 0, NULL, 0);
}

static int reset_head(void)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	/*
	 * Reset is overall quite simple, however there is no current public
	 * API for resetting.
	 */
	cp.git_cmd = 1;
	strvec_pushl(&cp.args, "reset", "--quiet", "--refresh", NULL);

	return run_command(&cp);
}

static int is_path_a_directory(const char *path)
{
	/*
	 * This function differs from abspath.c:is_directory() in that
	 * here we use lstat() instead of stat(); we do not want to
	 * follow symbolic links here.
	 */
	struct stat st;
	return (!lstat(path, &st) && S_ISDIR(st.st_mode));
}

static void add_diff_to_buf(struct diff_queue_struct *q,
			    struct diff_options *options,
			    void *data)
{
	int i;

	for (i = 0; i < q->nr; i++) {
		if (is_path_a_directory(q->queue[i]->one->path))
			continue;

		strbuf_addstr(data, q->queue[i]->one->path);

		/* NUL-terminate: will be fed to update-index -z */
		strbuf_addch(data, '\0');
	}
}

static int restore_untracked(struct object_id *u_tree)
{
	int res;
	struct child_process cp = CHILD_PROCESS_INIT;

	/*
	 * We need to run restore files from a given index, but without
	 * affecting the current index, so we use GIT_INDEX_FILE with
	 * run_command to fork processes that will not interfere.
	 */
	cp.git_cmd = 1;
	strvec_push(&cp.args, "read-tree");
	strvec_push(&cp.args, oid_to_hex(u_tree));
	strvec_pushf(&cp.env, "GIT_INDEX_FILE=%s",
		     stash_index_path.buf);
	if (run_command(&cp)) {
		remove_path(stash_index_path.buf);
		return -1;
	}

	child_process_init(&cp);
	cp.git_cmd = 1;
	strvec_pushl(&cp.args, "checkout-index", "--all", NULL);
	strvec_pushf(&cp.env, "GIT_INDEX_FILE=%s",
		     stash_index_path.buf);

	res = run_command(&cp);
	remove_path(stash_index_path.buf);
	return res;
}

static void unstage_changes_unless_new(struct object_id *orig_tree)
{
	/*
	 * When we enter this function, there has been a clean merge of
	 * relevant trees, and the merge logic always stages whatever merges
	 * cleanly.  We want to unstage those changes, unless it corresponds
	 * to a file that didn't exist as of orig_tree.
	 *
	 * However, if any SKIP_WORKTREE path is modified relative to
	 * orig_tree, then we want to clear the SKIP_WORKTREE bit and write
	 * it to the worktree before unstaging.
	 */

	struct checkout state = CHECKOUT_INIT;
	struct diff_options diff_opts;
	struct lock_file lock = LOCK_INIT;
	int i;

	/* If any entries have skip_worktree set, we'll have to check 'em out */
	state.force = 1;
	state.quiet = 1;
	state.refresh_cache = 1;
	state.istate = &the_index;

	/*
	 * Step 1: get a difference between orig_tree (which corresponding
	 * to the index before a merge was run) and the current index
	 * (reflecting the changes brought in by the merge).
	 */
	diff_setup(&diff_opts);
	diff_opts.flags.recursive = 1;
	diff_opts.detect_rename = 0;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_setup_done(&diff_opts);

	do_diff_cache(orig_tree, &diff_opts);
	diffcore_std(&diff_opts);

	/* Iterate over the paths that changed due to the merge... */
	for (i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *p;
		struct cache_entry *ce;
		int pos;

		/* Look up the path's position in the current index. */
		p = diff_queued_diff.queue[i];
		pos = index_name_pos(&the_index, p->two->path,
				     strlen(p->two->path));

		/*
		 * Step 2: Place changes in the working tree
		 *
		 * Stash is about restoring changes *to the working tree*.
		 * So if the merge successfully got a new version of some
		 * path, but left it out of the working tree, then clear the
		 * SKIP_WORKTREE bit and write it to the working tree.
		 */
		if (pos >= 0 && ce_skip_worktree(the_index.cache[pos])) {
			struct stat st;

			ce = the_index.cache[pos];
			if (!lstat(ce->name, &st)) {
				/* Conflicting path present; relocate it */
				struct strbuf new_path = STRBUF_INIT;
				int fd;

				strbuf_addf(&new_path,
					    "%s.stash.XXXXXX", ce->name);
				fd = xmkstemp(new_path.buf);
				close(fd);
				printf(_("WARNING: Untracked file in way of "
					 "tracked file!  Renaming\n "
					 "           %s -> %s\n"
					 "         to make room.\n"),
				       ce->name, new_path.buf);
				if (rename(ce->name, new_path.buf))
					die("Failed to move %s to %s\n",
					    ce->name, new_path.buf);
				strbuf_release(&new_path);
			}
			checkout_entry(ce, &state, NULL, NULL);
			ce->ce_flags &= ~CE_SKIP_WORKTREE;
		}

		/*
		 * Step 3: "unstage" changes, as long as they are still tracked
		 */
		if (p->one->oid_valid) {
			/*
			 * Path existed in orig_tree; restore index entry
			 * from that tree in order to "unstage" the changes.
			 */
			int option = ADD_CACHE_OK_TO_REPLACE;
			if (pos < 0)
				option = ADD_CACHE_OK_TO_ADD;

			ce = make_cache_entry(&the_index,
					      p->one->mode,
					      &p->one->oid,
					      p->one->path,
					      0, 0);
			add_index_entry(&the_index, ce, option);
		}
	}
	diff_flush(&diff_opts);

	/*
	 * Step 4: write the new index to disk
	 */
	repo_hold_locked_index(the_repository, &lock, LOCK_DIE_ON_ERROR);
	if (write_locked_index(&the_index, &lock,
			       COMMIT_LOCK | SKIP_IF_UNCHANGED))
		die(_("Unable to write index."));
}

static int do_apply_stash(const char *prefix, struct stash_info *info,
			  int index, int quiet)
{
	int clean, ret;
	int has_index = index;
	struct merge_options o;
	struct object_id c_tree;
	struct object_id index_tree;
	struct tree *head, *merge, *merge_base;
	struct lock_file lock = LOCK_INIT;

	repo_read_index_preload(the_repository, NULL, 0);
	if (repo_refresh_and_write_index(the_repository, REFRESH_QUIET, 0, 0,
					 NULL, NULL, NULL))
		return -1;

	if (write_index_as_tree(&c_tree, &the_index, get_index_file(), 0,
				NULL))
		return error(_("cannot apply a stash in the middle of a merge"));

	if (index) {
		if (oideq(&info->b_tree, &info->i_tree) ||
		    oideq(&c_tree, &info->i_tree)) {
			has_index = 0;
		} else {
			struct strbuf out = STRBUF_INIT;

			if (diff_tree_binary(&out, &info->w_commit)) {
				strbuf_release(&out);
				return error(_("could not generate diff %s^!."),
					     oid_to_hex(&info->w_commit));
			}

			ret = apply_cached(&out);
			strbuf_release(&out);
			if (ret)
				return error(_("conflicts in index. "
					       "Try without --index."));

			discard_index(&the_index);
			repo_read_index(the_repository);
			if (write_index_as_tree(&index_tree, &the_index,
						get_index_file(), 0, NULL))
				return error(_("could not save index tree"));

			reset_head();
			discard_index(&the_index);
			repo_read_index(the_repository);
		}
	}

	init_merge_options(&o, the_repository);

	o.branch1 = "Updated upstream";
	o.branch2 = "Stashed changes";
	o.ancestor = "Stash base";

	if (oideq(&info->b_tree, &c_tree))
		o.branch1 = "Version stash was based on";

	if (quiet)
		o.verbosity = 0;

	if (o.verbosity >= 3)
		printf_ln(_("Merging %s with %s"), o.branch1, o.branch2);

	head = lookup_tree(o.repo, &c_tree);
	merge = lookup_tree(o.repo, &info->w_tree);
	merge_base = lookup_tree(o.repo, &info->b_tree);

	repo_hold_locked_index(o.repo, &lock, LOCK_DIE_ON_ERROR);
	clean = merge_ort_nonrecursive(&o, head, merge, merge_base);

	/*
	 * If 'clean' >= 0, reverse the value for 'ret' so 'ret' is 0 when the
	 * merge was clean, and nonzero if the merge was unclean or encountered
	 * an error.
	 */
	ret = clean >= 0 ? !clean : clean;

	if (ret < 0)
		rollback_lock_file(&lock);
	else if (write_locked_index(o.repo->index, &lock,
				      COMMIT_LOCK | SKIP_IF_UNCHANGED))
		ret = error(_("could not write index"));

	if (ret) {
		rerere(0);

		if (index)
			fprintf_ln(stderr, _("Index was not unstashed."));

		goto restore_untracked;
	}

	if (has_index) {
		if (reset_tree(&index_tree, 0, 0))
			ret = -1;
	} else {
		unstage_changes_unless_new(&c_tree);
	}

restore_untracked:
	if (info->has_u && restore_untracked(&info->u_tree))
		ret = error(_("could not restore untracked files from stash"));

	if (!quiet) {
		struct child_process cp = CHILD_PROCESS_INIT;

		/*
		 * Status is quite simple and could be replaced with calls to
		 * wt_status in the future, but it adds complexities which may
		 * require more tests.
		 */
		cp.git_cmd = 1;
		cp.dir = prefix;
		strvec_pushf(&cp.env, GIT_WORK_TREE_ENVIRONMENT"=%s",
			     absolute_path(get_git_work_tree()));
		strvec_pushf(&cp.env, GIT_DIR_ENVIRONMENT"=%s",
			     absolute_path(get_git_dir()));
		strvec_push(&cp.args, "status");
		run_command(&cp);
	}

	return ret;
}

static int apply_stash(int argc, const char **argv, const char *prefix)
{
	int ret = -1;
	int quiet = 0;
	int index = 0;
	struct stash_info info = STASH_INFO_INIT;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet, only report errors")),
		OPT_BOOL(0, "index", &index,
			 N_("attempt to recreate the index")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_apply_usage, 0);

	if (get_stash_info(&info, argc, argv))
		goto cleanup;

	ret = do_apply_stash(prefix, &info, index, quiet);
cleanup:
	free_stash_info(&info);
	return ret;
}

static int reject_reflog_ent(struct object_id *ooid UNUSED,
			     struct object_id *noid UNUSED,
			     const char *email UNUSED,
			     timestamp_t timestamp UNUSED,
			     int tz UNUSED, const char *message UNUSED,
			     void *cb_data UNUSED)
{
	return 1;
}

static int reflog_is_empty(const char *refname)
{
	return !for_each_reflog_ent(refname, reject_reflog_ent, NULL);
}

static int do_drop_stash(struct stash_info *info, int quiet)
{
	if (!reflog_delete(info->revision.buf,
			   EXPIRE_REFLOGS_REWRITE | EXPIRE_REFLOGS_UPDATE_REF,
			   0)) {
		if (!quiet)
			printf_ln(_("Dropped %s (%s)"), info->revision.buf,
				  oid_to_hex(&info->w_commit));
	} else {
		return error(_("%s: Could not drop stash entry"),
			     info->revision.buf);
	}

	if (reflog_is_empty(ref_stash))
		do_clear_stash();

	return 0;
}

static int get_stash_info_assert(struct stash_info *info, int argc,
				 const char **argv)
{
	int ret = get_stash_info(info, argc, argv);

	if (ret < 0)
		return ret;

	if (!info->is_stash_ref)
		return error(_("'%s' is not a stash reference"), info->revision.buf);

	return 0;
}

static int drop_stash(int argc, const char **argv, const char *prefix)
{
	int ret = -1;
	int quiet = 0;
	struct stash_info info = STASH_INFO_INIT;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet, only report errors")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_drop_usage, 0);

	if (get_stash_info_assert(&info, argc, argv))
		goto cleanup;

	ret = do_drop_stash(&info, quiet);
cleanup:
	free_stash_info(&info);
	return ret;
}

static int pop_stash(int argc, const char **argv, const char *prefix)
{
	int ret = -1;
	int index = 0;
	int quiet = 0;
	struct stash_info info = STASH_INFO_INIT;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet, only report errors")),
		OPT_BOOL(0, "index", &index,
			 N_("attempt to recreate the index")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_pop_usage, 0);

	if (get_stash_info_assert(&info, argc, argv))
		goto cleanup;

	if ((ret = do_apply_stash(prefix, &info, index, quiet)))
		printf_ln(_("The stash entry is kept in case "
			    "you need it again."));
	else
		ret = do_drop_stash(&info, quiet);

cleanup:
	free_stash_info(&info);
	return ret;
}

static int branch_stash(int argc, const char **argv, const char *prefix)
{
	int ret = -1;
	const char *branch = NULL;
	struct stash_info info = STASH_INFO_INIT;
	struct child_process cp = CHILD_PROCESS_INIT;
	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_branch_usage, 0);

	if (!argc) {
		fprintf_ln(stderr, _("No branch name specified"));
		return -1;
	}

	branch = argv[0];

	if (get_stash_info(&info, argc - 1, argv + 1))
		goto cleanup;

	cp.git_cmd = 1;
	strvec_pushl(&cp.args, "checkout", "-b", NULL);
	strvec_push(&cp.args, branch);
	strvec_push(&cp.args, oid_to_hex(&info.b_commit));
	ret = run_command(&cp);
	if (!ret)
		ret = do_apply_stash(prefix, &info, 1, 0);
	if (!ret && info.is_stash_ref)
		ret = do_drop_stash(&info, 0);

cleanup:
	free_stash_info(&info);
	return ret;
}

static int list_stash(int argc, const char **argv, const char *prefix)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_list_usage,
			     PARSE_OPT_KEEP_UNKNOWN_OPT);

	if (!ref_exists(ref_stash))
		return 0;

	cp.git_cmd = 1;
	strvec_pushl(&cp.args, "log", "--format=%gd: %gs", "-g",
		     "--first-parent", NULL);
	strvec_pushv(&cp.args, argv);
	strvec_push(&cp.args, ref_stash);
	strvec_push(&cp.args, "--");
	return run_command(&cp);
}

static int show_stat = 1;
static int show_patch;
static int show_include_untracked;

static int git_stash_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "stash.showstat")) {
		show_stat = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "stash.showpatch")) {
		show_patch = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "stash.showincludeuntracked")) {
		show_include_untracked = git_config_bool(var, value);
		return 0;
	}
	return git_diff_basic_config(var, value, cb);
}

static void diff_include_untracked(const struct stash_info *info, struct diff_options *diff_opt)
{
	const struct object_id *oid[] = { &info->w_commit, &info->u_tree };
	struct tree *tree[ARRAY_SIZE(oid)];
	struct tree_desc tree_desc[ARRAY_SIZE(oid)];
	struct unpack_trees_options unpack_tree_opt = { 0 };
	int i;

	for (i = 0; i < ARRAY_SIZE(oid); i++) {
		tree[i] = parse_tree_indirect(oid[i]);
		if (parse_tree(tree[i]) < 0)
			die(_("failed to parse tree"));
		init_tree_desc(&tree_desc[i], tree[i]->buffer, tree[i]->size);
	}

	unpack_tree_opt.head_idx = -1;
	unpack_tree_opt.src_index = &the_index;
	unpack_tree_opt.dst_index = &the_index;
	unpack_tree_opt.merge = 1;
	unpack_tree_opt.fn = stash_worktree_untracked_merge;

	if (unpack_trees(ARRAY_SIZE(tree_desc), tree_desc, &unpack_tree_opt))
		die(_("failed to unpack trees"));

	do_diff_cache(&info->b_commit, diff_opt);
}

static int show_stash(int argc, const char **argv, const char *prefix)
{
	int i;
	int ret = -1;
	struct stash_info info = STASH_INFO_INIT;
	struct rev_info rev;
	struct strvec stash_args = STRVEC_INIT;
	struct strvec revision_args = STRVEC_INIT;
	enum {
		UNTRACKED_NONE,
		UNTRACKED_INCLUDE,
		UNTRACKED_ONLY
	} show_untracked = show_include_untracked ? UNTRACKED_INCLUDE : UNTRACKED_NONE;
	struct option options[] = {
		OPT_SET_INT('u', "include-untracked", &show_untracked,
			    N_("include untracked files in the stash"),
			    UNTRACKED_INCLUDE),
		OPT_SET_INT_F(0, "only-untracked", &show_untracked,
			      N_("only show untracked files in the stash"),
			      UNTRACKED_ONLY, PARSE_OPT_NONEG),
		OPT_END()
	};
	int do_usage = 0;

	init_diff_ui_defaults();
	git_config(git_diff_ui_config, NULL);
	init_revisions(&rev, prefix);

	argc = parse_options(argc, argv, prefix, options, git_stash_show_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN_OPT |
			     PARSE_OPT_KEEP_DASHDASH);

	strvec_push(&revision_args, argv[0]);
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			strvec_push(&stash_args, argv[i]);
		else
			strvec_push(&revision_args, argv[i]);
	}

	if (get_stash_info(&info, stash_args.nr, stash_args.v))
		goto cleanup;

	/*
	 * The config settings are applied only if there are not passed
	 * any options.
	 */
	if (revision_args.nr == 1) {
		if (show_stat)
			rev.diffopt.output_format = DIFF_FORMAT_DIFFSTAT;

		if (show_patch)
			rev.diffopt.output_format |= DIFF_FORMAT_PATCH;

		if (!show_stat && !show_patch) {
			ret = 0;
			goto cleanup;
		}
	}

	argc = setup_revisions(revision_args.nr, revision_args.v, &rev, NULL);
	if (argc > 1)
		goto usage;
	if (!rev.diffopt.output_format) {
		rev.diffopt.output_format = DIFF_FORMAT_PATCH;
		diff_setup_done(&rev.diffopt);
	}

	rev.diffopt.flags.recursive = 1;
	setup_diff_pager(&rev.diffopt);
	switch (show_untracked) {
	case UNTRACKED_NONE:
		diff_tree_oid(&info.b_commit, &info.w_commit, "", &rev.diffopt);
		break;
	case UNTRACKED_ONLY:
		if (info.has_u)
			diff_root_tree_oid(&info.u_tree, "", &rev.diffopt);
		break;
	case UNTRACKED_INCLUDE:
		if (info.has_u)
			diff_include_untracked(&info, &rev.diffopt);
		else
			diff_tree_oid(&info.b_commit, &info.w_commit, "", &rev.diffopt);
		break;
	}
	log_tree_diff_flush(&rev);

	ret = diff_result_code(&rev.diffopt, 0);
cleanup:
	strvec_clear(&stash_args);
	free_stash_info(&info);
	release_revisions(&rev);
	if (do_usage)
		usage_with_options(git_stash_show_usage, options);
	return ret;
usage:
	do_usage = 1;
	goto cleanup;
}

static int do_store_stash(const struct object_id *w_commit, const char *stash_msg,
			  int quiet)
{
	if (!stash_msg)
		stash_msg = "Created via \"git stash store\".";

	if (update_ref(stash_msg, ref_stash, w_commit, NULL,
		       REF_FORCE_CREATE_REFLOG,
		       quiet ? UPDATE_REFS_QUIET_ON_ERR :
		       UPDATE_REFS_MSG_ON_ERR)) {
		if (!quiet) {
			fprintf_ln(stderr, _("Cannot update %s with %s"),
				   ref_stash, oid_to_hex(w_commit));
		}
		return -1;
	}

	return 0;
}

static int store_stash(int argc, const char **argv, const char *prefix)
{
	int quiet = 0;
	const char *stash_msg = NULL;
	struct object_id obj;
	struct object_context dummy;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet")),
		OPT_STRING('m', "message", &stash_msg, "message",
			   N_("stash message")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_store_usage,
			     PARSE_OPT_KEEP_UNKNOWN_OPT);

	if (argc != 1) {
		if (!quiet)
			fprintf_ln(stderr, _("\"git stash store\" requires one "
					     "<commit> argument"));
		return -1;
	}

	if (get_oid_with_context(the_repository,
				 argv[0], quiet ? GET_OID_QUIETLY : 0, &obj,
				 &dummy)) {
		if (!quiet)
			fprintf_ln(stderr, _("Cannot update %s with %s"),
					     ref_stash, argv[0]);
		return -1;
	}

	return do_store_stash(&obj, stash_msg, quiet);
}

static void add_pathspecs(struct strvec *args,
			  const struct pathspec *ps) {
	int i;

	for (i = 0; i < ps->nr; i++)
		strvec_push(args, ps->items[i].original);
}

/*
 * `untracked_files` will be filled with the names of untracked files.
 * The return value is:
 *
 * = 0 if there are not any untracked files
 * > 0 if there are untracked files
 */
static int get_untracked_files(const struct pathspec *ps, int include_untracked,
			       struct strbuf *untracked_files)
{
	int i;
	int found = 0;
	struct dir_struct dir = DIR_INIT;

	if (include_untracked != INCLUDE_ALL_FILES)
		setup_standard_excludes(&dir);

	fill_directory(&dir, the_repository->index, ps);
	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];
		found++;
		strbuf_addstr(untracked_files, ent->name);
		/* NUL-terminate: will be fed to update-index -z */
		strbuf_addch(untracked_files, '\0');
	}

	dir_clear(&dir);
	return found;
}

/*
 * The return value of `check_changes_tracked_files()` can be:
 *
 * < 0 if there was an error
 * = 0 if there are no changes.
 * > 0 if there are changes.
 */
static int check_changes_tracked_files(const struct pathspec *ps)
{
	int result;
	struct rev_info rev;
	struct object_id dummy;
	int ret = 0;

	/* No initial commit. */
	if (get_oid("HEAD", &dummy))
		return -1;

	if (repo_read_index(the_repository) < 0)
		return -1;

	init_revisions(&rev, NULL);
	copy_pathspec(&rev.prune_data, ps);

	rev.diffopt.flags.quick = 1;
	rev.diffopt.flags.ignore_submodules = 1;
	rev.abbrev = 0;

	add_head_to_pending(&rev);
	diff_setup_done(&rev.diffopt);

	result = run_diff_index(&rev, 1);
	if (diff_result_code(&rev.diffopt, result)) {
		ret = 1;
		goto done;
	}

	result = run_diff_files(&rev, 0);
	if (diff_result_code(&rev.diffopt, result)) {
		ret = 1;
		goto done;
	}

done:
	release_revisions(&rev);
	return ret;
}

/*
 * The function will fill `untracked_files` with the names of untracked files
 * It will return 1 if there were any changes and 0 if there were not.
 */
static int check_changes(const struct pathspec *ps, int include_untracked,
			 struct strbuf *untracked_files)
{
	int ret = 0;
	if (check_changes_tracked_files(ps))
		ret = 1;

	if (include_untracked && get_untracked_files(ps, include_untracked,
						     untracked_files))
		ret = 1;

	return ret;
}

static int save_untracked_files(struct stash_info *info, struct strbuf *msg,
				struct strbuf files)
{
	int ret = 0;
	struct strbuf untracked_msg = STRBUF_INIT;
	struct child_process cp_upd_index = CHILD_PROCESS_INIT;
	struct index_state istate = INDEX_STATE_INIT(the_repository);

	cp_upd_index.git_cmd = 1;
	strvec_pushl(&cp_upd_index.args, "update-index", "-z", "--add",
		     "--remove", "--stdin", NULL);
	strvec_pushf(&cp_upd_index.env, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);

	strbuf_addf(&untracked_msg, "untracked files on %s\n", msg->buf);
	if (pipe_command(&cp_upd_index, files.buf, files.len, NULL, 0,
			 NULL, 0)) {
		ret = -1;
		goto done;
	}

	if (write_index_as_tree(&info->u_tree, &istate, stash_index_path.buf, 0,
				NULL)) {
		ret = -1;
		goto done;
	}

	if (commit_tree(untracked_msg.buf, untracked_msg.len,
			&info->u_tree, NULL, &info->u_commit, NULL, NULL)) {
		ret = -1;
		goto done;
	}

done:
	release_index(&istate);
	strbuf_release(&untracked_msg);
	remove_path(stash_index_path.buf);
	return ret;
}

static int stash_staged(struct stash_info *info, struct strbuf *out_patch,
			int quiet)
{
	int ret = 0;
	struct child_process cp_diff_tree = CHILD_PROCESS_INIT;
	struct index_state istate = INDEX_STATE_INIT(the_repository);

	if (write_index_as_tree(&info->w_tree, &istate, the_repository->index_file,
				0, NULL)) {
		ret = -1;
		goto done;
	}

	cp_diff_tree.git_cmd = 1;
	strvec_pushl(&cp_diff_tree.args, "diff-tree", "-p", "-U1", "HEAD",
		     oid_to_hex(&info->w_tree), "--", NULL);
	if (pipe_command(&cp_diff_tree, NULL, 0, out_patch, 0, NULL, 0)) {
		ret = -1;
		goto done;
	}

	if (!out_patch->len) {
		if (!quiet)
			fprintf_ln(stderr, _("No staged changes"));
		ret = 1;
	}

done:
	release_index(&istate);
	return ret;
}

static int stash_patch(struct stash_info *info, const struct pathspec *ps,
		       struct strbuf *out_patch, int quiet)
{
	int ret = 0;
	struct child_process cp_read_tree = CHILD_PROCESS_INIT;
	struct child_process cp_diff_tree = CHILD_PROCESS_INIT;
	struct index_state istate = INDEX_STATE_INIT(the_repository);
	char *old_index_env = NULL, *old_repo_index_file;

	remove_path(stash_index_path.buf);

	cp_read_tree.git_cmd = 1;
	strvec_pushl(&cp_read_tree.args, "read-tree", "HEAD", NULL);
	strvec_pushf(&cp_read_tree.env, "GIT_INDEX_FILE=%s",
		     stash_index_path.buf);
	if (run_command(&cp_read_tree)) {
		ret = -1;
		goto done;
	}

	/* Find out what the user wants. */
	old_repo_index_file = the_repository->index_file;
	the_repository->index_file = stash_index_path.buf;
	old_index_env = xstrdup_or_null(getenv(INDEX_ENVIRONMENT));
	setenv(INDEX_ENVIRONMENT, the_repository->index_file, 1);

	ret = !!run_add_p(the_repository, ADD_P_STASH, NULL, ps);

	the_repository->index_file = old_repo_index_file;
	if (old_index_env && *old_index_env)
		setenv(INDEX_ENVIRONMENT, old_index_env, 1);
	else
		unsetenv(INDEX_ENVIRONMENT);
	FREE_AND_NULL(old_index_env);

	/* State of the working tree. */
	if (write_index_as_tree(&info->w_tree, &istate, stash_index_path.buf, 0,
				NULL)) {
		ret = -1;
		goto done;
	}

	cp_diff_tree.git_cmd = 1;
	strvec_pushl(&cp_diff_tree.args, "diff-tree", "-p", "-U1", "HEAD",
		     oid_to_hex(&info->w_tree), "--", NULL);
	if (pipe_command(&cp_diff_tree, NULL, 0, out_patch, 0, NULL, 0)) {
		ret = -1;
		goto done;
	}

	if (!out_patch->len) {
		if (!quiet)
			fprintf_ln(stderr, _("No changes selected"));
		ret = 1;
	}

done:
	release_index(&istate);
	remove_path(stash_index_path.buf);
	return ret;
}

static int stash_working_tree(struct stash_info *info, const struct pathspec *ps)
{
	int ret = 0;
	struct rev_info rev;
	struct child_process cp_upd_index = CHILD_PROCESS_INIT;
	struct strbuf diff_output = STRBUF_INIT;
	struct index_state istate = INDEX_STATE_INIT(the_repository);

	init_revisions(&rev, NULL);
	copy_pathspec(&rev.prune_data, ps);

	set_alternate_index_output(stash_index_path.buf);
	if (reset_tree(&info->i_tree, 0, 0)) {
		ret = -1;
		goto done;
	}
	set_alternate_index_output(NULL);

	rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = add_diff_to_buf;
	rev.diffopt.format_callback_data = &diff_output;

	if (repo_read_index_preload(the_repository, &rev.diffopt.pathspec, 0) < 0) {
		ret = -1;
		goto done;
	}

	add_pending_object(&rev, parse_object(the_repository, &info->b_commit),
			   "");
	if (run_diff_index(&rev, 0)) {
		ret = -1;
		goto done;
	}

	cp_upd_index.git_cmd = 1;
	strvec_pushl(&cp_upd_index.args, "update-index",
		     "--ignore-skip-worktree-entries",
		     "-z", "--add", "--remove", "--stdin", NULL);
	strvec_pushf(&cp_upd_index.env, "GIT_INDEX_FILE=%s",
		     stash_index_path.buf);

	if (pipe_command(&cp_upd_index, diff_output.buf, diff_output.len,
			 NULL, 0, NULL, 0)) {
		ret = -1;
		goto done;
	}

	if (write_index_as_tree(&info->w_tree, &istate, stash_index_path.buf, 0,
				NULL)) {
		ret = -1;
		goto done;
	}

done:
	release_index(&istate);
	release_revisions(&rev);
	strbuf_release(&diff_output);
	remove_path(stash_index_path.buf);
	return ret;
}

static int do_create_stash(const struct pathspec *ps, struct strbuf *stash_msg_buf,
			   int include_untracked, int patch_mode, int only_staged,
			   struct stash_info *info, struct strbuf *patch,
			   int quiet)
{
	int ret = 0;
	int flags = 0;
	int untracked_commit_option = 0;
	const char *head_short_sha1 = NULL;
	const char *branch_ref = NULL;
	const char *branch_name = "(no branch)";
	struct commit *head_commit = NULL;
	struct commit_list *parents = NULL;
	struct strbuf msg = STRBUF_INIT;
	struct strbuf commit_tree_label = STRBUF_INIT;
	struct strbuf untracked_files = STRBUF_INIT;

	prepare_fallback_ident("git stash", "git@stash");

	repo_read_index_preload(the_repository, NULL, 0);
	if (repo_refresh_and_write_index(the_repository, REFRESH_QUIET, 0, 0,
					 NULL, NULL, NULL) < 0) {
		ret = -1;
		goto done;
	}

	if (get_oid("HEAD", &info->b_commit)) {
		if (!quiet)
			fprintf_ln(stderr, _("You do not have "
					     "the initial commit yet"));
		ret = -1;
		goto done;
	} else {
		head_commit = lookup_commit(the_repository, &info->b_commit);
	}

	if (!check_changes(ps, include_untracked, &untracked_files)) {
		ret = 1;
		goto done;
	}

	branch_ref = resolve_ref_unsafe("HEAD", 0, NULL, &flags);
	if (flags & REF_ISSYMREF)
		skip_prefix(branch_ref, "refs/heads/", &branch_name);
	head_short_sha1 = find_unique_abbrev(&head_commit->object.oid,
					     DEFAULT_ABBREV);
	strbuf_addf(&msg, "%s: %s ", branch_name, head_short_sha1);
	pp_commit_easy(CMIT_FMT_ONELINE, head_commit, &msg);

	strbuf_addf(&commit_tree_label, "index on %s\n", msg.buf);
	commit_list_insert(head_commit, &parents);
	if (write_index_as_tree(&info->i_tree, &the_index, get_index_file(), 0,
				NULL) ||
	    commit_tree(commit_tree_label.buf, commit_tree_label.len,
			&info->i_tree, parents, &info->i_commit, NULL, NULL)) {
		if (!quiet)
			fprintf_ln(stderr, _("Cannot save the current "
					     "index state"));
		ret = -1;
		goto done;
	}

	if (include_untracked) {
		if (save_untracked_files(info, &msg, untracked_files)) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot save "
						     "the untracked files"));
			ret = -1;
			goto done;
		}
		untracked_commit_option = 1;
	}
	if (patch_mode) {
		ret = stash_patch(info, ps, patch, quiet);
		if (ret < 0) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot save the current "
						     "worktree state"));
			goto done;
		} else if (ret > 0) {
			goto done;
		}
	} else if (only_staged) {
		ret = stash_staged(info, patch, quiet);
		if (ret < 0) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot save the current "
						     "staged state"));
			goto done;
		} else if (ret > 0) {
			goto done;
		}
	} else {
		if (stash_working_tree(info, ps)) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot save the current "
						     "worktree state"));
			ret = -1;
			goto done;
		}
	}

	if (!stash_msg_buf->len)
		strbuf_addf(stash_msg_buf, "WIP on %s", msg.buf);
	else
		strbuf_insertf(stash_msg_buf, 0, "On %s: ", branch_name);

	/*
	 * `parents` will be empty after calling `commit_tree()`, so there is
	 * no need to call `free_commit_list()`
	 */
	parents = NULL;
	if (untracked_commit_option)
		commit_list_insert(lookup_commit(the_repository,
						 &info->u_commit),
				   &parents);
	commit_list_insert(lookup_commit(the_repository, &info->i_commit),
			   &parents);
	commit_list_insert(head_commit, &parents);

	if (commit_tree(stash_msg_buf->buf, stash_msg_buf->len, &info->w_tree,
			parents, &info->w_commit, NULL, NULL)) {
		if (!quiet)
			fprintf_ln(stderr, _("Cannot record "
					     "working tree state"));
		ret = -1;
		goto done;
	}

done:
	strbuf_release(&commit_tree_label);
	strbuf_release(&msg);
	strbuf_release(&untracked_files);
	return ret;
}

static int create_stash(int argc, const char **argv, const char *prefix UNUSED)
{
	int ret;
	struct strbuf stash_msg_buf = STRBUF_INIT;
	struct stash_info info = STASH_INFO_INIT;
	struct pathspec ps;

	/* Starting with argv[1], since argv[0] is "create" */
	strbuf_join_argv(&stash_msg_buf, argc - 1, ++argv, ' ');

	memset(&ps, 0, sizeof(ps));
	if (!check_changes_tracked_files(&ps))
		return 0;

	ret = do_create_stash(&ps, &stash_msg_buf, 0, 0, 0, &info,
			      NULL, 0);
	if (!ret)
		printf_ln("%s", oid_to_hex(&info.w_commit));

	free_stash_info(&info);
	strbuf_release(&stash_msg_buf);
	return ret;
}

static int do_push_stash(const struct pathspec *ps, const char *stash_msg, int quiet,
			 int keep_index, int patch_mode, int include_untracked, int only_staged)
{
	int ret = 0;
	struct stash_info info = STASH_INFO_INIT;
	struct strbuf patch = STRBUF_INIT;
	struct strbuf stash_msg_buf = STRBUF_INIT;
	struct strbuf untracked_files = STRBUF_INIT;

	if (patch_mode && keep_index == -1)
		keep_index = 1;

	if (patch_mode && include_untracked) {
		fprintf_ln(stderr, _("Can't use --patch and --include-untracked"
				     " or --all at the same time"));
		ret = -1;
		goto done;
	}

	/* --patch overrides --staged */
	if (patch_mode)
		only_staged = 0;

	if (only_staged && include_untracked) {
		fprintf_ln(stderr, _("Can't use --staged and --include-untracked"
				     " or --all at the same time"));
		ret = -1;
		goto done;
	}

	repo_read_index_preload(the_repository, NULL, 0);
	if (!include_untracked && ps->nr) {
		int i;
		char *ps_matched = xcalloc(ps->nr, 1);

		/* TODO: audit for interaction with sparse-index. */
		ensure_full_index(&the_index);
		for (i = 0; i < the_index.cache_nr; i++)
			ce_path_match(&the_index, the_index.cache[i], ps,
				      ps_matched);

		if (report_path_error(ps_matched, ps)) {
			fprintf_ln(stderr, _("Did you forget to 'git add'?"));
			ret = -1;
			free(ps_matched);
			goto done;
		}
		free(ps_matched);
	}

	if (repo_refresh_and_write_index(the_repository, REFRESH_QUIET, 0, 0,
					 NULL, NULL, NULL)) {
		ret = -1;
		goto done;
	}

	if (!check_changes(ps, include_untracked, &untracked_files)) {
		if (!quiet)
			printf_ln(_("No local changes to save"));
		goto done;
	}

	if (!reflog_exists(ref_stash) && do_clear_stash()) {
		ret = -1;
		if (!quiet)
			fprintf_ln(stderr, _("Cannot initialize stash"));
		goto done;
	}

	if (stash_msg)
		strbuf_addstr(&stash_msg_buf, stash_msg);
	if (do_create_stash(ps, &stash_msg_buf, include_untracked, patch_mode, only_staged,
			    &info, &patch, quiet)) {
		ret = -1;
		goto done;
	}

	if (do_store_stash(&info.w_commit, stash_msg_buf.buf, 1)) {
		ret = -1;
		if (!quiet)
			fprintf_ln(stderr, _("Cannot save the current status"));
		goto done;
	}

	if (!quiet)
		printf_ln(_("Saved working directory and index state %s"),
			  stash_msg_buf.buf);

	if (!(patch_mode || only_staged)) {
		if (include_untracked && !ps->nr) {
			struct child_process cp = CHILD_PROCESS_INIT;

			cp.git_cmd = 1;
			if (startup_info->original_cwd) {
				cp.dir = startup_info->original_cwd;
				strvec_pushf(&cp.env, "%s=%s",
					     GIT_WORK_TREE_ENVIRONMENT,
					     the_repository->worktree);
			}
			strvec_pushl(&cp.args, "clean", "--force",
				     "--quiet", "-d", ":/", NULL);
			if (include_untracked == INCLUDE_ALL_FILES)
				strvec_push(&cp.args, "-x");
			if (run_command(&cp)) {
				ret = -1;
				goto done;
			}
		}
		discard_index(&the_index);
		if (ps->nr) {
			struct child_process cp_add = CHILD_PROCESS_INIT;
			struct child_process cp_diff = CHILD_PROCESS_INIT;
			struct child_process cp_apply = CHILD_PROCESS_INIT;
			struct strbuf out = STRBUF_INIT;

			cp_add.git_cmd = 1;
			strvec_push(&cp_add.args, "add");
			if (!include_untracked)
				strvec_push(&cp_add.args, "-u");
			if (include_untracked == INCLUDE_ALL_FILES)
				strvec_push(&cp_add.args, "--force");
			strvec_push(&cp_add.args, "--");
			add_pathspecs(&cp_add.args, ps);
			if (run_command(&cp_add)) {
				ret = -1;
				goto done;
			}

			cp_diff.git_cmd = 1;
			strvec_pushl(&cp_diff.args, "diff-index", "-p",
				     "--cached", "--binary", "HEAD", "--",
				     NULL);
			add_pathspecs(&cp_diff.args, ps);
			if (pipe_command(&cp_diff, NULL, 0, &out, 0, NULL, 0)) {
				ret = -1;
				goto done;
			}

			cp_apply.git_cmd = 1;
			strvec_pushl(&cp_apply.args, "apply", "--index",
				     "-R", NULL);
			if (pipe_command(&cp_apply, out.buf, out.len, NULL, 0,
					 NULL, 0)) {
				ret = -1;
				goto done;
			}
		} else {
			struct child_process cp = CHILD_PROCESS_INIT;
			cp.git_cmd = 1;
			/* BUG: this nukes untracked files in the way */
			strvec_pushl(&cp.args, "reset", "--hard", "-q",
				     "--no-recurse-submodules", NULL);
			if (run_command(&cp)) {
				ret = -1;
				goto done;
			}
		}

		if (keep_index == 1 && !is_null_oid(&info.i_tree)) {
			struct child_process cp = CHILD_PROCESS_INIT;

			cp.git_cmd = 1;
			strvec_pushl(&cp.args, "checkout", "--no-overlay",
				     oid_to_hex(&info.i_tree), "--", NULL);
			if (!ps->nr)
				strvec_push(&cp.args, ":/");
			else
				add_pathspecs(&cp.args, ps);
			if (run_command(&cp)) {
				ret = -1;
				goto done;
			}
		}
		goto done;
	} else {
		struct child_process cp = CHILD_PROCESS_INIT;

		cp.git_cmd = 1;
		strvec_pushl(&cp.args, "apply", "-R", NULL);

		if (pipe_command(&cp, patch.buf, patch.len, NULL, 0, NULL, 0)) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot remove "
						     "worktree changes"));
			ret = -1;
			goto done;
		}

		if (keep_index < 1) {
			struct child_process cp = CHILD_PROCESS_INIT;

			cp.git_cmd = 1;
			strvec_pushl(&cp.args, "reset", "-q", "--refresh", "--",
				     NULL);
			add_pathspecs(&cp.args, ps);
			if (run_command(&cp)) {
				ret = -1;
				goto done;
			}
		}
		goto done;
	}

done:
	strbuf_release(&patch);
	free_stash_info(&info);
	strbuf_release(&stash_msg_buf);
	strbuf_release(&untracked_files);
	return ret;
}

static int push_stash(int argc, const char **argv, const char *prefix,
		      int push_assumed)
{
	int force_assume = 0;
	int keep_index = -1;
	int only_staged = 0;
	int patch_mode = 0;
	int include_untracked = 0;
	int quiet = 0;
	int pathspec_file_nul = 0;
	const char *stash_msg = NULL;
	const char *pathspec_from_file = NULL;
	struct pathspec ps;
	struct option options[] = {
		OPT_BOOL('k', "keep-index", &keep_index,
			 N_("keep index")),
		OPT_BOOL('S', "staged", &only_staged,
			 N_("stash staged changes only")),
		OPT_BOOL('p', "patch", &patch_mode,
			 N_("stash in patch mode")),
		OPT__QUIET(&quiet, N_("quiet mode")),
		OPT_BOOL('u', "include-untracked", &include_untracked,
			 N_("include untracked files in stash")),
		OPT_SET_INT('a', "all", &include_untracked,
			    N_("include ignore files"), 2),
		OPT_STRING('m', "message", &stash_msg, N_("message"),
			   N_("stash message")),
		OPT_PATHSPEC_FROM_FILE(&pathspec_from_file),
		OPT_PATHSPEC_FILE_NUL(&pathspec_file_nul),
		OPT_END()
	};
	int ret;

	if (argc) {
		force_assume = !strcmp(argv[0], "-p");
		argc = parse_options(argc, argv, prefix, options,
				     push_assumed ? git_stash_usage :
				     git_stash_push_usage,
				     PARSE_OPT_KEEP_DASHDASH);
	}

	if (argc) {
		if (!strcmp(argv[0], "--")) {
			argc--;
			argv++;
		} else if (push_assumed && !force_assume) {
			die("subcommand wasn't specified; 'push' can't be assumed due to unexpected token '%s'",
			    argv[0]);
		}
	}

	parse_pathspec(&ps, 0, PATHSPEC_PREFER_FULL | PATHSPEC_PREFIX_ORIGIN,
		       prefix, argv);

	if (pathspec_from_file) {
		if (patch_mode)
			die(_("options '%s' and '%s' cannot be used together"), "--pathspec-from-file", "--patch");

		if (only_staged)
			die(_("options '%s' and '%s' cannot be used together"), "--pathspec-from-file", "--staged");

		if (ps.nr)
			die(_("'%s' and pathspec arguments cannot be used together"), "--pathspec-from-file");

		parse_pathspec_file(&ps, 0,
				    PATHSPEC_PREFER_FULL | PATHSPEC_PREFIX_ORIGIN,
				    prefix, pathspec_from_file, pathspec_file_nul);
	} else if (pathspec_file_nul) {
		die(_("the option '%s' requires '%s'"), "--pathspec-file-nul", "--pathspec-from-file");
	}

	ret = do_push_stash(&ps, stash_msg, quiet, keep_index, patch_mode,
			    include_untracked, only_staged);
	clear_pathspec(&ps);
	return ret;
}

static int push_stash_unassumed(int argc, const char **argv, const char *prefix)
{
	return push_stash(argc, argv, prefix, 0);
}

static int save_stash(int argc, const char **argv, const char *prefix)
{
	int keep_index = -1;
	int only_staged = 0;
	int patch_mode = 0;
	int include_untracked = 0;
	int quiet = 0;
	int ret = 0;
	const char *stash_msg = NULL;
	struct pathspec ps;
	struct strbuf stash_msg_buf = STRBUF_INIT;
	struct option options[] = {
		OPT_BOOL('k', "keep-index", &keep_index,
			 N_("keep index")),
		OPT_BOOL('S', "staged", &only_staged,
			 N_("stash staged changes only")),
		OPT_BOOL('p', "patch", &patch_mode,
			 N_("stash in patch mode")),
		OPT__QUIET(&quiet, N_("quiet mode")),
		OPT_BOOL('u', "include-untracked", &include_untracked,
			 N_("include untracked files in stash")),
		OPT_SET_INT('a', "all", &include_untracked,
			    N_("include ignore files"), 2),
		OPT_STRING('m', "message", &stash_msg, "message",
			   N_("stash message")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_save_usage,
			     PARSE_OPT_KEEP_DASHDASH);

	if (argc)
		stash_msg = strbuf_join_argv(&stash_msg_buf, argc, argv, ' ');

	memset(&ps, 0, sizeof(ps));
	ret = do_push_stash(&ps, stash_msg, quiet, keep_index,
			    patch_mode, include_untracked, only_staged);

	strbuf_release(&stash_msg_buf);
	return ret;
}

int cmd_stash(int argc, const char **argv, const char *prefix)
{
	pid_t pid = getpid();
	const char *index_file;
	struct strvec args = STRVEC_INIT;
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("apply", &fn, apply_stash),
		OPT_SUBCOMMAND("clear", &fn, clear_stash),
		OPT_SUBCOMMAND("drop", &fn, drop_stash),
		OPT_SUBCOMMAND("pop", &fn, pop_stash),
		OPT_SUBCOMMAND("branch", &fn, branch_stash),
		OPT_SUBCOMMAND("list", &fn, list_stash),
		OPT_SUBCOMMAND("show", &fn, show_stash),
		OPT_SUBCOMMAND("store", &fn, store_stash),
		OPT_SUBCOMMAND("create", &fn, create_stash),
		OPT_SUBCOMMAND("push", &fn, push_stash_unassumed),
		OPT_SUBCOMMAND_F("save", &fn, save_stash, PARSE_OPT_NOCOMPLETE),
		OPT_END()
	};

	git_config(git_stash_config, NULL);

	argc = parse_options(argc, argv, prefix, options, git_stash_usage,
			     PARSE_OPT_SUBCOMMAND_OPTIONAL |
			     PARSE_OPT_KEEP_UNKNOWN_OPT |
			     PARSE_OPT_KEEP_DASHDASH);

	prepare_repo_settings(the_repository);
	the_repository->settings.command_requires_full_index = 0;

	index_file = get_index_file();
	strbuf_addf(&stash_index_path, "%s.stash.%" PRIuMAX, index_file,
		    (uintmax_t)pid);

	if (fn)
		return !!fn(argc, argv, prefix);
	else if (!argc)
		return !!push_stash_unassumed(0, NULL, prefix);

	/* Assume 'stash push' */
	strvec_push(&args, "push");
	strvec_pushv(&args, argv);
	return !!push_stash(args.nr, args.v, prefix, 1);
}
