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

static const char * const git_stash_usage[] = {
	N_("git stash list [<options>]"),
	N_("git stash show [<options>] [<stash>]"),
	N_("git stash drop [-q|--quiet] [<stash>]"),
	N_("git stash ( pop | apply ) [--index] [-q|--quiet] [<stash>]"),
	N_("git stash branch <branchname> [<stash>]"),
	N_("git stash clear"),
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

static void assert_stash_like(struct stash_info *info, const char *revision)
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

	if (argc)
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

	/*
	 * Diff-tree would not be very hard to replace with a native function,
	 * however it should be done together with apply_cached.
	 */
	cp.git_cmd = 1;
	argv_array_pushl(&cp.args, "diff-tree", "--binary", NULL);
	argv_array_pushf(&cp.args, "%s^2^..%s^2", w_commit_hex, w_commit_hex);

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
	argv_array_pushl(&cp.args, "apply", "--cached", NULL);
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
		strbuf_addch(data, 0);
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

			ret = apply_cached(&out);
			strbuf_release(&out);
			if (ret)
				return -1;

			discard_cache();
			read_cache();
			if (write_cache_as_tree(&index_tree, 0, NULL))
				return -1;

			reset_head();
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
	if (ret) {
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
			printf_ln(_("Dropped %s (%s)"), info->revision.buf,
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

	if (!argc)
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
	if (!strcmp(var, "stash.showstat")) {
		show_stat = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "stash.showpatch")) {
		show_patch = git_config_bool(var, value);
		return 0;
	}
	return git_default_config(var, value, cb);
}

static int show_stash(int argc, const char **argv, const char *prefix)
{
	int i;
	int opts = 0;
	int ret = 0;
	struct stash_info info;
	struct rev_info rev;
	struct argv_array stash_args = ARGV_ARRAY_INIT;
	struct option options[] = {
		OPT_END()
	};

	init_diff_ui_defaults();
	git_config(git_diff_ui_config, NULL);
	init_revisions(&rev, prefix);

	for (i = 1; i < argc; ++i) {
		if (argv[i][0] != '-')
			argv_array_push(&stash_args, argv[i]);
		else
			opts++;
	}

	ret = get_stash_info(&info, stash_args.argc, stash_args.argv);
	argv_array_clear(&stash_args);
	if (ret)
		return -1;

	/*
	 * The config settings are applied only if there are not passed
	 * any options.
	 */
	if (!opts) {
		git_config(git_stash_config, NULL);
		if (show_stat)
			rev.diffopt.output_format = DIFF_FORMAT_DIFFSTAT;

		if (show_patch)
			rev.diffopt.output_format |= DIFF_FORMAT_PATCH;

		if (!show_stat && !show_patch) {
			free_stash_info(&info);
			return 0;
		}
	}

	argc = setup_revisions(argc, argv, &rev, NULL);
	if (argc > 1) {
		free_stash_info(&info);
		usage_with_options(git_stash_show_usage, options);
	}

	rev.diffopt.flags.recursive = 1;
	setup_diff_pager(&rev.diffopt);
	diff_tree_oid(&info.b_commit, &info.w_commit, "", &rev.diffopt);
	log_tree_diff_flush(&rev);

	free_stash_info(&info);
	return diff_result_code(&rev.diffopt, 0);
}

static int do_store_stash(const char *w_commit, const char *stash_msg,
			  int quiet)
{
	int ret = 0;
	int need_to_free = 0;
	struct object_id obj;

	if (!stash_msg) {
		need_to_free = 1;
		stash_msg  = xstrdup("Created via \"git stash store\".");
	}

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
	if (need_to_free)
		free((char *) stash_msg);
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
		fprintf_ln(stderr, _("\"git stash store\" requires one <commit> argument"));
		return -1;
	}

	return do_store_stash(argv[0], stash_msg, quiet);
}

/*
 * `untracked_files` will be filled with the names of untracked files.
 * The return value is:
 *
 * = 0 if there are not any untracked files
 * > 0 if there are untracked files
 */
static struct strbuf untracked_files = STRBUF_INIT;

static int get_untracked_files(struct pathspec ps, int include_untracked)
{
	int max_len;
	int i;
	char *seen;
	struct dir_struct dir;

	memset(&dir, 0, sizeof(dir));
	if (include_untracked != 2)
		setup_standard_excludes(&dir);

	seen = xcalloc(ps.nr, 1);

	max_len = fill_directory(&dir, the_repository->index, &ps);
	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];
		if (!dir_path_match(&the_index, ent, &ps, max_len, seen)) {
			free(ent);
			continue;
		}
		strbuf_addf(&untracked_files, "%s%c", ent->name, '\0');
		free(ent);
	}

	free(dir.entries);
	free(dir.ignored);
	clear_directory(&dir);
	free(seen);
	return untracked_files.len;
}

/*
 * The return value of `check_changes_tracked_files()` can be:
 *
 * < 0 if there was an error
 * = 0 if there are no changes.
 * > 0 if there are changes.
 */

static int check_changes_tracked_files(struct pathspec ps)
{
	int result;
	struct rev_info rev;
	struct object_id dummy;

	init_revisions(&rev, NULL);
	rev.prune_data = ps;

	rev.diffopt.flags.quick = 1;
	rev.diffopt.flags.ignore_submodules = 1;
	rev.abbrev = 0;

	/* No initial commit. */
	if (get_oid("HEAD", &dummy))
		return -1;

	add_head_to_pending(&rev);
	diff_setup_done(&rev.diffopt);

	if (read_cache() < 0)
		return 1;
	result = run_diff_index(&rev, 1);
	if (diff_result_code(&rev.diffopt, result))
		return 1;

	object_array_clear(&rev.pending);
	result = run_diff_files(&rev, 0);
	if (diff_result_code(&rev.diffopt, result))
		return 1;

	return 0;
}

static int check_changes(struct pathspec ps, int include_untracked)
{
	int ret = 0;
	if (check_changes_tracked_files(ps))
		ret = 1;

	if (include_untracked && get_untracked_files(ps, include_untracked))
		ret = 1;

	return ret;
}

static int save_untracked_files(struct stash_info *info, struct strbuf *msg)
{
	int ret = 0;
	struct strbuf untracked_msg = STRBUF_INIT;
	struct child_process cp_upd_index = CHILD_PROCESS_INIT;
	struct index_state istate = { NULL };

	cp_upd_index.git_cmd = 1;
	argv_array_pushl(&cp_upd_index.args, "update-index", "-z", "--add",
			 "--remove", "--stdin", NULL);
	argv_array_pushf(&cp_upd_index.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);

	strbuf_addf(&untracked_msg, "untracked files on %s\n", msg->buf);
	if (pipe_command(&cp_upd_index, untracked_files.buf, untracked_files.len,
			 NULL, 0, NULL, 0)) {
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
	discard_index(&istate);
	strbuf_release(&untracked_msg);
	remove_path(stash_index_path.buf);
	return ret;
}

static struct strbuf patch = STRBUF_INIT;

static int stash_patch(struct stash_info *info, struct pathspec ps, int quiet)
{
	int i;
	int ret = 0;
	struct child_process cp_read_tree = CHILD_PROCESS_INIT;
	struct child_process cp_add_i = CHILD_PROCESS_INIT;
	struct child_process cp_diff_tree = CHILD_PROCESS_INIT;
	struct index_state istate = { NULL };

	remove_path(stash_index_path.buf);

	cp_read_tree.git_cmd = 1;
	argv_array_pushl(&cp_read_tree.args, "read-tree", "HEAD", NULL);
	argv_array_pushf(&cp_read_tree.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);
	if (run_command(&cp_read_tree)) {
		ret = -1;
		goto done;
	}

	cp_add_i.git_cmd = 1;
	argv_array_pushl(&cp_add_i.args, "add--interactive", "--patch=stash",
			"--", NULL);
	for (i = 0; i < ps.nr; ++i)
		argv_array_push(&cp_add_i.args, ps.items[i].match);
	argv_array_pushf(&cp_add_i.env_array, "GIT_INDEX_FILE=%s",
			 stash_index_path.buf);
	if (run_command(&cp_add_i)) {
		ret = -1;
		goto done;
	}

	if (write_index_as_tree(&info->w_tree, &istate, stash_index_path.buf, 0,
				NULL)) {
		ret = -1;
		goto done;
	}

	cp_diff_tree.git_cmd = 1;
	argv_array_pushl(&cp_diff_tree.args, "diff-tree", "-p", "HEAD",
			 oid_to_hex(&info->w_tree), "--", NULL);
	if (pipe_command(&cp_diff_tree, NULL, 0, &patch, 0, NULL, 0)) {
		ret = -1;
		goto done;
	}

	if (!patch.len) {
		if (!quiet)
			fprintf_ln(stderr, _("No changes selected"));
		ret = 1;
	}

done:
	discard_index(&istate);
	remove_path(stash_index_path.buf);
	return ret;
}

static int stash_working_tree(struct stash_info *info, struct pathspec ps)
{
	int ret = 0;
	struct child_process cp_upd_index = CHILD_PROCESS_INIT;
	struct strbuf diff_output = STRBUF_INIT;
	struct rev_info rev;
	struct index_state istate = { NULL };

	set_alternate_index_output(stash_index_path.buf);
	if (reset_tree(&info->i_tree, 0, 0)) {
		ret = -1;
		goto done;
	}
	set_alternate_index_output(NULL);

	git_config(git_diff_basic_config, NULL);
	init_revisions(&rev, NULL);
	rev.prune_data = ps;
	rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
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

	cp_upd_index.git_cmd = 1;
	argv_array_pushl(&cp_upd_index.args, "update-index", "-z", "--add",
			 "--remove", "--stdin", NULL);
	argv_array_pushf(&cp_upd_index.env_array, "GIT_INDEX_FILE=%s",
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
	discard_index(&istate);
	UNLEAK(rev);
	object_array_clear(&rev.pending);
	strbuf_release(&diff_output);
	remove_path(stash_index_path.buf);
	return ret;
}

static int do_create_stash(struct pathspec ps, const char **stash_msg,
			   int include_untracked, int patch_mode,
			   struct stash_info *info, int quiet)
{
	int untracked_commit_option = 0;
	int ret = 0;
	int flags;
	const char *head_short_sha1 = NULL;
	const char *branch_ref = NULL;
	const char *branch_name = "(no branch)";
	struct commit *head_commit = NULL;
	struct commit_list *parents = NULL;
	struct strbuf msg = STRBUF_INIT;
	struct strbuf commit_tree_label = STRBUF_INIT;
	struct strbuf stash_msg_buf = STRBUF_INIT;

	read_cache_preload(NULL);
	refresh_cache(REFRESH_QUIET);

	if (get_oid("HEAD", &info->b_commit)) {
		if (!quiet)
			fprintf_ln(stderr, _("You do not have the initial commit yet"));
		ret = -1;
		*stash_msg = NULL;
		goto done;
	} else {
		head_commit = lookup_commit(the_repository, &info->b_commit);
	}

	branch_ref = resolve_ref_unsafe("HEAD", 0, NULL, &flags);
	if (flags & REF_ISSYMREF)
		branch_name = strrchr(branch_ref, '/') + 1;
	head_short_sha1 = find_unique_abbrev(&head_commit->object.oid,
					     DEFAULT_ABBREV);
	strbuf_addf(&msg, "%s: %s ", branch_name, head_short_sha1);
	pp_commit_easy(CMIT_FMT_ONELINE, head_commit, &msg);

	strbuf_addf(&commit_tree_label, "index on %s\n", msg.buf);
	commit_list_insert(head_commit, &parents);
	if (write_cache_as_tree(&info->i_tree, 0, NULL) ||
	    commit_tree(commit_tree_label.buf, commit_tree_label.len,
			&info->i_tree, parents, &info->i_commit, NULL, NULL)) {
		if (!quiet)
			fprintf_ln(stderr, _("Cannot save the current index state"));
		ret = -1;
		*stash_msg = NULL;
		goto done;
	}

	if (include_untracked) {
		if (save_untracked_files(info, &msg)) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot save the untracked files"));
			ret = -1;
			*stash_msg = NULL;
			goto done;
		}
		untracked_commit_option = 1;
	}
	if (patch_mode) {
		ret = stash_patch(info, ps, quiet);
		*stash_msg = NULL;
		if (ret < 0) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot save the current worktree state"));
			goto done;
		} else if (ret > 0) {
			goto done;
		}
	} else {
		if (stash_working_tree(info, ps)) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot save the current worktree state"));
			ret = -1;
			*stash_msg = NULL;
			goto done;
		}
	}

	if (!*stash_msg || !strlen(*stash_msg))
		strbuf_addf(&stash_msg_buf, "WIP on %s", msg.buf);
	else
		strbuf_addf(&stash_msg_buf, "On %s: %s", branch_name,
			    *stash_msg);
	*stash_msg = strbuf_detach(&stash_msg_buf, NULL);

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
			fprintf_ln(stderr, _("Cannot record working tree state"));
		ret = -1;
		goto done;
	}

done:
	strbuf_release(&commit_tree_label);
	strbuf_release(&msg);
	strbuf_release(&stash_msg_buf);
	return ret;
}

static int create_stash(int argc, const char **argv, const char *prefix)
{
	int i;
	int ret = 0;
	char *to_free = NULL;
	const char *stash_msg = NULL;
	struct stash_info info;
	struct pathspec ps;
	struct strbuf stash_msg_buf = STRBUF_INIT;
	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_create_usage,
			     0);

	memset(&ps, 0, sizeof(ps));
	if (!check_changes_tracked_files(ps))
		return 0;

	for (i = 0; i < argc; ++i)
		strbuf_addf(&stash_msg_buf, "%s ", argv[i]);
	stash_msg = strbuf_detach(&stash_msg_buf, NULL);
	to_free = (char *) stash_msg;

	if (!(ret = do_create_stash(ps, &stash_msg, 0, 0, &info, 0)))
		printf_ln("%s", oid_to_hex(&info.w_commit));

	free(to_free);
	free((char *) stash_msg);
	return ret;
}

static void add_ps_items_to_argv_array(struct argv_array *args,
				       struct pathspec ps) {
	int i;
	for (i = 0; i < ps.nr; ++i)
		argv_array_push(args, ps.items[i].match);
}

static int do_push_stash(struct pathspec ps, const char *stash_msg, int quiet,
			 int keep_index, int patch_mode, int include_untracked)
{
	int ret = 0;
	struct stash_info info;
	if (patch_mode && keep_index == -1)
		keep_index = 1;

	if (patch_mode && include_untracked) {
		fprintf_ln(stderr, _("Can't use --patch and --include-untracked or --all at the same time"));
		return -1;
	}

	read_cache_preload(NULL);
	if (!include_untracked && ps.nr) {
		int i;
		char *ps_matched = xcalloc(ps.nr, 1);

		for (i = 0; i < active_nr; ++i) {
			const struct cache_entry *ce = active_cache[i];
			ce_path_match(&the_index, ce, &ps, ps_matched);
		}

		if (report_path_error(ps_matched, &ps, NULL)) {
			fprintf_ln(stderr, _("Did you forget to 'git add'?"));
			return -1;
		}
		free(ps_matched);
	}

	if (refresh_cache(REFRESH_QUIET))
		return -1;

	if (!check_changes(ps, include_untracked)) {
		if (!quiet)
			printf_ln(_("No local changes to save"));
		return 0;
	}

	if (!reflog_exists(ref_stash) && do_clear_stash()) {
		if (!quiet)
			fprintf_ln(stderr, _("Cannot initialize stash"));
		return -1;
	}

	if (do_create_stash(ps, &stash_msg, include_untracked, patch_mode,
			    &info, quiet)) {
		ret = -1;
		goto done;
	}

	if (do_store_stash(oid_to_hex(&info.w_commit), stash_msg, 1)) {
		if (!quiet)
			fprintf_ln(stderr, _("Cannot save the current status"));
		ret = -1;
		goto done;
	}

	if (!quiet)
		printf_ln(_("Saved working directory and index state %s"),
			stash_msg);

	if (!patch_mode) {
		if (include_untracked && !ps.nr) {
			struct child_process cp = CHILD_PROCESS_INIT;

			cp.git_cmd = 1;
			argv_array_pushl(&cp.args, "clean", "--force",
					 "--quiet", "-d", NULL);
			if (include_untracked == 2)
				argv_array_push(&cp.args, "-x");
			if (run_command(&cp)) {
				ret = -1;
				goto done;
			}
		}
		if (ps.nr) {
			struct child_process cp1 = CHILD_PROCESS_INIT;
			struct child_process cp2 = CHILD_PROCESS_INIT;
			struct child_process cp3 = CHILD_PROCESS_INIT;
			struct strbuf out = STRBUF_INIT;

			cp1.git_cmd = 1;
			argv_array_push(&cp1.args, "add");
			if (!include_untracked)
				argv_array_push(&cp1.args, "-u");
			if (include_untracked == 2)
				argv_array_push(&cp1.args, "--force");
			argv_array_push(&cp1.args, "--");
			add_ps_items_to_argv_array(&cp1.args, ps);
			if (run_command(&cp1)) {
				ret = -1;
				goto done;
			}

			cp2.git_cmd = 1;
			argv_array_pushl(&cp2.args, "diff-index", "-p",
					 "--cached", "--binary", "HEAD", "--",
					 NULL);
			add_ps_items_to_argv_array(&cp2.args, ps);
			if (pipe_command(&cp2, NULL, 0, &out, 0, NULL, 0)) {
				ret = -1;
				goto done;
			}

			cp3.git_cmd = 1;
			argv_array_pushl(&cp3.args, "apply", "--index", "-R",
					 NULL);
			if (pipe_command(&cp3, out.buf, out.len, NULL, 0, NULL,
					 0)) {
				ret = -1;
				goto done;
			}
		} else {
			struct child_process cp = CHILD_PROCESS_INIT;
			cp.git_cmd = 1;
			argv_array_pushl(&cp.args, "reset", "--hard", "-q",
					 NULL);
			if (run_command(&cp)) {
				ret = -1;
				goto done;
			}
		}

		if (keep_index == 1 && !is_null_oid(&info.i_tree)) {
			struct child_process cp1 = CHILD_PROCESS_INIT;
			struct child_process cp2 = CHILD_PROCESS_INIT;
			struct strbuf out = STRBUF_INIT;

			if (reset_tree(&info.i_tree, 0, 1)) {
				ret = -1;
				goto done;
			}

			cp1.git_cmd = 1;
			argv_array_pushl(&cp1.args, "ls-files", "-z",
					 "--modified", "--", NULL);
			add_ps_items_to_argv_array(&cp1.args, ps);
			if (pipe_command(&cp1, NULL, 0, &out, 0, NULL, 0)) {
				ret = -1;
				goto done;
			}

			cp2.git_cmd = 1;
			argv_array_pushl(&cp2.args, "checkout-index", "-z",
					 "--force", "--stdin", NULL);
			if (pipe_command(&cp2, out.buf, out.len, NULL, 0, NULL,
					 0)) {
				ret = -1;
				goto done;
			}
		}
	} else {
		struct child_process cp = CHILD_PROCESS_INIT;

		cp.git_cmd = 1;
		argv_array_pushl(&cp.args, "apply", "-R", NULL);

		if (pipe_command(&cp, patch.buf, patch.len, NULL, 0, NULL, 0)) {
			if (!quiet)
				fprintf_ln(stderr, _("Cannot remove worktree changes"));
			ret = -1;
			goto done;
		}

		if (keep_index < 1) {
			int i;
			struct child_process cp = CHILD_PROCESS_INIT;

			cp.git_cmd = 1;
			argv_array_pushl(&cp.args, "reset", "-q", "--", NULL);
			for (i = 0; i < ps.nr; ++i)
				argv_array_push(&cp.args, ps.items[i].match);
			if (run_command(&cp)) {
				ret = -1;
				goto done;
			}
		}
	}
done:
	free((char *) stash_msg);
	return ret;
}

static int push_stash(int argc, const char **argv, const char *prefix)
{
	int keep_index = -1;
	int patch_mode = 0;
	int include_untracked = 0;
	int quiet = 0;
	const char *stash_msg = NULL;
	struct pathspec ps;
	struct option options[] = {
		OPT_SET_INT('k', "keep-index", &keep_index,
			N_("keep index"), 1),
		OPT_BOOL('p', "patch", &patch_mode,
			N_("stash in patch mode")),
		OPT__QUIET(&quiet, N_("quiet mode")),
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

	parse_pathspec(&ps, 0, PATHSPEC_PREFER_FULL, prefix, argv);
	return do_push_stash(ps, stash_msg, quiet, keep_index, patch_mode,
			     include_untracked);
}

static int save_stash(int argc, const char **argv, const char *prefix)
{
	int i;
	int keep_index = -1;
	int patch_mode = 0;
	int include_untracked = 0;
	int quiet = 0;
	int ret = 0;
	const char *stash_msg = NULL;
	char *to_free = NULL;
	struct strbuf stash_msg_buf = STRBUF_INIT;
	struct pathspec ps;
	struct option options[] = {
		OPT_SET_INT('k', "keep-index", &keep_index,
			N_("keep index"), 1),
		OPT_BOOL('p', "patch", &patch_mode,
			N_("stash in patch mode")),
		OPT__QUIET(&quiet, N_("quiet mode")),
		OPT_BOOL('u', "include-untracked", &include_untracked,
			 N_("include untracked files in stash")),
		OPT_SET_INT('a', "all", &include_untracked,
			    N_("include ignore files"), 2),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_stash_save_usage,
			     0);

	for (i = 0; i < argc; ++i)
		strbuf_addf(&stash_msg_buf, "%s ", argv[i]);
	stash_msg = strbuf_detach(&stash_msg_buf, NULL);
	to_free = (char *) stash_msg;

	memset(&ps, 0, sizeof(ps));
	ret = do_push_stash(ps, stash_msg, quiet, keep_index, patch_mode,
			    include_untracked);

	free(to_free);
	return ret;
}

int cmd_stash(int argc, const char **argv, const char *prefix)
{
	int i = -1;
	pid_t pid = getpid();
	const char *index_file;
	struct argv_array args = ARGV_ARRAY_INIT;

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
	else if (*argv[0] != '-')
		usage_msg_opt(xstrfmt(_("unknown subcommand: %s"), argv[0]),
			      git_stash_usage, options);

	if (strcmp(argv[0], "-p")) {
		while (++i < argc && strcmp(argv[i], "--")) {
			/*
			 * `akpqu` is a string which contains all short options,
			 * except `-m` which is verified separately.
			 */
			if ((strlen(argv[i]) == 2) && *argv[i] == '-' &&
			    strchr("akpqu", argv[i][1]))
				continue;

			if (!strcmp(argv[i], "--all") ||
			    !strcmp(argv[i], "--keep-index") ||
			    !strcmp(argv[i], "--no-keep-index") ||
			    !strcmp(argv[i], "--patch") ||
			    !strcmp(argv[i], "--quiet") ||
			    !strcmp(argv[i], "--include-untracked"))
				continue;

			/*
			 * `-m` and `--message=` are verified separately because
			 * they need to be immediately followed by a string
			 * (i.e.`-m"foobar"` or `--message="foobar"`).
			 */
			if ((strlen(argv[i]) > 2 &&
			     !strncmp(argv[i], "-m", 2)) ||
			    (strlen(argv[i]) > 10 &&
			     !strncmp(argv[i], "--message=", 10)))
				continue;

			usage_with_options(git_stash_usage, options);
		}
	}

	argv_array_push(&args, "push");
	argv_array_pushv(&args, argv);
	return !!push_stash(args.argc, args.argv, prefix);
}
