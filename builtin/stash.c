#include "builtin.h"
#include "config.h"
#include "parse-options.h"
#include "refs.h"
#include "lockfile.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "merge-recursive.h"
#include "argv-array.h"
#include "run-command.h"
#include "dir.h"
#include "rerere.h"
#include "revision.h"
#include "log-tree.h"
#include "diffcore.h"
#include "apply.h"

static const char * const git_stash_usage[] = {
	N_("git stash list [<options>]"),
	N_("git stash show [<options>] [<stash>]"),
	N_("git stash drop [-q|--quiet] [<stash>]"),
	N_("git stash ( pop | apply ) [--index] [-q|--quiet] [<stash>]"),
	N_("git stash branch <branchname> [<stash>]"),
	N_("git stash clear"),
	N_("git stash store [-m|--message <message>] [-q|--quiet] <commit>"),
	N_("git stash create [<message>]"),
	N_("git stash [push [-p|--patch] [-k|--[no-]keep-index] [-q|--quiet]\n"
	   "          [-u|--include-untracked] [-a|--all] [-m|--message <message>]\n"
	   "          [--] [<pathspec>...]]"),
	N_("git stash save [-p|--patch] [-k|--[no-]keep-index] [-q|--quiet]\n"
	   "          [-u|--include-untracked] [-a|--all] [<message>]"),
	NULL
};

static const char * const git_stash_list_usage[] = {
	N_("git stash list [<options>]"),
	NULL
};

static const char * const git_stash_show_usage[] = {
	N_("git stash show [<options>] [<stash>]"),
	NULL
};

static const char * const git_stash_drop_usage[] = {
	N_("git stash drop [-q|--quiet] [<stash>]"),
	NULL
};

static const char * const git_stash_pop_usage[] = {
	N_("git stash pop [--index] [-q|--quiet] [<stash>]"),
	NULL
};

static const char * const git_stash_apply_usage[] = {
	N_("git stash apply [--index] [-q|--quiet] [<stash>]"),
	NULL
};

static const char * const git_stash_branch_usage[] = {
	N_("git stash branch <branchname> [<stash>]"),
	NULL
};

static const char * const git_stash_clear_usage[] = {
	N_("git stash clear"),
	NULL
};

static const char * const git_stash_store_usage[] = {
	N_("git stash store [-m|--message <message>] [-q|--quiet] <commit>"),
	NULL
};

static const char * const git_stash_create_usage[] = {
	N_("git stash create [<message>]"),
	NULL
};

static const char * const git_stash_push_usage[] = {
	N_("git stash [push [-p|--patch] [-k|--[no-]keep-index] [-q|--quiet]\n"
	   "          [-u|--include-untracked] [-a|--all] [-m|--message <message>]\n"
	   "          [--] [<pathspec>...]]"),
	NULL
};

static const char * const git_stash_save_usage[] = {
	N_("git stash save [-p|--patch] [-k|--[no-]keep-index] [-q|--quiet]\n"
	   "          [-u|--include-untracked] [-a|--all] [<message>]"),
	NULL
};

static const char *ref_stash = "refs/stash";
static int quiet;
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

static void free_stash_info(struct stash_info *info)
{
	strbuf_release(&info->revision);
}

static void assert_stash_like(struct stash_info *info, const char * revision)
{
	if (get_oidf(&info->b_commit, "%s^1", revision) ||
	    get_oidf(&info->w_tree, "%s:", revision) ||
	    get_oidf(&info->b_tree, "%s^1:", revision) ||
	    get_oidf(&info->i_tree, "%s^2:", revision)) {
		free_stash_info(info);
		error(_("'%s' is not a stash-like commit"), revision);
		exit(128);
	}
}

static int get_stash_info(struct stash_info *info, int argc, const char **argv)
{
	struct strbuf symbolic = STRBUF_INIT;
	int ret;
	const char *revision;
	const char *commit = NULL;
	char *end_of_rev;
	char *expanded_ref;
	struct object_id dummy;

	if (argc > 1) {
		int i;
		struct strbuf refs_msg = STRBUF_INIT;
		for (i = 0; i < argc; ++i)
			strbuf_addf(&refs_msg, " '%s'", argv[i]);

		fprintf_ln(stderr, _("Too many revisions specified:%s"),
			   refs_msg.buf);
		strbuf_release(&refs_msg);

		return -1;
	}

	if (argc == 1)
		commit = argv[0];

	strbuf_init(&info->revision, 0);
	if (!commit) {
		if (!ref_exists(ref_stash)) {
			free_stash_info(info);
			fprintf_ln(stderr, "No stash entries found.");
			return -1;
		}

		strbuf_addf(&info->revision, "%s@{0}", ref_stash);
	} else if (strspn(commit, "0123456789") == strlen(commit)) {
		strbuf_addf(&info->revision, "%s@{%s}", ref_stash, commit);
	} else {
		strbuf_addstr(&info->revision, commit);
	}

	revision = info->revision.buf;

	if (get_oid(revision, &info->w_commit)) {
		error(_("%s is not a valid reference"), revision);
		free_stash_info(info);
		return -1;
	}

	assert_stash_like(info, revision);

	info->has_u = !get_oidf(&info->u_tree, "%s^3:", revision);

	end_of_rev = strchrnul(revision, '@');
	strbuf_add(&symbolic, revision, end_of_rev - revision);

	ret = dwim_ref(symbolic.buf, symbolic.len, &dummy, &expanded_ref);
	strbuf_release(&symbolic);
	switch (ret) {
	case 0: /* Not found, but valid ref */
		info->is_stash_ref = 0;
		break;
	case 1:
		info->is_stash_ref = !strcmp(expanded_ref, ref_stash);
		break;
	default: /* Invalid or ambiguous */
		free_stash_info(info);
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

	if (argc != 0)
		return error(_("git stash clear with parameters is unimplemented"));

	return do_clear_stash();
}

static int reset_tree(struct object_id *i_tree, int update, int reset)
{
	struct unpack_trees_options opts;
	int nr_trees = 1;
	struct tree_desc t[MAX_UNPACK_TREES];
	struct tree *tree;
	struct lock_file lock_file = LOCK_INIT;

	read_cache_preload(NULL);
	if (refresh_cache(REFRESH_QUIET))
		return -1;

	hold_locked_index(&lock_file, LOCK_DIE_ON_ERROR);

	memset(&opts, 0, sizeof(opts));

	tree = parse_tree_indirect(i_tree);
	if (parse_tree(tree))
		return -1;

	init_tree_desc(t, tree->buffer, tree->size);

	opts.head_idx = 1;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
	opts.merge = 1;
	opts.reset = reset;
	opts.update = update;
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

	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "diff-tree", "--binary", NULL);
	argv_array_pushf(&cp.args, "%s^2^..%s^2", w_commit_hex, w_commit_hex);

	return pipe_command(&cp, NULL, 0, out, 0, NULL, 0);
}

static int apply_patch_from_buf(struct strbuf *patch, int cached, int reverse,
				int check_index)
{
	int ret = 0;
	struct apply_state state;
	struct argv_array args = ARGV_ARRAY_INIT;
	const char *patch_path = ".git/stash_patch.patch";
	FILE *patch_file;

	if (init_apply_state(&state, the_repository, NULL))
		return -1;

	state.cached = cached;
	state.apply_in_reverse = reverse;
	state.check_index = check_index;
	if (state.cached)
		state.check_index = 1;
	if (state.check_index)
		state.unsafe_paths = 0;

	patch_file = fopen(patch_path, "w");
	strbuf_write(patch, patch_file);
	fclose(patch_file);

	argv_array_push(&args, patch_path);
	ret = apply_all_patches(&state, args.argc, args.argv, 0);

	remove_path(patch_path);
	clear_apply_state(&state);
	return ret;
}

static int reset_head(const char *prefix)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	/*
	 * Reset is overall quite simple, however there is no current public
	 * API for resetting.
	 */
	cp.git_cmd = 1;
	argv_array_push(&cp.args, "reset");

	return run_command(&cp);
}

static void add_diff_to_buf(struct diff_queue_struct *q,
			    struct diff_options *options,
			    void *data)
{
	int i;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		strbuf_addstr(data, p->one->path);
		strbuf_addch(data, '\n');
	}
}

static int get_newly_staged(struct strbuf *out, struct object_id *c_tree)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	const char *c_tree_hex = oid_to_hex(c_tree);

	/*
	 * diff-index is very similar to diff-tree above, and should be
	 * converted together with update_index.
	 */
	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "diff-index", "--cached", "--name-only",
			 "--diff-filter=A", NULL);
	argv_array_push(&cp.args, c_tree_hex);
	return pipe_command(&cp, NULL, 0, out, 0, NULL, 0);
}

static int update_index(struct strbuf *out)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	/*
	 * Update-index is very complicated and may need to have a public
	 * function exposed in order to remove this forking.
	 */
	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "update-index", "--add", "--stdin", NULL);
	return pipe_command(&cp, out->buf, out->len, NULL, 0, NULL, 0);
}

static int restore_untracked(struct object_id *u_tree)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	int res;

	/*
	 * We need to run restore files from a given index, but without
	 * affecting the current index, so we use GIT_INDEX_FILE with
	 * run_command to fork processes that will not interfere.
	 */
	cp.git_cmd = 1;
	argv_array_push(&cp.args, "read-tree");
	argv_array_push(&cp.args, oid_to_hex(u_tree));
	argv_array_pushf(&cp.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);
	if (run_command(&cp)) {
		remove_path(stash_index_path.buf);
		return -1;
	}

	child_process_init(&cp);
	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "checkout-index", "--all", NULL);
	argv_array_pushf(&cp.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);

	res = run_command(&cp);
	remove_path(stash_index_path.buf);
	return res;
}

static int do_apply_stash(const char *prefix, struct stash_info *info,
	int index)
{
	struct merge_options o;
	struct object_id c_tree;
	struct object_id index_tree;
	const struct object_id *bases[1];
	struct commit *result;
	int ret;
	int has_index = index;

	read_cache_preload(NULL);
	if (refresh_cache(REFRESH_QUIET))
		return -1;

	if (write_cache_as_tree(&c_tree, 0, NULL) || reset_tree(&c_tree, 0, 0))
		return error(_("Cannot apply a stash in the middle of a merge"));

	if (index) {
		if (!oidcmp(&info->b_tree, &info->i_tree) || !oidcmp(&c_tree,
			&info->i_tree)) {
			has_index = 0;
		} else {
			struct strbuf out = STRBUF_INIT;

			if (diff_tree_binary(&out, &info->w_commit)) {
				strbuf_release(&out);
				return -1;
			}

			ret = apply_patch_from_buf(&out, 1, 0, 0);
			strbuf_release(&out);
			if (ret)
				return -1;

			discard_cache();
			read_cache();
			if (write_cache_as_tree(&index_tree, 0, NULL))
				return -1;

			reset_head(prefix);
		}
	}

	if (info->has_u && restore_untracked(&info->u_tree))
		return error(_("Could not restore untracked files from stash"));

	init_merge_options(&o);

	o.branch1 = "Updated upstream";
	o.branch2 = "Stashed changes";

	if (!oidcmp(&info->b_tree, &c_tree))
		o.branch1 = "Version stash was based on";

	if (quiet)
		o.verbosity = 0;

	if (o.verbosity >= 3)
		printf_ln(_("Merging %s with %s"), o.branch1, o.branch2);

	bases[0] = &info->b_tree;

	ret = merge_recursive_generic(&o, &c_tree, &info->w_tree, 1, bases,
				      &result);
	if (ret != 0) {
		rerere(0);

		if (index)
			fprintf_ln(stderr, _("Index was not unstashed."));

		return ret;
	}

	if (has_index) {
		if (reset_tree(&index_tree, 0, 0))
			return -1;
	} else {
		struct strbuf out = STRBUF_INIT;

		if (get_newly_staged(&out, &c_tree)) {
			strbuf_release(&out);
			return -1;
		}

		if (reset_tree(&c_tree, 0, 1)) {
			strbuf_release(&out);
			return -1;
		}

		ret = update_index(&out);
		strbuf_release(&out);
		if (ret)
			return -1;

		discard_cache();
	}

	if (quiet) {
		if (refresh_cache(REFRESH_QUIET))
			warning("could not refresh index");
	} else {
		struct child_process cp = CHILD_PROCESS_INIT;

		/*
		 * Status is quite simple and could be replaced with calls to
		 * wt_status in the future, but it adds complexities which may
		 * require more tests.
		 */
		cp.git_cmd = 1;
		cp.dir = prefix;
		argv_array_push(&cp.args, "status");
		run_command(&cp);
	}

	return 0;
}

static int apply_stash(int argc, const char **argv, const char *prefix)
{
	int index = 0;
	struct stash_info info;
	int ret;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet, only report errors")),
		OPT_BOOL(0, "index", &index,
			N_("attempt to recreate the index")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_apply_usage, 0);

	if (get_stash_info(&info, argc, argv))
		return -1;

	ret = do_apply_stash(prefix, &info, index);
	free_stash_info(&info);
	return ret;
}

static int do_drop_stash(const char *prefix, struct stash_info *info)
{
	struct child_process cp_reflog = CHILD_PROCESS_INIT;
	struct child_process cp = CHILD_PROCESS_INIT;
	int ret;

	/*
	 * reflog does not provide a simple function for deleting refs. One will
	 * need to be added to avoid implementing too much reflog code here
	 */

	cp_reflog.git_cmd = 1;
	argv_array_pushl(&cp_reflog.args, "reflog", "delete", "--updateref",
			 "--rewrite", NULL);
	argv_array_push(&cp_reflog.args, info->revision.buf);
	ret = run_command(&cp_reflog);
	if (!ret) {
		if (!quiet)
			printf(_("Dropped %s (%s)\n"), info->revision.buf,
			       oid_to_hex(&info->w_commit));
	} else {
		return error(_("%s: Could not drop stash entry"),
			     info->revision.buf);
	}

	/*
	 * This could easily be replaced by get_oid, but currently it will throw
	 * a fatal error when a reflog is empty, which we can not recover from.
	 */
	cp.git_cmd = 1;
	/* Even though --quiet is specified, rev-parse still outputs the hash */
	cp.no_stdout = 1;
	argv_array_pushl(&cp.args, "rev-parse", "--verify", "--quiet", NULL);
	argv_array_pushf(&cp.args, "%s@{0}", ref_stash);
	ret = run_command(&cp);

	/* do_clear_stash if we just dropped the last stash entry */
	if (ret)
		do_clear_stash();

	return 0;
}

static void assert_stash_ref(struct stash_info *info)
{
	if (!info->is_stash_ref) {
		free_stash_info(info);
		error(_("'%s' is not a stash reference"), info->revision.buf);
		exit(128);
	}
}

static int drop_stash(int argc, const char **argv, const char *prefix)
{
	struct stash_info info;
	int ret;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet, only report errors")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_drop_usage, 0);

	if (get_stash_info(&info, argc, argv))
		return -1;

	assert_stash_ref(&info);

	ret = do_drop_stash(prefix, &info);
	free_stash_info(&info);
	return ret;
}

static int pop_stash(int argc, const char **argv, const char *prefix)
{
	int index = 0, ret;
	struct stash_info info;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet, only report errors")),
		OPT_BOOL(0, "index", &index,
			N_("attempt to recreate the index")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_pop_usage, 0);

	if (get_stash_info(&info, argc, argv))
		return -1;

	assert_stash_ref(&info);
	if ((ret = do_apply_stash(prefix, &info, index)))
		printf_ln(_("The stash entry is kept in case you need it again."));
	else
		ret = do_drop_stash(prefix, &info);

	free_stash_info(&info);
	return ret;
}

static int branch_stash(int argc, const char **argv, const char *prefix)
{
	const char *branch = NULL;
	int ret;
	struct child_process cp = CHILD_PROCESS_INIT;
	struct stash_info info;
	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_branch_usage, 0);

	if (argc == 0)
		return error(_("No branch name specified"));

	branch = argv[0];

	if (get_stash_info(&info, argc - 1, argv + 1))
		return -1;

	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "checkout", "-b", NULL);
	argv_array_push(&cp.args, branch);
	argv_array_push(&cp.args, oid_to_hex(&info.b_commit));
	ret = run_command(&cp);
	if (!ret)
		ret = do_apply_stash(prefix, &info, 1);
	if (!ret && info.is_stash_ref)
		ret = do_drop_stash(prefix, &info);

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
			     PARSE_OPT_KEEP_UNKNOWN);

	if (!ref_exists(ref_stash))
		return 0;

	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "log", "--format=%gd: %gs", "-g",
			 "--first-parent", "-m", NULL);
	argv_array_pushv(&cp.args, argv);
	argv_array_push(&cp.args, ref_stash);
	argv_array_push(&cp.args, "--");
	return run_command(&cp);
}

static int show_stat = 1;
static int show_patch;

static int git_stash_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "stash.showStat")) {
		show_stat = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "stash.showPatch")) {
		show_patch = git_config_bool(var, value);
		return 0;
	}
	return git_default_config(var, value, cb);
}

static int show_stash(int argc, const char **argv, const char *prefix)
{
	int i;
	int flags = 0;
	struct stash_info info;
	struct rev_info rev;
	struct argv_array stash_args = ARGV_ARRAY_INIT;
	struct option options[] = {
		OPT_END()
	};

	init_diff_ui_defaults();
	git_config(git_diff_ui_config, NULL);

	init_revisions(&rev, prefix);

	/* Push arguments which are not options into stash_args. */
	for (i = 1; i < argc; ++i) {
		if (argv[i][0] != '-')
			argv_array_push(&stash_args, argv[i]);
		else
			flags++;
	}

	/*
	 * The config settings are applied only if there are not passed
	 * any flags.
	 */
	if (!flags) {
		git_config(git_stash_config, NULL);
		if (show_stat)
			rev.diffopt.output_format |= DIFF_FORMAT_DIFFSTAT;
		if (show_patch) {
			rev.diffopt.output_format = ~DIFF_FORMAT_NO_OUTPUT;
			rev.diffopt.output_format |= DIFF_FORMAT_PATCH;
		}
	}

	if (get_stash_info(&info, stash_args.argc, stash_args.argv)) {
		argv_array_clear(&stash_args);
		return -1;
	}

	argc = setup_revisions(argc, argv, &rev, NULL);
	if (!rev.diffopt.output_format)
		rev.diffopt.output_format = DIFF_FORMAT_PATCH;
	diff_setup_done(&rev.diffopt);
	rev.diffopt.flags.recursive = 1;
	setup_diff_pager(&rev.diffopt);

	/*
	 * We can return early if there was any option not recognised by
	 * `diff_opt_parse()`, besides the word `stash`.
	 */
	if (argc > 1) {
		free_stash_info(&info);
		argv_array_clear(&stash_args);
		usage_with_options(git_stash_show_usage, options);
	}

	/* Do the diff thing. */
	diff_tree_oid(&info.b_commit, &info.w_commit, "", &rev.diffopt);
	log_tree_diff_flush(&rev);

	free_stash_info(&info);
	argv_array_clear(&stash_args);
	return 0;
}

static int do_store_stash(const char *w_commit, const char *stash_msg,
			  int quiet)
{
	int ret = 0;
	struct object_id obj;

	if (!stash_msg)
		stash_msg  = xstrdup("Created via \"git stash store\".");

	ret = get_oid(w_commit, &obj);
	if (!ret) {
		ret = update_ref(stash_msg, ref_stash, &obj, NULL,
				 REF_FORCE_CREATE_REFLOG,
				 quiet ? UPDATE_REFS_QUIET_ON_ERR :
				 UPDATE_REFS_MSG_ON_ERR);
	}
	if (ret && !quiet)
		fprintf_ln(stderr, _("Cannot update %s with %s"),
			   ref_stash, w_commit);

	return ret;
}

static int store_stash(int argc, const char **argv, const char *prefix)
{
	const char *stash_msg = NULL;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("be quiet, only report errors")),
		OPT_STRING('m', "message", &stash_msg, "message", N_("stash message")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_store_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	if (argc != 1) {
		fprintf(stderr, _("\"git stash store\" requires one <commit> argument\n"));
		return -1;
	}

	return do_store_stash(argv[0], stash_msg, quiet);
}

/*
 * `has_untracked_files` is:
 * -2 if `get_untracked_files()` hasn't been called
 * -1 if there were errors
 *  0 if there are no untracked files
 *  1 if there are untracked files
 *
 * `untracked_files` will be filled with the names of untracked files.
 * The return value is:
 *
 * = 0 if there are not any untracked files
 * > 0 if there are untracked files
 */
static struct strbuf untracked_files = STRBUF_INIT;
static int has_untracked_files = -2;

static int get_untracked_files(const char **argv, const char *prefix,
			       int include_untracked)
{
	int max_len;
	int i;
	char *seen;
	struct dir_struct dir;
	struct pathspec pathspec;

	if (has_untracked_files != -2)
		return has_untracked_files;

	memset(&dir, 0, sizeof(dir));
	if (include_untracked != 2)
		setup_standard_excludes(&dir);

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_FULL,
		       prefix, argv);
	seen = xcalloc(pathspec.nr, 1);

	max_len = fill_directory(&dir, the_repository->index, &pathspec);
	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];
		if (!dir_path_match(&the_index, ent, &pathspec, max_len, seen)) {
			free(ent);
			continue;
		}
		strbuf_addf(&untracked_files, "%s\n", ent->name);
		free(ent);
	}

	free(dir.entries);
	free(dir.ignored);
	clear_directory(&dir);
	free(seen);
	has_untracked_files = untracked_files.len;
	return untracked_files.len;
}

/*
 * `changes` is:
 * -2 if `check_changes()` hasn't been called
 * -1 if there were any errors
 *  0 if there are no changes
 *  1 if there are changes
 *
 * The return value of `check_changes()` can be:
 *
 * < 0 if there was an error
 * = 0 if there are no changes.
 * > 0 if there are changes.
 */
static int changes = -2;

static int check_changes(const char **argv, int include_untracked,
			 const char *prefix)
{
	int result;
	int ret = 0;
	struct rev_info rev;
	struct object_id dummy;
	struct argv_array args = ARGV_ARRAY_INIT;

	if (changes != -2)
		return changes;

	init_revisions(&rev, prefix);
	parse_pathspec(&rev.prune_data, 0, PATHSPEC_PREFER_FULL,
		       prefix, argv);

	rev.diffopt.flags.quick = 1;
	rev.diffopt.flags.ignore_submodules = 1;
	rev.abbrev = 0;

	/* No initial commit. */
	if (get_oid("HEAD", &dummy)) {
		ret = -1;
		goto done;
	}

	add_head_to_pending(&rev);
	diff_setup_done(&rev.diffopt);

	if (read_cache() < 0) {
		ret = -1;
		goto done;
	}
	result = run_diff_index(&rev, 1);
	if (diff_result_code(&rev.diffopt, result)) {
		ret = 1;
		goto done;
	}

	object_array_clear(&rev.pending);
	result = run_diff_files(&rev, 0);
	if (diff_result_code(&rev.diffopt, result)) {
		ret = 1;
		goto done;
	}

	if (include_untracked && get_untracked_files(argv, prefix,
						     include_untracked))
		ret = 1;

done:
	changes = ret;
	argv_array_clear(&args);
	return ret;
}

static int save_untracked_files(struct stash_info *info, struct strbuf *msg)
{
	int ret = 0;
	struct strbuf untracked_msg = STRBUF_INIT;
	struct child_process cp = CHILD_PROCESS_INIT;
	struct index_state state = { NULL };

	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "update-index", "--add",
			 "--remove", "--stdin", NULL);
	argv_array_pushf(&cp.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);

	strbuf_addf(&untracked_msg, "untracked files on %s\n", msg->buf);
	if (pipe_command(&cp, untracked_files.buf, untracked_files.len,
			 NULL, 0, NULL, 0)) {
		ret = -1;
		goto done;
	}

	if (write_index_as_tree(&info->u_tree, &state, stash_index_path.buf, 0,
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
	strbuf_release(&untracked_msg);
	remove_path(stash_index_path.buf);
	return ret;
}

static struct strbuf patch = STRBUF_INIT;

static int stash_patch(struct stash_info *info, const char **argv)
{
	int ret = 0;
	struct child_process cp0 = CHILD_PROCESS_INIT;
	struct child_process cp1 = CHILD_PROCESS_INIT;
	struct child_process cp3 = CHILD_PROCESS_INIT;
	struct index_state state = { NULL };

	remove_path(stash_index_path.buf);

	cp0.git_cmd = 1;
	argv_array_pushl(&cp0.args, "read-tree", "HEAD", NULL);
	argv_array_pushf(&cp0.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);
	if (run_command(&cp0)) {
		ret = -1;
		goto done;
	}

	cp1.git_cmd = 1;
	argv_array_pushl(&cp1.args, "add--interactive", "--patch=stash",
			"--", NULL);
	if (argv)
		argv_array_pushv(&cp1.args, argv);
	argv_array_pushf(&cp1.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);
	if (run_command(&cp1)) {
		ret = -1;
		goto done;
	}

	if (write_index_as_tree(&info->w_tree, &state, stash_index_path.buf, 0,
				NULL)) {
		ret = -1;
		goto done;
	}

	cp3.git_cmd = 1;
	argv_array_pushl(&cp3.args, "diff-tree", "-p", "HEAD",
			 oid_to_hex(&info->w_tree), "--", NULL);
	if (pipe_command(&cp3, NULL, 0, &patch, 0, NULL, 0))
		ret = -1;

	if (!patch.len) {
		fprintf_ln(stdout, "No changes selected");
		ret = 1;
	}

done:
	remove_path(stash_index_path.buf);
	return ret;
}

static int stash_working_tree(struct stash_info *info,
			      const char **argv, const char *prefix)
{
	int ret = 0;
	struct child_process cp2 = CHILD_PROCESS_INIT;
	struct argv_array args = ARGV_ARRAY_INIT;
	struct strbuf diff_output = STRBUF_INIT;
	struct rev_info rev;
	struct index_state state = { NULL };

	set_alternate_index_output(stash_index_path.buf);
	if (reset_tree(&info->i_tree, 0, 0)) {
		ret = -1;
		goto done;
	}
	set_alternate_index_output(".git/index");

	argv_array_push(&args, "dummy");
	if (argv)
		argv_array_pushv(&args, argv);
	git_config(git_diff_basic_config, NULL);
	init_revisions(&rev, prefix);
	args.argc = setup_revisions(args.argc, args.argv, &rev, NULL);

	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = add_diff_to_buf;
	rev.diffopt.format_callback_data = &diff_output;

	if (read_cache_preload(&rev.diffopt.pathspec) < 0) {
		ret = -1;
		goto done;
	}

	add_pending_object(&rev, parse_object(the_repository, &info->b_commit), "");
	if (run_diff_index(&rev, 0)) {
		ret = -1;
		goto done;
	}

	cp2.git_cmd = 1;
	argv_array_pushl(&cp2.args, "update-index", "--add",
			 "--remove", "--stdin", NULL);
	argv_array_pushf(&cp2.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);

	if (pipe_command(&cp2, diff_output.buf, diff_output.len,
			 NULL, 0, NULL, 0)) {
		ret = -1;
		goto done;
	}

	if (write_index_as_tree(&info->w_tree, &state, stash_index_path.buf, 0,
				NULL)) {

		ret = -1;
		goto done;
	}

	discard_cache();
	read_cache();

done:
	UNLEAK(rev);
	argv_array_clear(&args);
	object_array_clear(&rev.pending);
	strbuf_release(&diff_output);
	remove_path(stash_index_path.buf);
	return ret;
}

static int do_create_stash(int argc, const char **argv, const char *prefix,
			   const char **stash_msg, int include_untracked,
			   int patch_mode, struct stash_info *info, int quiet)
{
	int untracked_commit_option = 0;
	int ret = 0;
	int subject_len;
	int flags;
	const char *head_short_sha1 = NULL;
	const char *branch_ref = NULL;
	const char *head_subject = NULL;
	const char *branch_name = "(no branch)";
	struct commit *head_commit = NULL;
	struct commit_list *parents = NULL;
	struct strbuf msg = STRBUF_INIT;
	struct strbuf commit_tree_label = STRBUF_INIT;
	struct strbuf final_stash_msg = STRBUF_INIT;

	read_cache_preload(NULL);
	refresh_cache(REFRESH_QUIET);

	if (!check_changes(argv, include_untracked, prefix)) {
		ret = 1;
		goto done;
	}

	if (get_oid("HEAD", &info->b_commit)) {
		if (!quiet)
			fprintf_ln(stderr, "You do not have the initial commit yet");
		ret = -1;
		goto done;
	} else {
		head_commit = lookup_commit(the_repository, &info->b_commit);
	}

	branch_ref = resolve_ref_unsafe("HEAD", 0, NULL, &flags);
	if (flags & REF_ISSYMREF)
		branch_name = strrchr(branch_ref, '/') + 1;
	head_short_sha1 = find_unique_abbrev(&head_commit->object.oid,
					     DEFAULT_ABBREV);
	subject_len = find_commit_subject(get_commit_buffer(head_commit, NULL),
					  &head_subject);
	strbuf_addf(&msg, "%s: %s %.*s\n", branch_name, head_short_sha1,
		    subject_len, head_subject);

	strbuf_addf(&commit_tree_label, "index on %s\n", msg.buf);
	commit_list_insert(head_commit, &parents);
	if (write_cache_as_tree(&info->i_tree, 0, NULL) ||
	    commit_tree(commit_tree_label.buf, commit_tree_label.len,
			&info->i_tree, parents, &info->i_commit, NULL, NULL)) {
		if (!quiet)
			fprintf_ln(stderr, "Cannot save the current index state");
		ret = -1;
		goto done;
	}

	if (include_untracked && get_untracked_files(argv, prefix,
						     include_untracked)) {
		if (save_untracked_files(info, &msg)) {
			if (!quiet)
				printf_ln("Cannot save the untracked files");
			ret = -1;
			goto done;
		}
		untracked_commit_option = 1;
	}
	if (patch_mode) {
		ret = stash_patch(info, argv);
		if (ret < 0) {
			if (!quiet)
				printf_ln("Cannot save the current worktree state");
			goto done;
		} else if (ret > 0) {
			goto done;
		}
	} else {
		if (stash_working_tree(info, argv, prefix)) {
			if (!quiet)
				printf_ln("Cannot save the current worktree state");
			ret = -1;
			goto done;
		}
	}

	if (!*stash_msg || !strlen(*stash_msg))
		strbuf_addf(&final_stash_msg, "WIP on %s", msg.buf);
	else
		strbuf_addf(&final_stash_msg, "On %s: %s\n", branch_name,
			    *stash_msg);
	*stash_msg = strbuf_detach(&final_stash_msg, NULL);

	/*
	 * `parents` will be empty after calling `commit_tree()`, so there is
	 * no need to call `free_commit_list()`
	 */
	parents = NULL;
	if (untracked_commit_option)
		commit_list_insert(lookup_commit(the_repository, &info->u_commit), &parents);
	commit_list_insert(lookup_commit(the_repository, &info->i_commit), &parents);
	commit_list_insert(head_commit, &parents);

	if (commit_tree(*stash_msg, strlen(*stash_msg), &info->w_tree,
			parents, &info->w_commit, NULL, NULL)) {
		if (!quiet)
			printf_ln("Cannot record working tree state");
		ret = -1;
		goto done;
	}

done:
	strbuf_release(&commit_tree_label);
	strbuf_release(&msg);
	strbuf_release(&final_stash_msg);
	return ret;
}

static int create_stash(int argc, const char **argv, const char *prefix)
{
	int i;
	int include_untracked = 0;
	int ret = 0;
	struct stash_info info;
	struct strbuf stash_msg_buf = STRBUF_INIT;
	const char *stash_msg;
	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_create_usage,
			     0);

	for (i = 0; i < argc; ++i)
		strbuf_addf(&stash_msg_buf, "%s ", argv[i]);

	stash_msg = strbuf_detach(&stash_msg_buf, NULL);

	ret = do_create_stash(0, NULL, prefix, &stash_msg,
			      include_untracked, 0, &info, 0);

	if (!ret)
		printf_ln("%s", oid_to_hex(&info.w_commit));

	/*
	 * ret can be 1 if there were no changes. In this case, we should
	 * not error out.
	 */
	return ret < 0;
}

static int do_push_stash(int argc, const char **argv, const char *prefix,
			 int keep_index, int patch_mode, int include_untracked,
			 int quiet, const char *stash_msg)
{
	int ret = 0;
	struct pathspec ps;
	struct stash_info info;
	if (patch_mode && keep_index == -1)
		keep_index = 1;

	if (patch_mode && include_untracked) {
		fprintf_ln(stderr, "Can't use --patch and --include-untracked or --all at the same time");
		return -1;
	}

	parse_pathspec(&ps, 0, PATHSPEC_PREFER_FULL, prefix, argv);

	if (read_cache() < 0)
		die(_("index file corrupt"));

	if (!include_untracked && ps.nr) {
		int i;
		char *ps_matched = xcalloc(ps.nr, 1);

		for (i = 0; i < active_nr; ++i) {
			const struct cache_entry *ce = active_cache[i];
			if (!ce_path_match(&the_index, ce, &ps, ps_matched))
				continue;
		}

		if (report_path_error(ps_matched, &ps, prefix)) {
			fprintf_ln(stderr, "Did you forget to 'git add'?");
			return -1;
		}
	}

	read_cache_preload(NULL);
	if (refresh_cache(REFRESH_QUIET))
		return -1;

	if (!check_changes(argv, include_untracked, prefix)) {
		if (!quiet)
			fprintf_ln(stdout, "No local changes to save");
		return 0;
	}

	if (!reflog_exists(ref_stash) && do_clear_stash()) {
		if (!quiet)
			fprintf_ln(stderr, "Cannot initialize stash");
		return -1;
	}

	if ((ret = do_create_stash(argc, argv, prefix, &stash_msg,
				   include_untracked, patch_mode, &info,
				   quiet)))
		return ret;

	if (do_store_stash(oid_to_hex(&info.w_commit), stash_msg, 1)) {
		if (!quiet)
			fprintf_ln(stderr, "Cannot save the current status");
		return -1;
	}

	if (!quiet)
		fprintf(stdout, "Saved working directory and index state %s",
			stash_msg);

	if (!patch_mode) {
		if (include_untracked && ps.nr == 0) {
			struct child_process cp = CHILD_PROCESS_INIT;

			cp.git_cmd = 1;
			argv_array_pushl(&cp.args, "clean", "--force",
					 "--quiet", "-d", NULL);
			if (include_untracked == 2)
				argv_array_push(&cp.args, "-x");
			if (run_command(&cp))
				return -1;
		}
		if (argc != 0) {
			int i;
			struct child_process cp1 = CHILD_PROCESS_INIT;
			struct child_process cp2 = CHILD_PROCESS_INIT;
			struct strbuf out = STRBUF_INIT;

			cp1.git_cmd = 1;
			argv_array_push(&cp1.args, "add");
			if (!include_untracked)
				argv_array_push(&cp1.args, "-u");
			if (include_untracked == 2)
				argv_array_push(&cp1.args, "--force");
			argv_array_push(&cp1.args, "--");
			for (i = 0; i < ps.nr; ++i)
				argv_array_push(&cp1.args, ps.items[i].match);
			if (run_command(&cp1))
				return -1;

			cp2.git_cmd = 1;
			argv_array_pushl(&cp2.args, "diff-index", "-p",
					 "--cached", "--binary", "HEAD", "--",
					 NULL);
			for (i = 0; i < ps.nr; ++i)
				argv_array_push(&cp2.args, ps.items[i].match);
			if (pipe_command(&cp2, NULL, 0, &out, 0, NULL, 0))
				return -1;

			discard_cache();
			read_cache();
			if (apply_patch_from_buf(&out, 0, 1, 1))
				return -1;
		} else {
			struct child_process cp = CHILD_PROCESS_INIT;
			cp.git_cmd = 1;
			argv_array_pushl(&cp.args, "reset", "--hard", "-q",
					 NULL);
			if (run_command(&cp))
				return -1;
		}

		if (keep_index == 1 && !is_null_oid(&info.i_tree)) {
			int i;
			struct child_process cp1 = CHILD_PROCESS_INIT;
			struct child_process cp2 = CHILD_PROCESS_INIT;
			struct strbuf out = STRBUF_INIT;

			if (reset_tree(&info.i_tree, 0, 1))
				return -1;

			cp1.git_cmd = 1;
			argv_array_pushl(&cp1.args, "ls-files", "-z",
					 "--modified", "--", NULL);
			for (i = 0; i < ps.nr; ++i)
				argv_array_push(&cp1.args, ps.items[i].match);
			if (pipe_command(&cp1, NULL, 0, &out, 0, NULL, 0))
				return -1;

			cp2.git_cmd = 1;
			argv_array_pushl(&cp2.args, "checkout-index", "-z",
					 "--force", "--stdin", NULL);
			if (pipe_command(&cp2, out.buf, out.len, NULL, 0, NULL,
					 0))
				return -1;
		}
	} else {
		if (apply_patch_from_buf(&patch, 0, 1, 0)) {
			if (!quiet)
				fprintf_ln(stderr, "Cannot remove worktree changes");
			return -1;
		}

		if (keep_index < 1) {
			int i;
			struct child_process cp = CHILD_PROCESS_INIT;

			cp.git_cmd = 1;
			argv_array_pushl(&cp.args, "reset", "-q", "--", NULL);
			for (i = 0; i < ps.nr; ++i)
				argv_array_push(&cp.args, ps.items[i].match);
			if (run_command(&cp))
				return -1;
		}
	}
	return 0;
}

static int push_stash(int argc, const char **argv, const char *prefix)
{
	int keep_index = -1;
	int patch_mode = 0;
	int include_untracked = 0;
	int quiet = 0;
	const char *stash_msg = NULL;
	struct option options[] = {
		OPT_SET_INT('k', "keep-index", &keep_index,
			N_("keep index"), 1),
		OPT_BOOL('p', "patch", &patch_mode,
			N_("stash in patch mode")),
		OPT_BOOL('q', "quiet", &quiet,
			N_("quiet mode")),
		OPT_BOOL('u', "include-untracked", &include_untracked,
			 N_("include untracked files in stash")),
		OPT_SET_INT('a', "all", &include_untracked,
			    N_("include ignore files"), 2),
		OPT_STRING('m', "message", &stash_msg, N_("message"),
			 N_("stash message")),
		OPT_END()
	};

	if (argc)
		argc = parse_options(argc, argv, prefix, options,
				     git_stash_push_usage,
				     0);

	return do_push_stash(argc, argv, prefix, keep_index, patch_mode,
			     include_untracked, quiet, stash_msg);
}

static int save_stash(int argc, const char **argv, const char *prefix)
{
	int i;
	int keep_index = -1;
	int patch_mode = 0;
	int include_untracked = 0;
	int quiet = 0;
	char *stash_msg = NULL;
	struct strbuf alt_stash_msg = STRBUF_INIT;
	struct option options[] = {
		OPT_SET_INT('k', "keep-index", &keep_index,
			N_("keep index"), 1),
		OPT_BOOL('p', "patch", &patch_mode,
			N_("stash in patch mode")),
		OPT_BOOL('q', "quiet", &quiet,
			N_("quiet mode")),
		OPT_BOOL('u', "include-untracked", &include_untracked,
			 N_("include untracked files in stash")),
		OPT_SET_INT('a', "all", &include_untracked,
			    N_("include ignore files"), 2),
		OPT_STRING('m', "message", &stash_msg, N_("message"),
			 N_("stash message")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_save_usage,
			     0);

	for (i = 0; i < argc; ++i)
		strbuf_addf(&alt_stash_msg, "%s ", argv[i]);

	stash_msg = strbuf_detach(&alt_stash_msg, NULL);

	return do_push_stash(0, NULL, prefix, keep_index, patch_mode,
			     include_untracked, quiet, stash_msg);
}

int cmd_stash(int argc, const char **argv, const char *prefix)
{
	pid_t pid = getpid();
	const char *index_file;

	struct option options[] = {
		OPT_END()
	};

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, options, git_stash_usage,
			     PARSE_OPT_KEEP_UNKNOWN | PARSE_OPT_KEEP_DASHDASH);

	index_file = get_index_file();
	strbuf_addf(&stash_index_path, "%s.stash.%" PRIuMAX, index_file,
		    (uintmax_t)pid);

	if (argc == 0)
		return !!push_stash(0, NULL, prefix);
	else if (!strcmp(argv[0], "apply"))
		return !!apply_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "clear"))
		return !!clear_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "drop"))
		return !!drop_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "pop"))
		return !!pop_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "branch"))
		return !!branch_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "list"))
		return !!list_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "show"))
		return !!show_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "store"))
		return !!store_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "create"))
		return !!create_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "push"))
		return !!push_stash(argc, argv, prefix);
	else if (!strcmp(argv[0], "save"))
		return !!save_stash(argc, argv, prefix);
	if (*argv[0] == '-') {
		struct argv_array args = ARGV_ARRAY_INIT;
		argv_array_push(&args, "push");
		argv_array_pushv(&args, argv);
		return !!push_stash(args.argc, args.argv, prefix);
	}

	usage_msg_opt(xstrfmt(_("unknown subcommand: %s"), argv[0]),
		      git_stash_usage, options);
}
