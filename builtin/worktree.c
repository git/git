#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "abspath.h"
#include "advice.h"
#include "checkout.h"
#include "config.h"
#include "copy.h"
#include "dir.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "object-file.h"
#include "object-name.h"
#include "parse-options.h"
#include "path.h"
#include "strvec.h"
#include "branch.h"
#include "read-cache-ll.h"
#include "refs.h"
#include "remote.h"
#include "run-command.h"
#include "hook.h"
#include "sigchain.h"
#include "submodule.h"
#include "utf8.h"
#include "worktree.h"
#include "quote.h"

#define BUILTIN_WORKTREE_ADD_USAGE \
	N_("git worktree add [-f] [--detach] [--checkout] [--lock [--reason <string>]]\n" \
	   "                 [--orphan] [(-b | -B) <new-branch>] <path> [<commit-ish>]")

#define BUILTIN_WORKTREE_LIST_USAGE \
	N_("git worktree list [-v | --porcelain [-z]]")
#define BUILTIN_WORKTREE_LOCK_USAGE \
	N_("git worktree lock [--reason <string>] <worktree>")
#define BUILTIN_WORKTREE_MOVE_USAGE \
	N_("git worktree move <worktree> <new-path>")
#define BUILTIN_WORKTREE_PRUNE_USAGE \
	N_("git worktree prune [-n] [-v] [--expire <expire>]")
#define BUILTIN_WORKTREE_REMOVE_USAGE \
	N_("git worktree remove [-f] <worktree>")
#define BUILTIN_WORKTREE_REPAIR_USAGE \
	N_("git worktree repair [<path>...]")
#define BUILTIN_WORKTREE_UNLOCK_USAGE \
	N_("git worktree unlock <worktree>")

#define WORKTREE_ADD_DWIM_ORPHAN_INFER_TEXT \
	_("No possible source branch, inferring '--orphan'")

#define WORKTREE_ADD_ORPHAN_WITH_DASH_B_HINT_TEXT \
	_("If you meant to create a worktree containing a new unborn branch\n" \
	"(branch with no commits) for this repository, you can do so\n" \
	"using the --orphan flag:\n" \
	"\n" \
	"    git worktree add --orphan -b %s %s\n")

#define WORKTREE_ADD_ORPHAN_NO_DASH_B_HINT_TEXT \
	_("If you meant to create a worktree containing a new unborn branch\n" \
	"(branch with no commits) for this repository, you can do so\n" \
	"using the --orphan flag:\n" \
	"\n" \
	"    git worktree add --orphan %s\n")

static const char * const git_worktree_usage[] = {
	BUILTIN_WORKTREE_ADD_USAGE,
	BUILTIN_WORKTREE_LIST_USAGE,
	BUILTIN_WORKTREE_LOCK_USAGE,
	BUILTIN_WORKTREE_MOVE_USAGE,
	BUILTIN_WORKTREE_PRUNE_USAGE,
	BUILTIN_WORKTREE_REMOVE_USAGE,
	BUILTIN_WORKTREE_REPAIR_USAGE,
	BUILTIN_WORKTREE_UNLOCK_USAGE,
	NULL
};

static const char * const git_worktree_add_usage[] = {
	BUILTIN_WORKTREE_ADD_USAGE,
	NULL,
};

static const char * const git_worktree_list_usage[] = {
	BUILTIN_WORKTREE_LIST_USAGE,
	NULL
};

static const char * const git_worktree_lock_usage[] = {
	BUILTIN_WORKTREE_LOCK_USAGE,
	NULL
};

static const char * const git_worktree_move_usage[] = {
	BUILTIN_WORKTREE_MOVE_USAGE,
	NULL
};

static const char * const git_worktree_prune_usage[] = {
	BUILTIN_WORKTREE_PRUNE_USAGE,
	NULL
};

static const char * const git_worktree_remove_usage[] = {
	BUILTIN_WORKTREE_REMOVE_USAGE,
	NULL
};

static const char * const git_worktree_repair_usage[] = {
	BUILTIN_WORKTREE_REPAIR_USAGE,
	NULL
};

static const char * const git_worktree_unlock_usage[] = {
	BUILTIN_WORKTREE_UNLOCK_USAGE,
	NULL
};

struct add_opts {
	int force;
	int detach;
	int quiet;
	int checkout;
	int orphan;
	const char *keep_locked;
};

static int show_only;
static int verbose;
static int guess_remote;
static timestamp_t expire;

static int git_worktree_config(const char *var, const char *value,
			       const struct config_context *ctx, void *cb)
{
	if (!strcmp(var, "worktree.guessremote")) {
		guess_remote = git_config_bool(var, value);
		return 0;
	}

	return git_default_config(var, value, ctx, cb);
}

static int delete_git_dir(const char *id)
{
	struct strbuf sb = STRBUF_INIT;
	int ret;

	strbuf_addstr(&sb, git_common_path("worktrees/%s", id));
	ret = remove_dir_recursively(&sb, 0);
	if (ret < 0 && errno == ENOTDIR)
		ret = unlink(sb.buf);
	if (ret)
		error_errno(_("failed to delete '%s'"), sb.buf);
	strbuf_release(&sb);
	return ret;
}

static void delete_worktrees_dir_if_empty(void)
{
	rmdir(git_path("worktrees")); /* ignore failed removal */
}

static void prune_worktree(const char *id, const char *reason)
{
	if (show_only || verbose)
		fprintf_ln(stderr, _("Removing %s/%s: %s"), "worktrees", id, reason);
	if (!show_only)
		delete_git_dir(id);
}

static int prune_cmp(const void *a, const void *b)
{
	const struct string_list_item *x = a;
	const struct string_list_item *y = b;
	int c;

	if ((c = fspathcmp(x->string, y->string)))
	    return c;
	/*
	 * paths same; prune_dupes() removes all but the first worktree entry
	 * having the same path, so sort main worktree ('util' is NULL) above
	 * linked worktrees ('util' not NULL) since main worktree can't be
	 * removed
	 */
	if (!x->util)
		return -1;
	if (!y->util)
		return 1;
	/* paths same; sort by .git/worktrees/<id> */
	return strcmp(x->util, y->util);
}

static void prune_dups(struct string_list *l)
{
	int i;

	QSORT(l->items, l->nr, prune_cmp);
	for (i = 1; i < l->nr; i++) {
		if (!fspathcmp(l->items[i].string, l->items[i - 1].string))
			prune_worktree(l->items[i].util, "duplicate entry");
	}
}

static void prune_worktrees(void)
{
	struct strbuf reason = STRBUF_INIT;
	struct strbuf main_path = STRBUF_INIT;
	struct string_list kept = STRING_LIST_INIT_DUP;
	DIR *dir = opendir(git_path("worktrees"));
	struct dirent *d;
	if (!dir)
		return;
	while ((d = readdir_skip_dot_and_dotdot(dir)) != NULL) {
		char *path;
		strbuf_reset(&reason);
		if (should_prune_worktree(d->d_name, &reason, &path, expire))
			prune_worktree(d->d_name, reason.buf);
		else if (path)
			string_list_append_nodup(&kept, path)->util = xstrdup(d->d_name);
	}
	closedir(dir);

	strbuf_add_absolute_path(&main_path, repo_get_common_dir(the_repository));
	/* massage main worktree absolute path to match 'gitdir' content */
	strbuf_strip_suffix(&main_path, "/.");
	string_list_append_nodup(&kept, strbuf_detach(&main_path, NULL));
	prune_dups(&kept);
	string_list_clear(&kept, 1);

	if (!show_only)
		delete_worktrees_dir_if_empty();
	strbuf_release(&reason);
}

static int prune(int ac, const char **av, const char *prefix,
		 struct repository *repo UNUSED)
{
	struct option options[] = {
		OPT__DRY_RUN(&show_only, N_("do not remove, show only")),
		OPT__VERBOSE(&verbose, N_("report pruned working trees")),
		OPT_EXPIRY_DATE(0, "expire", &expire,
				N_("expire working trees older than <time>")),
		OPT_END()
	};

	expire = TIME_MAX;
	ac = parse_options(ac, av, prefix, options, git_worktree_prune_usage,
			   0);
	if (ac)
		usage_with_options(git_worktree_prune_usage, options);
	prune_worktrees();
	return 0;
}

static char *junk_work_tree;
static char *junk_git_dir;
static int is_junk;
static pid_t junk_pid;

static void remove_junk(void)
{
	struct strbuf sb = STRBUF_INIT;
	if (!is_junk || getpid() != junk_pid)
		return;
	if (junk_git_dir) {
		strbuf_addstr(&sb, junk_git_dir);
		remove_dir_recursively(&sb, 0);
		strbuf_reset(&sb);
	}
	if (junk_work_tree) {
		strbuf_addstr(&sb, junk_work_tree);
		remove_dir_recursively(&sb, 0);
	}
	strbuf_release(&sb);
}

static void remove_junk_on_signal(int signo)
{
	remove_junk();
	sigchain_pop(signo);
	raise(signo);
}

static const char *worktree_basename(const char *path, int *olen)
{
	const char *name;
	int len;

	len = strlen(path);
	while (len && is_dir_sep(path[len - 1]))
		len--;

	for (name = path + len - 1; name > path; name--)
		if (is_dir_sep(*name)) {
			name++;
			break;
		}

	*olen = len;
	return name;
}

/* check that path is viable location for worktree */
static void check_candidate_path(const char *path,
				 int force,
				 struct worktree **worktrees,
				 const char *cmd)
{
	struct worktree *wt;
	int locked;

	if (file_exists(path) && !is_empty_dir(path))
		die(_("'%s' already exists"), path);

	wt = find_worktree_by_path(worktrees, path);
	if (!wt)
		return;

	locked = !!worktree_lock_reason(wt);
	if ((!locked && force) || (locked && force > 1)) {
		if (delete_git_dir(wt->id))
		    die(_("unusable worktree destination '%s'"), path);
		return;
	}

	if (locked)
		die(_("'%s' is a missing but locked worktree;\nuse '%s -f -f' to override, or 'unlock' and 'prune' or 'remove' to clear"), path, cmd);
	else
		die(_("'%s' is a missing but already registered worktree;\nuse '%s -f' to override, or 'prune' or 'remove' to clear"), path, cmd);
}

static void copy_sparse_checkout(const char *worktree_git_dir)
{
	char *from_file = git_pathdup("info/sparse-checkout");
	char *to_file = xstrfmt("%s/info/sparse-checkout", worktree_git_dir);

	if (file_exists(from_file)) {
		if (safe_create_leading_directories(to_file) ||
			copy_file(to_file, from_file, 0666))
			error(_("failed to copy '%s' to '%s'; sparse-checkout may not work correctly"),
				from_file, to_file);
	}

	free(from_file);
	free(to_file);
}

static void copy_filtered_worktree_config(const char *worktree_git_dir)
{
	char *from_file = git_pathdup("config.worktree");
	char *to_file = xstrfmt("%s/config.worktree", worktree_git_dir);

	if (file_exists(from_file)) {
		struct config_set cs = { { 0 } };
		int bare;

		if (safe_create_leading_directories(to_file) ||
			copy_file(to_file, from_file, 0666)) {
			error(_("failed to copy worktree config from '%s' to '%s'"),
				from_file, to_file);
			goto worktree_copy_cleanup;
		}

		git_configset_init(&cs);
		git_configset_add_file(&cs, from_file);

		if (!git_configset_get_bool(&cs, "core.bare", &bare) &&
			bare &&
			git_config_set_multivar_in_file_gently(
				to_file, "core.bare", NULL, "true", NULL, 0))
			error(_("failed to unset '%s' in '%s'"),
				"core.bare", to_file);
		if (!git_configset_get(&cs, "core.worktree") &&
			git_config_set_in_file_gently(to_file,
							"core.worktree", NULL, NULL))
			error(_("failed to unset '%s' in '%s'"),
				"core.worktree", to_file);

		git_configset_clear(&cs);
	}

worktree_copy_cleanup:
	free(from_file);
	free(to_file);
}

static int checkout_worktree(const struct add_opts *opts,
			     struct strvec *child_env)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	cp.git_cmd = 1;
	strvec_pushl(&cp.args, "reset", "--hard", "--no-recurse-submodules", NULL);
	if (opts->quiet)
		strvec_push(&cp.args, "--quiet");
	strvec_pushv(&cp.env, child_env->v);
	return run_command(&cp);
}

static int make_worktree_orphan(const char * ref, const struct add_opts *opts,
				struct strvec *child_env)
{
	struct strbuf symref = STRBUF_INIT;
	struct child_process cp = CHILD_PROCESS_INIT;

	validate_new_branchname(ref, &symref, 0);
	strvec_pushl(&cp.args, "symbolic-ref", "HEAD", symref.buf, NULL);
	if (opts->quiet)
		strvec_push(&cp.args, "--quiet");
	strvec_pushv(&cp.env, child_env->v);
	strbuf_release(&symref);
	cp.git_cmd = 1;
	return run_command(&cp);
}

static int add_worktree(const char *path, const char *refname,
			const struct add_opts *opts)
{
	struct strbuf sb_git = STRBUF_INIT, sb_repo = STRBUF_INIT;
	struct strbuf sb = STRBUF_INIT, sb_tmp = STRBUF_INIT;
	struct strbuf sb_path_realpath = STRBUF_INIT, sb_repo_realpath = STRBUF_INIT;
	const char *name;
	struct strvec child_env = STRVEC_INIT;
	unsigned int counter = 0;
	int len, ret;
	struct strbuf symref = STRBUF_INIT;
	struct commit *commit = NULL;
	int is_branch = 0;
	struct strbuf sb_name = STRBUF_INIT;
	struct worktree **worktrees, *wt = NULL;
	struct ref_store *wt_refs;

	worktrees = get_worktrees();
	check_candidate_path(path, opts->force, worktrees, "add");
	free_worktrees(worktrees);
	worktrees = NULL;

	/* is 'refname' a branch or commit? */
	if (!opts->detach && !strbuf_check_branch_ref(&symref, refname) &&
	    refs_ref_exists(get_main_ref_store(the_repository), symref.buf)) {
		is_branch = 1;
		if (!opts->force)
			die_if_checked_out(symref.buf, 0);
	}
	commit = lookup_commit_reference_by_name(refname);
	if (!commit && !opts->orphan)
		die(_("invalid reference: %s"), refname);

	name = worktree_basename(path, &len);
	strbuf_add(&sb, name, path + len - name);
	sanitize_refname_component(sb.buf, &sb_name);
	if (!sb_name.len)
		BUG("How come '%s' becomes empty after sanitization?", sb.buf);
	strbuf_reset(&sb);
	name = sb_name.buf;
	git_path_buf(&sb_repo, "worktrees/%s", name);
	len = sb_repo.len;
	if (safe_create_leading_directories_const(sb_repo.buf))
		die_errno(_("could not create leading directories of '%s'"),
			  sb_repo.buf);

	while (mkdir(sb_repo.buf, 0777)) {
		counter++;
		if ((errno != EEXIST) || !counter /* overflow */)
			die_errno(_("could not create directory of '%s'"),
				  sb_repo.buf);
		strbuf_setlen(&sb_repo, len);
		strbuf_addf(&sb_repo, "%d", counter);
	}
	name = strrchr(sb_repo.buf, '/') + 1;

	junk_pid = getpid();
	atexit(remove_junk);
	sigchain_push_common(remove_junk_on_signal);

	junk_git_dir = xstrdup(sb_repo.buf);
	is_junk = 1;

	/*
	 * lock the incomplete repo so prune won't delete it, unlock
	 * after the preparation is over.
	 */
	strbuf_addf(&sb, "%s/locked", sb_repo.buf);
	if (opts->keep_locked)
		write_file(sb.buf, "%s", opts->keep_locked);
	else
		write_file(sb.buf, _("initializing"));

	strbuf_addf(&sb_git, "%s/.git", path);
	if (safe_create_leading_directories_const(sb_git.buf))
		die_errno(_("could not create leading directories of '%s'"),
			  sb_git.buf);
	junk_work_tree = xstrdup(path);

	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s/gitdir", sb_repo.buf);
	strbuf_realpath(&sb_path_realpath, path, 1);
	strbuf_realpath(&sb_repo_realpath, sb_repo.buf, 1);
	write_file(sb.buf, "%s/.git", relative_path(sb_path_realpath.buf, sb_repo_realpath.buf, &sb_tmp));
	write_file(sb_git.buf, "gitdir: %s", relative_path(sb_repo_realpath.buf, sb_path_realpath.buf, &sb_tmp));
	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s/commondir", sb_repo.buf);
	write_file(sb.buf, "../..");

	/*
	 * Set up the ref store of the worktree and create the HEAD reference.
	 */
	wt = get_linked_worktree(name, 1);
	if (!wt) {
		ret = error(_("could not find created worktree '%s'"), name);
		goto done;
	}
	wt_refs = get_worktree_ref_store(wt);

	ret = ref_store_create_on_disk(wt_refs, REF_STORE_CREATE_ON_DISK_IS_WORKTREE, &sb);
	if (ret)
		goto done;

	if (!is_branch && commit)
		ret = refs_update_ref(wt_refs, NULL, "HEAD", &commit->object.oid,
				      NULL, 0, UPDATE_REFS_MSG_ON_ERR);
	else
		ret = refs_update_symref(wt_refs, "HEAD", symref.buf, NULL);
	if (ret)
		goto done;

	/*
	 * If the current worktree has sparse-checkout enabled, then copy
	 * the sparse-checkout patterns from the current worktree.
	 */
	if (core_apply_sparse_checkout)
		copy_sparse_checkout(sb_repo.buf);

	/*
	 * If we are using worktree config, then copy all current config
	 * values from the current worktree into the new one, that way the
	 * new worktree behaves the same as this one.
	 */
	if (the_repository->repository_format_worktree_config)
		copy_filtered_worktree_config(sb_repo.buf);

	strvec_pushf(&child_env, "%s=%s", GIT_DIR_ENVIRONMENT, sb_git.buf);
	strvec_pushf(&child_env, "%s=%s", GIT_WORK_TREE_ENVIRONMENT, path);

	if (opts->orphan &&
	    (ret = make_worktree_orphan(refname, opts, &child_env)))
		goto done;

	if (opts->checkout &&
	    (ret = checkout_worktree(opts, &child_env)))
		goto done;

	is_junk = 0;
	FREE_AND_NULL(junk_work_tree);
	FREE_AND_NULL(junk_git_dir);

done:
	if (ret || !opts->keep_locked) {
		strbuf_reset(&sb);
		strbuf_addf(&sb, "%s/locked", sb_repo.buf);
		unlink_or_warn(sb.buf);
	}

	/*
	 * Hook failure does not warrant worktree deletion, so run hook after
	 * is_junk is cleared, but do return appropriate code when hook fails.
	 */
	if (!ret && opts->checkout && !opts->orphan) {
		struct run_hooks_opt opt = RUN_HOOKS_OPT_INIT;

		strvec_pushl(&opt.env, "GIT_DIR", "GIT_WORK_TREE", NULL);
		strvec_pushl(&opt.args,
			     oid_to_hex(null_oid()),
			     oid_to_hex(&commit->object.oid),
			     "1",
			     NULL);
		opt.dir = path;

		ret = run_hooks_opt(the_repository, "post-checkout", &opt);
	}

	strvec_clear(&child_env);
	strbuf_release(&sb);
	strbuf_release(&sb_tmp);
	strbuf_release(&symref);
	strbuf_release(&sb_repo);
	strbuf_release(&sb_repo_realpath);
	strbuf_release(&sb_git);
	strbuf_release(&sb_path_realpath);
	strbuf_release(&sb_name);
	free_worktree(wt);
	return ret;
}

static void print_preparing_worktree_line(int detach,
					  const char *branch,
					  const char *new_branch,
					  int force_new_branch)
{
	if (force_new_branch) {
		struct commit *commit = lookup_commit_reference_by_name(new_branch);
		if (!commit)
			fprintf_ln(stderr, _("Preparing worktree (new branch '%s')"), new_branch);
		else
			fprintf_ln(stderr, _("Preparing worktree (resetting branch '%s'; was at %s)"),
				  new_branch,
				  repo_find_unique_abbrev(the_repository, &commit->object.oid, DEFAULT_ABBREV));
	} else if (new_branch) {
		fprintf_ln(stderr, _("Preparing worktree (new branch '%s')"), new_branch);
	} else {
		struct strbuf s = STRBUF_INIT;
		if (!detach && !strbuf_check_branch_ref(&s, branch) &&
		    refs_ref_exists(get_main_ref_store(the_repository), s.buf))
			fprintf_ln(stderr, _("Preparing worktree (checking out '%s')"),
				  branch);
		else {
			struct commit *commit = lookup_commit_reference_by_name(branch);
			if (!commit)
				BUG(_("unreachable: invalid reference: %s"), branch);
			fprintf_ln(stderr, _("Preparing worktree (detached HEAD %s)"),
				  repo_find_unique_abbrev(the_repository, &commit->object.oid, DEFAULT_ABBREV));
		}
		strbuf_release(&s);
	}
}

/**
 * Callback to short circuit iteration over refs on the first reference
 * corresponding to a valid oid.
 *
 * Returns 0 on failure and non-zero on success.
 */
static int first_valid_ref(const char *refname UNUSED,
			   const char *referent UNUSED,
			   const struct object_id *oid UNUSED,
			   int flags UNUSED,
			   void *cb_data UNUSED)
{
	return 1;
}

/**
 * Verifies HEAD and determines whether there exist any valid local references.
 *
 * - Checks whether HEAD points to a valid reference.
 *
 * - Checks whether any valid local branches exist.
 *
 * - Emits a warning if there exist any valid branches but HEAD does not point
 *   to a valid reference.
 *
 * Returns 1 if any of the previous checks are true, otherwise returns 0.
 */
static int can_use_local_refs(const struct add_opts *opts)
{
	if (refs_head_ref(get_main_ref_store(the_repository), first_valid_ref, NULL)) {
		return 1;
	} else if (refs_for_each_branch_ref(get_main_ref_store(the_repository), first_valid_ref, NULL)) {
		if (!opts->quiet) {
			struct strbuf path = STRBUF_INIT;
			struct strbuf contents = STRBUF_INIT;

			strbuf_add_real_path(&path, get_worktree_git_dir(NULL));
			strbuf_addstr(&path, "/HEAD");
			strbuf_read_file(&contents, path.buf, 64);
			strbuf_stripspace(&contents, NULL);
			strbuf_strip_suffix(&contents, "\n");

			warning(_("HEAD points to an invalid (or orphaned) reference.\n"
				  "HEAD path: '%s'\n"
				  "HEAD contents: '%s'"),
				  path.buf, contents.buf);
			strbuf_release(&path);
			strbuf_release(&contents);
		}
		return 1;
	}
	return 0;
}

/**
 * Reports whether the necessary flags were set and whether the repository has
 * remote references to attempt DWIM tracking of upstream branches.
 *
 * 1. Checks that `--guess-remote` was used or `worktree.guessRemote = true`.
 *
 * 2. Checks whether any valid remote branches exist.
 *
 * 3. Checks that there exists at least one remote and emits a warning/error
 *    if both checks 1. and 2. are false (can be bypassed with `--force`).
 *
 * Returns 1 if checks 1. and 2. are true, otherwise 0.
 */
static int can_use_remote_refs(const struct add_opts *opts)
{
	if (!guess_remote) {
		return 0;
	} else if (refs_for_each_remote_ref(get_main_ref_store(the_repository), first_valid_ref, NULL)) {
		return 1;
	} else if (!opts->force && remote_get(NULL)) {
		die(_("No local or remote refs exist despite at least one remote\n"
		      "present, stopping; use 'add -f' to override or fetch a remote first"));
	}
	return 0;
}

/**
 * Determines whether `--orphan` should be inferred in the evaluation of
 * `worktree add path/` or `worktree add -b branch path/` and emits an error
 * if the supplied arguments would produce an illegal combination when the
 * `--orphan` flag is included.
 *
 * `opts` and `opt_track` contain the other options & flags supplied to the
 * command.
 *
 * remote determines whether to check `can_use_remote_refs()` or not. This
 * is primarily to differentiate between the basic `add` DWIM and `add -b`.
 *
 * Returns 1 when inferring `--orphan`, 0 otherwise, and emits an error when
 * `--orphan` is inferred but doing so produces an illegal combination of
 * options and flags. Additionally produces an error when remote refs are
 * checked and the repo is in a state that looks like the user added a remote
 * but forgot to fetch (and did not override the warning with -f).
 */
static int dwim_orphan(const struct add_opts *opts, int opt_track, int remote)
{
	if (can_use_local_refs(opts)) {
		return 0;
	} else if (remote && can_use_remote_refs(opts)) {
		return 0;
	} else if (!opts->quiet) {
		fprintf_ln(stderr, WORKTREE_ADD_DWIM_ORPHAN_INFER_TEXT);
	}

	if (opt_track) {
		die(_("options '%s' and '%s' cannot be used together"),
		    "--orphan", "--track");
	} else if (!opts->checkout) {
		die(_("options '%s' and '%s' cannot be used together"),
		    "--orphan", "--no-checkout");
	}
	return 1;
}

static char *dwim_branch(const char *path, char **new_branch)
{
	int n;
	int branch_exists;
	const char *s = worktree_basename(path, &n);
	char *branchname = xstrndup(s, n);
	struct strbuf ref = STRBUF_INIT;

	branch_exists = !strbuf_check_branch_ref(&ref, branchname) &&
			refs_ref_exists(get_main_ref_store(the_repository),
					ref.buf);
	strbuf_release(&ref);
	if (branch_exists)
		return branchname;

	*new_branch = branchname;
	if (guess_remote) {
		struct object_id oid;
		char *remote = unique_tracking_name(*new_branch, &oid, NULL);
		return remote;
	}
	return NULL;
}

static int add(int ac, const char **av, const char *prefix,
	       struct repository *repo UNUSED)
{
	struct add_opts opts;
	const char *new_branch_force = NULL;
	char *path;
	const char *branch;
	char *branch_to_free = NULL;
	char *new_branch_to_free = NULL;
	const char *new_branch = NULL;
	char *opt_track = NULL;
	const char *lock_reason = NULL;
	int keep_locked = 0;
	int used_new_branch_options;
	struct option options[] = {
		OPT__FORCE(&opts.force,
			   N_("checkout <branch> even if already checked out in other worktree"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_STRING('b', NULL, &new_branch, N_("branch"),
			   N_("create a new branch")),
		OPT_STRING('B', NULL, &new_branch_force, N_("branch"),
			   N_("create or reset a branch")),
		OPT_BOOL(0, "orphan", &opts.orphan, N_("create unborn branch")),
		OPT_BOOL('d', "detach", &opts.detach, N_("detach HEAD at named commit")),
		OPT_BOOL(0, "checkout", &opts.checkout, N_("populate the new working tree")),
		OPT_BOOL(0, "lock", &keep_locked, N_("keep the new working tree locked")),
		OPT_STRING(0, "reason", &lock_reason, N_("string"),
			   N_("reason for locking")),
		OPT__QUIET(&opts.quiet, N_("suppress progress reporting")),
		OPT_PASSTHRU(0, "track", &opt_track, NULL,
			     N_("set up tracking mode (see git-branch(1))"),
			     PARSE_OPT_NOARG | PARSE_OPT_OPTARG),
		OPT_BOOL(0, "guess-remote", &guess_remote,
			 N_("try to match the new branch name with a remote-tracking branch")),
		OPT_END()
	};
	int ret;

	memset(&opts, 0, sizeof(opts));
	opts.checkout = 1;
	ac = parse_options(ac, av, prefix, options, git_worktree_add_usage, 0);
	if (!!opts.detach + !!new_branch + !!new_branch_force > 1)
		die(_("options '%s', '%s', and '%s' cannot be used together"), "-b", "-B", "--detach");
	if (opts.detach && opts.orphan)
		die(_("options '%s' and '%s' cannot be used together"),
		    "--orphan", "--detach");
	if (opts.orphan && opt_track)
		die(_("options '%s' and '%s' cannot be used together"),
		    "--orphan", "--track");
	if (opts.orphan && !opts.checkout)
		die(_("options '%s' and '%s' cannot be used together"),
		    "--orphan", "--no-checkout");
	if (opts.orphan && ac == 2)
		die(_("option '%s' and commit-ish cannot be used together"),
		    "--orphan");
	if (lock_reason && !keep_locked)
		die(_("the option '%s' requires '%s'"), "--reason", "--lock");
	if (lock_reason)
		opts.keep_locked = lock_reason;
	else if (keep_locked)
		opts.keep_locked = _("added with --lock");

	if (ac < 1 || ac > 2)
		usage_with_options(git_worktree_add_usage, options);

	path = prefix_filename(prefix, av[0]);
	branch = ac < 2 ? "HEAD" : av[1];
	used_new_branch_options = new_branch || new_branch_force;

	if (!strcmp(branch, "-"))
		branch = "@{-1}";

	if (new_branch_force) {
		struct strbuf symref = STRBUF_INIT;

		new_branch = new_branch_force;

		if (!opts.force &&
		    !strbuf_check_branch_ref(&symref, new_branch) &&
		    refs_ref_exists(get_main_ref_store(the_repository), symref.buf))
			die_if_checked_out(symref.buf, 0);
		strbuf_release(&symref);
	}

	if (opts.orphan && !new_branch) {
		int n;
		const char *s = worktree_basename(path, &n);
		new_branch = new_branch_to_free = xstrndup(s, n);
	} else if (opts.orphan) {
		; /* no-op */
	} else if (opts.detach) {
		/* Check HEAD */
		if (!strcmp(branch, "HEAD"))
			can_use_local_refs(&opts);
	} else if (ac < 2 && new_branch) {
		/* DWIM: Infer --orphan when repo has no refs. */
		opts.orphan = dwim_orphan(&opts, !!opt_track, 0);
	} else if (ac < 2) {
		/* DWIM: Guess branch name from path. */
		char *s = dwim_branch(path, &new_branch_to_free);
		if (s)
			branch = branch_to_free = s;
		new_branch = new_branch_to_free;

		/* DWIM: Infer --orphan when repo has no refs. */
		opts.orphan = (!s) && dwim_orphan(&opts, !!opt_track, 1);
	} else if (ac == 2) {
		struct object_id oid;
		struct commit *commit;
		char *remote;

		commit = lookup_commit_reference_by_name(branch);
		if (!commit) {
			remote = unique_tracking_name(branch, &oid, NULL);
			if (remote) {
				new_branch = branch;
				branch = new_branch_to_free = remote;
			}
		}

		if (!strcmp(branch, "HEAD"))
			can_use_local_refs(&opts);

	}

	if (!opts.orphan && !lookup_commit_reference_by_name(branch)) {
		int attempt_hint = !opts.quiet && (ac < 2);
		if (attempt_hint && used_new_branch_options) {
			advise_if_enabled(ADVICE_WORKTREE_ADD_ORPHAN,
				WORKTREE_ADD_ORPHAN_WITH_DASH_B_HINT_TEXT,
				new_branch, path);
		} else if (attempt_hint) {
			advise_if_enabled(ADVICE_WORKTREE_ADD_ORPHAN,
				WORKTREE_ADD_ORPHAN_NO_DASH_B_HINT_TEXT, path);
		}
		die(_("invalid reference: %s"), branch);
	}

	if (!opts.quiet)
		print_preparing_worktree_line(opts.detach, branch, new_branch, !!new_branch_force);

	if (opts.orphan) {
		branch = new_branch;
	} else if (new_branch) {
		struct child_process cp = CHILD_PROCESS_INIT;
		cp.git_cmd = 1;
		strvec_push(&cp.args, "branch");
		if (new_branch_force)
			strvec_push(&cp.args, "--force");
		if (opts.quiet)
			strvec_push(&cp.args, "--quiet");
		strvec_push(&cp.args, new_branch);
		strvec_push(&cp.args, branch);
		if (opt_track)
			strvec_push(&cp.args, opt_track);
		if (run_command(&cp))
			return -1;
		branch = new_branch;
	} else if (opt_track) {
		die(_("--[no-]track can only be used if a new branch is created"));
	}

	ret = add_worktree(path, branch, &opts);
	free(path);
	free(opt_track);
	free(branch_to_free);
	free(new_branch_to_free);
	return ret;
}

static void show_worktree_porcelain(struct worktree *wt, int line_terminator)
{
	const char *reason;

	printf("worktree %s%c", wt->path, line_terminator);
	if (wt->is_bare)
		printf("bare%c", line_terminator);
	else {
		printf("HEAD %s%c", oid_to_hex(&wt->head_oid), line_terminator);
		if (wt->is_detached)
			printf("detached%c", line_terminator);
		else if (wt->head_ref)
			printf("branch %s%c", wt->head_ref, line_terminator);
	}

	reason = worktree_lock_reason(wt);
	if (reason) {
		fputs("locked", stdout);
		if (*reason) {
			fputc(' ', stdout);
			write_name_quoted(reason, stdout, line_terminator);
		} else {
			fputc(line_terminator, stdout);
		}
	}

	reason = worktree_prune_reason(wt, expire);
	if (reason)
		printf("prunable %s%c", reason, line_terminator);

	fputc(line_terminator, stdout);
}

static void show_worktree(struct worktree *wt, int path_maxlen, int abbrev_len)
{
	struct strbuf sb = STRBUF_INIT;
	int cur_path_len = strlen(wt->path);
	int path_adj = cur_path_len - utf8_strwidth(wt->path);
	const char *reason;

	strbuf_addf(&sb, "%-*s ", 1 + path_maxlen + path_adj, wt->path);
	if (wt->is_bare)
		strbuf_addstr(&sb, "(bare)");
	else {
		strbuf_addf(&sb, "%-*s ", abbrev_len,
				repo_find_unique_abbrev(the_repository, &wt->head_oid, DEFAULT_ABBREV));
		if (wt->is_detached)
			strbuf_addstr(&sb, "(detached HEAD)");
		else if (wt->head_ref) {
			char *ref = refs_shorten_unambiguous_ref(get_main_ref_store(the_repository),
								 wt->head_ref,
								 0);
			strbuf_addf(&sb, "[%s]", ref);
			free(ref);
		} else
			strbuf_addstr(&sb, "(error)");
	}

	reason = worktree_lock_reason(wt);
	if (verbose && reason && *reason)
		strbuf_addf(&sb, "\n\tlocked: %s", reason);
	else if (reason)
		strbuf_addstr(&sb, " locked");

	reason = worktree_prune_reason(wt, expire);
	if (verbose && reason)
		strbuf_addf(&sb, "\n\tprunable: %s", reason);
	else if (reason)
		strbuf_addstr(&sb, " prunable");

	printf("%s\n", sb.buf);
	strbuf_release(&sb);
}

static void measure_widths(struct worktree **wt, int *abbrev, int *maxlen)
{
	int i;

	for (i = 0; wt[i]; i++) {
		int sha1_len;
		int path_len = strlen(wt[i]->path);

		if (path_len > *maxlen)
			*maxlen = path_len;
		sha1_len = strlen(repo_find_unique_abbrev(the_repository, &wt[i]->head_oid, *abbrev));
		if (sha1_len > *abbrev)
			*abbrev = sha1_len;
	}
}

static int pathcmp(const void *a_, const void *b_)
{
	const struct worktree *const *a = a_;
	const struct worktree *const *b = b_;
	return fspathcmp((*a)->path, (*b)->path);
}

static void pathsort(struct worktree **wt)
{
	int n = 0;
	struct worktree **p = wt;

	while (*p++)
		n++;
	QSORT(wt, n, pathcmp);
}

static int list(int ac, const char **av, const char *prefix,
		struct repository *repo UNUSED)
{
	int porcelain = 0;
	int line_terminator = '\n';

	struct option options[] = {
		OPT_BOOL(0, "porcelain", &porcelain, N_("machine-readable output")),
		OPT__VERBOSE(&verbose, N_("show extended annotations and reasons, if available")),
		OPT_EXPIRY_DATE(0, "expire", &expire,
				N_("add 'prunable' annotation to worktrees older than <time>")),
		OPT_SET_INT('z', NULL, &line_terminator,
			    N_("terminate records with a NUL character"), '\0'),
		OPT_END()
	};

	expire = TIME_MAX;
	ac = parse_options(ac, av, prefix, options, git_worktree_list_usage, 0);
	if (ac)
		usage_with_options(git_worktree_list_usage, options);
	else if (verbose && porcelain)
		die(_("options '%s' and '%s' cannot be used together"), "--verbose", "--porcelain");
	else if (!line_terminator && !porcelain)
		die(_("the option '%s' requires '%s'"), "-z", "--porcelain");
	else {
		struct worktree **worktrees = get_worktrees();
		int path_maxlen = 0, abbrev = DEFAULT_ABBREV, i;

		/* sort worktrees by path but keep main worktree at top */
		pathsort(worktrees + 1);

		if (!porcelain)
			measure_widths(worktrees, &abbrev, &path_maxlen);

		for (i = 0; worktrees[i]; i++) {
			if (porcelain)
				show_worktree_porcelain(worktrees[i],
							line_terminator);
			else
				show_worktree(worktrees[i], path_maxlen, abbrev);
		}
		free_worktrees(worktrees);
	}
	return 0;
}

static int lock_worktree(int ac, const char **av, const char *prefix,
			 struct repository *repo UNUSED)
{
	const char *reason = "", *old_reason;
	struct option options[] = {
		OPT_STRING(0, "reason", &reason, N_("string"),
			   N_("reason for locking")),
		OPT_END()
	};
	struct worktree **worktrees, *wt;

	ac = parse_options(ac, av, prefix, options, git_worktree_lock_usage, 0);
	if (ac != 1)
		usage_with_options(git_worktree_lock_usage, options);

	worktrees = get_worktrees();
	wt = find_worktree(worktrees, prefix, av[0]);
	if (!wt)
		die(_("'%s' is not a working tree"), av[0]);
	if (is_main_worktree(wt))
		die(_("The main working tree cannot be locked or unlocked"));

	old_reason = worktree_lock_reason(wt);
	if (old_reason) {
		if (*old_reason)
			die(_("'%s' is already locked, reason: %s"),
			    av[0], old_reason);
		die(_("'%s' is already locked"), av[0]);
	}

	write_file(git_common_path("worktrees/%s/locked", wt->id),
		   "%s", reason);
	free_worktrees(worktrees);
	return 0;
}

static int unlock_worktree(int ac, const char **av, const char *prefix,
			   struct repository *repo UNUSED)
{
	struct option options[] = {
		OPT_END()
	};
	struct worktree **worktrees, *wt;
	int ret;

	ac = parse_options(ac, av, prefix, options, git_worktree_unlock_usage, 0);
	if (ac != 1)
		usage_with_options(git_worktree_unlock_usage, options);

	worktrees = get_worktrees();
	wt = find_worktree(worktrees, prefix, av[0]);
	if (!wt)
		die(_("'%s' is not a working tree"), av[0]);
	if (is_main_worktree(wt))
		die(_("The main working tree cannot be locked or unlocked"));
	if (!worktree_lock_reason(wt))
		die(_("'%s' is not locked"), av[0]);
	ret = unlink_or_warn(git_common_path("worktrees/%s/locked", wt->id));
	free_worktrees(worktrees);
	return ret;
}

static void validate_no_submodules(const struct worktree *wt)
{
	struct index_state istate = INDEX_STATE_INIT(the_repository);
	struct strbuf path = STRBUF_INIT;
	int i, found_submodules = 0;

	if (is_directory(worktree_git_path(the_repository, wt, "modules"))) {
		/*
		 * There could be false positives, e.g. the "modules"
		 * directory exists but is empty. But it's a rare case and
		 * this simpler check is probably good enough for now.
		 */
		found_submodules = 1;
	} else if (read_index_from(&istate, worktree_git_path(the_repository, wt, "index"),
				   get_worktree_git_dir(wt)) > 0) {
		for (i = 0; i < istate.cache_nr; i++) {
			struct cache_entry *ce = istate.cache[i];
			int err;

			if (!S_ISGITLINK(ce->ce_mode))
				continue;

			strbuf_reset(&path);
			strbuf_addf(&path, "%s/%s", wt->path, ce->name);
			if (!is_submodule_populated_gently(path.buf, &err))
				continue;

			found_submodules = 1;
			break;
		}
	}
	discard_index(&istate);
	strbuf_release(&path);

	if (found_submodules)
		die(_("working trees containing submodules cannot be moved or removed"));
}

static int move_worktree(int ac, const char **av, const char *prefix,
			 struct repository *repo UNUSED)
{
	int force = 0;
	struct option options[] = {
		OPT__FORCE(&force,
			 N_("force move even if worktree is dirty or locked"),
			 PARSE_OPT_NOCOMPLETE),
		OPT_END()
	};
	struct worktree **worktrees, *wt;
	struct strbuf dst = STRBUF_INIT;
	struct strbuf errmsg = STRBUF_INIT;
	const char *reason = NULL;
	char *path;

	ac = parse_options(ac, av, prefix, options, git_worktree_move_usage,
			   0);
	if (ac != 2)
		usage_with_options(git_worktree_move_usage, options);

	path = prefix_filename(prefix, av[1]);
	strbuf_addstr(&dst, path);
	free(path);

	worktrees = get_worktrees();
	wt = find_worktree(worktrees, prefix, av[0]);
	if (!wt)
		die(_("'%s' is not a working tree"), av[0]);
	if (is_main_worktree(wt))
		die(_("'%s' is a main working tree"), av[0]);
	if (is_directory(dst.buf)) {
		const char *sep = find_last_dir_sep(wt->path);

		if (!sep)
			die(_("could not figure out destination name from '%s'"),
			    wt->path);
		strbuf_trim_trailing_dir_sep(&dst);
		strbuf_addstr(&dst, sep);
	}
	check_candidate_path(dst.buf, force, worktrees, "move");

	validate_no_submodules(wt);

	if (force < 2)
		reason = worktree_lock_reason(wt);
	if (reason) {
		if (*reason)
			die(_("cannot move a locked working tree, lock reason: %s\nuse 'move -f -f' to override or unlock first"),
			    reason);
		die(_("cannot move a locked working tree;\nuse 'move -f -f' to override or unlock first"));
	}
	if (validate_worktree(wt, &errmsg, 0))
		die(_("validation failed, cannot move working tree: %s"),
		    errmsg.buf);
	strbuf_release(&errmsg);

	if (rename(wt->path, dst.buf) == -1)
		die_errno(_("failed to move '%s' to '%s'"), wt->path, dst.buf);

	update_worktree_location(wt, dst.buf);

	strbuf_release(&dst);
	free_worktrees(worktrees);
	return 0;
}

/*
 * Note, "git status --porcelain" is used to determine if it's safe to
 * delete a whole worktree. "git status" does not ignore user
 * configuration, so if a normal "git status" shows "clean" for the
 * user, then it's ok to remove it.
 *
 * This assumption may be a bad one. We may want to ignore
 * (potentially bad) user settings and only delete a worktree when
 * it's absolutely safe to do so from _our_ point of view because we
 * know better.
 */
static void check_clean_worktree(struct worktree *wt,
				 const char *original_path)
{
	struct child_process cp;
	char buf[1];
	int ret;

	/*
	 * Until we sort this out, all submodules are "dirty" and
	 * will abort this function.
	 */
	validate_no_submodules(wt);

	child_process_init(&cp);
	strvec_pushf(&cp.env, "%s=%s/.git",
		     GIT_DIR_ENVIRONMENT, wt->path);
	strvec_pushf(&cp.env, "%s=%s",
		     GIT_WORK_TREE_ENVIRONMENT, wt->path);
	strvec_pushl(&cp.args, "status",
		     "--porcelain", "--ignore-submodules=none",
		     NULL);
	cp.git_cmd = 1;
	cp.dir = wt->path;
	cp.out = -1;
	ret = start_command(&cp);
	if (ret)
		die_errno(_("failed to run 'git status' on '%s'"),
			  original_path);
	ret = xread(cp.out, buf, sizeof(buf));
	if (ret)
		die(_("'%s' contains modified or untracked files, use --force to delete it"),
		    original_path);
	close(cp.out);
	ret = finish_command(&cp);
	if (ret)
		die_errno(_("failed to run 'git status' on '%s', code %d"),
			  original_path, ret);
}

static int delete_git_work_tree(struct worktree *wt)
{
	struct strbuf sb = STRBUF_INIT;
	int ret = 0;

	strbuf_addstr(&sb, wt->path);
	if (remove_dir_recursively(&sb, 0)) {
		error_errno(_("failed to delete '%s'"), sb.buf);
		ret = -1;
	}
	strbuf_release(&sb);
	return ret;
}

static int remove_worktree(int ac, const char **av, const char *prefix,
			   struct repository *repo UNUSED)
{
	int force = 0;
	struct option options[] = {
		OPT__FORCE(&force,
			 N_("force removal even if worktree is dirty or locked"),
			 PARSE_OPT_NOCOMPLETE),
		OPT_END()
	};
	struct worktree **worktrees, *wt;
	struct strbuf errmsg = STRBUF_INIT;
	const char *reason = NULL;
	int ret = 0;

	ac = parse_options(ac, av, prefix, options, git_worktree_remove_usage, 0);
	if (ac != 1)
		usage_with_options(git_worktree_remove_usage, options);

	worktrees = get_worktrees();
	wt = find_worktree(worktrees, prefix, av[0]);
	if (!wt)
		die(_("'%s' is not a working tree"), av[0]);
	if (is_main_worktree(wt))
		die(_("'%s' is a main working tree"), av[0]);
	if (force < 2)
		reason = worktree_lock_reason(wt);
	if (reason) {
		if (*reason)
			die(_("cannot remove a locked working tree, lock reason: %s\nuse 'remove -f -f' to override or unlock first"),
			    reason);
		die(_("cannot remove a locked working tree;\nuse 'remove -f -f' to override or unlock first"));
	}
	if (validate_worktree(wt, &errmsg, WT_VALIDATE_WORKTREE_MISSING_OK))
		die(_("validation failed, cannot remove working tree: %s"),
		    errmsg.buf);
	strbuf_release(&errmsg);

	if (file_exists(wt->path)) {
		if (!force)
			check_clean_worktree(wt, av[0]);

		ret |= delete_git_work_tree(wt);
	}
	/*
	 * continue on even if ret is non-zero, there's no going back
	 * from here.
	 */
	ret |= delete_git_dir(wt->id);
	delete_worktrees_dir_if_empty();

	free_worktrees(worktrees);
	return ret;
}

static void report_repair(int iserr, const char *path, const char *msg, void *cb_data)
{
	if (!iserr) {
		fprintf_ln(stderr, _("repair: %s: %s"), msg, path);
	} else {
		int *exit_status = (int *)cb_data;
		fprintf_ln(stderr, _("error: %s: %s"), msg, path);
		*exit_status = 1;
	}
}

static int repair(int ac, const char **av, const char *prefix,
		  struct repository *repo UNUSED)
{
	const char **p;
	const char *self[] = { ".", NULL };
	struct option options[] = {
		OPT_END()
	};
	int rc = 0;

	ac = parse_options(ac, av, prefix, options, git_worktree_repair_usage, 0);
	p = ac > 0 ? av : self;
	for (; *p; p++)
		repair_worktree_at_path(*p, report_repair, &rc);
	repair_worktrees(report_repair, &rc);
	return rc;
}

int cmd_worktree(int ac,
		 const char **av,
		 const char *prefix,
		 struct repository *repo)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("add", &fn, add),
		OPT_SUBCOMMAND("prune", &fn, prune),
		OPT_SUBCOMMAND("list", &fn, list),
		OPT_SUBCOMMAND("lock", &fn, lock_worktree),
		OPT_SUBCOMMAND("unlock", &fn, unlock_worktree),
		OPT_SUBCOMMAND("move", &fn, move_worktree),
		OPT_SUBCOMMAND("remove", &fn, remove_worktree),
		OPT_SUBCOMMAND("repair", &fn, repair),
		OPT_END()
	};

	git_config(git_worktree_config, NULL);

	if (!prefix)
		prefix = "";

	ac = parse_options(ac, av, prefix, options, git_worktree_usage, 0);

	prepare_repo_settings(the_repository);
	the_repository->settings.command_requires_full_index = 0;

	return fn(ac, av, prefix, repo);
}
