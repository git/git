/*
 * Builtin "git pull"
 *
 * Based on git-pull.sh by Junio C Hamano
 *
 * Fetch one or more remote refs and merge it/them into the current HEAD.
 */
#define USE_THE_INDEX_VARIABLE
#include "cache.h"
#include "config.h"
#include "builtin.h"
#include "hex.h"
#include "parse-options.h"
#include "exec-cmd.h"
#include "run-command.h"
#include "oid-array.h"
#include "remote.h"
#include "dir.h"
#include "rebase.h"
#include "refs.h"
#include "refspec.h"
#include "revision.h"
#include "submodule.h"
#include "submodule-config.h"
#include "tempfile.h"
#include "lockfile.h"
#include "wt-status.h"
#include "commit-reach.h"
#include "sequencer.h"
#include "packfile.h"

/**
 * Parses the value of --rebase. If value is a false value, returns
 * REBASE_FALSE. If value is a true value, returns REBASE_TRUE. If value is
 * "merges", returns REBASE_MERGES. If value is a invalid value, dies with
 * a fatal error if fatal is true, otherwise returns REBASE_INVALID.
 */
static enum rebase_type parse_config_rebase(const char *key, const char *value,
		int fatal)
{
	enum rebase_type v = rebase_parse_value(value);
	if (v != REBASE_INVALID)
		return v;

	if (fatal)
		die(_("invalid value for '%s': '%s'"), key, value);
	else
		error(_("invalid value for '%s': '%s'"), key, value);

	return REBASE_INVALID;
}

/**
 * Callback for --rebase, which parses arg with parse_config_rebase().
 */
static int parse_opt_rebase(const struct option *opt, const char *arg, int unset)
{
	enum rebase_type *value = opt->value;

	if (arg)
		*value = parse_config_rebase("--rebase", arg, 0);
	else
		*value = unset ? REBASE_FALSE : REBASE_TRUE;
	return *value == REBASE_INVALID ? -1 : 0;
}

static const char * const pull_usage[] = {
	N_("git pull [<options>] [<repository> [<refspec>...]]"),
	NULL
};

/* Shared options */
static int opt_verbosity;
static char *opt_progress;
static int recurse_submodules = RECURSE_SUBMODULES_DEFAULT;
static int recurse_submodules_cli = RECURSE_SUBMODULES_DEFAULT;

/* Options passed to git-merge or git-rebase */
static enum rebase_type opt_rebase = -1;
static char *opt_diffstat;
static char *opt_log;
static char *opt_signoff;
static char *opt_squash;
static char *opt_commit;
static char *opt_edit;
static char *cleanup_arg;
static char *opt_ff;
static char *opt_verify_signatures;
static char *opt_verify;
static int opt_autostash = -1;
static int config_autostash;
static int check_trust_level = 1;
static struct strvec opt_strategies = STRVEC_INIT;
static struct strvec opt_strategy_opts = STRVEC_INIT;
static char *opt_gpg_sign;
static int opt_allow_unrelated_histories;

/* Options passed to git-fetch */
static char *opt_all;
static char *opt_append;
static char *opt_upload_pack;
static int opt_force;
static char *opt_tags;
static char *opt_prune;
static char *max_children;
static int opt_dry_run;
static char *opt_keep;
static char *opt_depth;
static char *opt_unshallow;
static char *opt_update_shallow;
static char *opt_refmap;
static char *opt_ipv4;
static char *opt_ipv6;
static int opt_show_forced_updates = -1;
static char *set_upstream;
static struct strvec opt_fetch = STRVEC_INIT;

static struct option pull_options[] = {
	/* Shared options */
	OPT__VERBOSITY(&opt_verbosity),
	OPT_PASSTHRU(0, "progress", &opt_progress, NULL,
		N_("force progress reporting"),
		PARSE_OPT_NOARG),
	OPT_CALLBACK_F(0, "recurse-submodules",
		   &recurse_submodules_cli, N_("on-demand"),
		   N_("control for recursive fetching of submodules"),
		   PARSE_OPT_OPTARG, option_fetch_parse_recurse_submodules),

	/* Options passed to git-merge or git-rebase */
	OPT_GROUP(N_("Options related to merging")),
	OPT_CALLBACK_F('r', "rebase", &opt_rebase,
		"(false|true|merges|interactive)",
		N_("incorporate changes by rebasing rather than merging"),
		PARSE_OPT_OPTARG, parse_opt_rebase),
	OPT_PASSTHRU('n', NULL, &opt_diffstat, NULL,
		N_("do not show a diffstat at the end of the merge"),
		PARSE_OPT_NOARG | PARSE_OPT_NONEG),
	OPT_PASSTHRU(0, "stat", &opt_diffstat, NULL,
		N_("show a diffstat at the end of the merge"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "summary", &opt_diffstat, NULL,
		N_("(synonym to --stat)"),
		PARSE_OPT_NOARG | PARSE_OPT_HIDDEN),
	OPT_PASSTHRU(0, "log", &opt_log, N_("n"),
		N_("add (at most <n>) entries from shortlog to merge commit message"),
		PARSE_OPT_OPTARG),
	OPT_PASSTHRU(0, "signoff", &opt_signoff, NULL,
		N_("add a Signed-off-by trailer"),
		PARSE_OPT_OPTARG),
	OPT_PASSTHRU(0, "squash", &opt_squash, NULL,
		N_("create a single commit instead of doing a merge"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "commit", &opt_commit, NULL,
		N_("perform a commit if the merge succeeds (default)"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "edit", &opt_edit, NULL,
		N_("edit message before committing"),
		PARSE_OPT_NOARG),
	OPT_CLEANUP(&cleanup_arg),
	OPT_PASSTHRU(0, "ff", &opt_ff, NULL,
		N_("allow fast-forward"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "ff-only", &opt_ff, NULL,
		N_("abort if fast-forward is not possible"),
		PARSE_OPT_NOARG | PARSE_OPT_NONEG),
	OPT_PASSTHRU(0, "verify", &opt_verify, NULL,
		N_("control use of pre-merge-commit and commit-msg hooks"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "verify-signatures", &opt_verify_signatures, NULL,
		N_("verify that the named commit has a valid GPG signature"),
		PARSE_OPT_NOARG),
	OPT_BOOL(0, "autostash", &opt_autostash,
		N_("automatically stash/stash pop before and after")),
	OPT_PASSTHRU_ARGV('s', "strategy", &opt_strategies, N_("strategy"),
		N_("merge strategy to use"),
		0),
	OPT_PASSTHRU_ARGV('X', "strategy-option", &opt_strategy_opts,
		N_("option=value"),
		N_("option for selected merge strategy"),
		0),
	OPT_PASSTHRU('S', "gpg-sign", &opt_gpg_sign, N_("key-id"),
		N_("GPG sign commit"),
		PARSE_OPT_OPTARG),
	OPT_SET_INT(0, "allow-unrelated-histories",
		    &opt_allow_unrelated_histories,
		    N_("allow merging unrelated histories"), 1),

	/* Options passed to git-fetch */
	OPT_GROUP(N_("Options related to fetching")),
	OPT_PASSTHRU(0, "all", &opt_all, NULL,
		N_("fetch from all remotes"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU('a', "append", &opt_append, NULL,
		N_("append to .git/FETCH_HEAD instead of overwriting"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "upload-pack", &opt_upload_pack, N_("path"),
		N_("path to upload pack on remote end"),
		0),
	OPT__FORCE(&opt_force, N_("force overwrite of local branch"), 0),
	OPT_PASSTHRU('t', "tags", &opt_tags, NULL,
		N_("fetch all tags and associated objects"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU('p', "prune", &opt_prune, NULL,
		N_("prune remote-tracking branches no longer on remote"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU('j', "jobs", &max_children, N_("n"),
		N_("number of submodules pulled in parallel"),
		PARSE_OPT_OPTARG),
	OPT_BOOL(0, "dry-run", &opt_dry_run,
		N_("dry run")),
	OPT_PASSTHRU('k', "keep", &opt_keep, NULL,
		N_("keep downloaded pack"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "depth", &opt_depth, N_("depth"),
		N_("deepen history of shallow clone"),
		0),
	OPT_PASSTHRU_ARGV(0, "shallow-since", &opt_fetch, N_("time"),
		N_("deepen history of shallow repository based on time"),
		0),
	OPT_PASSTHRU_ARGV(0, "shallow-exclude", &opt_fetch, N_("revision"),
		N_("deepen history of shallow clone, excluding rev"),
		0),
	OPT_PASSTHRU_ARGV(0, "deepen", &opt_fetch, N_("n"),
		N_("deepen history of shallow clone"),
		0),
	OPT_PASSTHRU(0, "unshallow", &opt_unshallow, NULL,
		N_("convert to a complete repository"),
		PARSE_OPT_NONEG | PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "update-shallow", &opt_update_shallow, NULL,
		N_("accept refs that update .git/shallow"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "refmap", &opt_refmap, N_("refmap"),
		N_("specify fetch refmap"),
		PARSE_OPT_NONEG),
	OPT_PASSTHRU_ARGV('o', "server-option", &opt_fetch,
		N_("server-specific"),
		N_("option to transmit"),
		0),
	OPT_PASSTHRU('4',  "ipv4", &opt_ipv4, NULL,
		N_("use IPv4 addresses only"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU('6',  "ipv6", &opt_ipv6, NULL,
		N_("use IPv6 addresses only"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU_ARGV(0, "negotiation-tip", &opt_fetch, N_("revision"),
		N_("report that we have only objects reachable from this object"),
		0),
	OPT_BOOL(0, "show-forced-updates", &opt_show_forced_updates,
		 N_("check for forced-updates on all updated branches")),
	OPT_PASSTHRU(0, "set-upstream", &set_upstream, NULL,
		N_("set upstream for git pull/fetch"),
		PARSE_OPT_NOARG),

	OPT_END()
};

/**
 * Pushes "-q" or "-v" switches into arr to match the opt_verbosity level.
 */
static void argv_push_verbosity(struct strvec *arr)
{
	int verbosity;

	for (verbosity = opt_verbosity; verbosity > 0; verbosity--)
		strvec_push(arr, "-v");

	for (verbosity = opt_verbosity; verbosity < 0; verbosity++)
		strvec_push(arr, "-q");
}

/**
 * Pushes "-f" switches into arr to match the opt_force level.
 */
static void argv_push_force(struct strvec *arr)
{
	int force = opt_force;
	while (force-- > 0)
		strvec_push(arr, "-f");
}

/**
 * Sets the GIT_REFLOG_ACTION environment variable to the concatenation of argv
 */
static void set_reflog_message(int argc, const char **argv)
{
	int i;
	struct strbuf msg = STRBUF_INIT;

	for (i = 0; i < argc; i++) {
		if (i)
			strbuf_addch(&msg, ' ');
		strbuf_addstr(&msg, argv[i]);
	}

	setenv("GIT_REFLOG_ACTION", msg.buf, 0);

	strbuf_release(&msg);
}

/**
 * If pull.ff is unset, returns NULL. If pull.ff is "true", returns "--ff". If
 * pull.ff is "false", returns "--no-ff". If pull.ff is "only", returns
 * "--ff-only". Otherwise, if pull.ff is set to an invalid value, die with an
 * error.
 */
static const char *config_get_ff(void)
{
	const char *value;

	if (git_config_get_value("pull.ff", &value))
		return NULL;

	switch (git_parse_maybe_bool(value)) {
	case 0:
		return "--no-ff";
	case 1:
		return "--ff";
	}

	if (!strcmp(value, "only"))
		return "--ff-only";

	die(_("invalid value for '%s': '%s'"), "pull.ff", value);
}

/**
 * Returns the default configured value for --rebase. It first looks for the
 * value of "branch.$curr_branch.rebase", where $curr_branch is the current
 * branch, and if HEAD is detached or the configuration key does not exist,
 * looks for the value of "pull.rebase". If both configuration keys do not
 * exist, returns REBASE_FALSE.
 */
static enum rebase_type config_get_rebase(int *rebase_unspecified)
{
	struct branch *curr_branch = branch_get("HEAD");
	const char *value;

	if (curr_branch) {
		char *key = xstrfmt("branch.%s.rebase", curr_branch->name);

		if (!git_config_get_value(key, &value)) {
			enum rebase_type ret = parse_config_rebase(key, value, 1);
			free(key);
			return ret;
		}

		free(key);
	}

	if (!git_config_get_value("pull.rebase", &value))
		return parse_config_rebase("pull.rebase", value, 1);

	*rebase_unspecified = 1;

	return REBASE_FALSE;
}

/**
 * Read config variables.
 */
static int git_pull_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "rebase.autostash")) {
		config_autostash = git_config_bool(var, value);
		return 0;
	} else if (!strcmp(var, "submodule.recurse")) {
		recurse_submodules = git_config_bool(var, value) ?
			RECURSE_SUBMODULES_ON : RECURSE_SUBMODULES_OFF;
		return 0;
	} else if (!strcmp(var, "gpg.mintrustlevel")) {
		check_trust_level = 0;
	}

	return git_default_config(var, value, cb);
}

/**
 * Appends merge candidates from FETCH_HEAD that are not marked not-for-merge
 * into merge_heads.
 */
static void get_merge_heads(struct oid_array *merge_heads)
{
	const char *filename = git_path_fetch_head(the_repository);
	FILE *fp;
	struct strbuf sb = STRBUF_INIT;
	struct object_id oid;

	fp = xfopen(filename, "r");
	while (strbuf_getline_lf(&sb, fp) != EOF) {
		const char *p;
		if (parse_oid_hex(sb.buf, &oid, &p))
			continue;  /* invalid line: does not start with object ID */
		if (starts_with(p, "\tnot-for-merge\t"))
			continue;  /* ref is not-for-merge */
		oid_array_append(merge_heads, &oid);
	}
	fclose(fp);
	strbuf_release(&sb);
}

/**
 * Used by die_no_merge_candidates() as a for_each_remote() callback to
 * retrieve the name of the remote if the repository only has one remote.
 */
static int get_only_remote(struct remote *remote, void *cb_data)
{
	const char **remote_name = cb_data;

	if (*remote_name)
		return -1;

	*remote_name = remote->name;
	return 0;
}

/**
 * Dies with the appropriate reason for why there are no merge candidates:
 *
 * 1. We fetched from a specific remote, and a refspec was given, but it ended
 *    up not fetching anything. This is usually because the user provided a
 *    wildcard refspec which had no matches on the remote end.
 *
 * 2. We fetched from a non-default remote, but didn't specify a branch to
 *    merge. We can't use the configured one because it applies to the default
 *    remote, thus the user must specify the branches to merge.
 *
 * 3. We fetched from the branch's or repo's default remote, but:
 *
 *    a. We are not on a branch, so there will never be a configured branch to
 *       merge with.
 *
 *    b. We are on a branch, but there is no configured branch to merge with.
 *
 * 4. We fetched from the branch's or repo's default remote, but the configured
 *    branch to merge didn't get fetched. (Either it doesn't exist, or wasn't
 *    part of the configured fetch refspec.)
 */
static void NORETURN die_no_merge_candidates(const char *repo, const char **refspecs)
{
	struct branch *curr_branch = branch_get("HEAD");
	const char *remote = curr_branch ? curr_branch->remote_name : NULL;

	if (*refspecs) {
		if (opt_rebase)
			fprintf_ln(stderr, _("There is no candidate for rebasing against among the refs that you just fetched."));
		else
			fprintf_ln(stderr, _("There are no candidates for merging among the refs that you just fetched."));
		fprintf_ln(stderr, _("Generally this means that you provided a wildcard refspec which had no\n"
					"matches on the remote end."));
	} else if (repo && curr_branch && (!remote || strcmp(repo, remote))) {
		fprintf_ln(stderr, _("You asked to pull from the remote '%s', but did not specify\n"
			"a branch. Because this is not the default configured remote\n"
			"for your current branch, you must specify a branch on the command line."),
			repo);
	} else if (!curr_branch) {
		fprintf_ln(stderr, _("You are not currently on a branch."));
		if (opt_rebase)
			fprintf_ln(stderr, _("Please specify which branch you want to rebase against."));
		else
			fprintf_ln(stderr, _("Please specify which branch you want to merge with."));
		fprintf_ln(stderr, _("See git-pull(1) for details."));
		fprintf(stderr, "\n");
		fprintf_ln(stderr, "    git pull %s %s", _("<remote>"), _("<branch>"));
		fprintf(stderr, "\n");
	} else if (!curr_branch->merge_nr) {
		const char *remote_name = NULL;

		if (for_each_remote(get_only_remote, &remote_name) || !remote_name)
			remote_name = _("<remote>");

		fprintf_ln(stderr, _("There is no tracking information for the current branch."));
		if (opt_rebase)
			fprintf_ln(stderr, _("Please specify which branch you want to rebase against."));
		else
			fprintf_ln(stderr, _("Please specify which branch you want to merge with."));
		fprintf_ln(stderr, _("See git-pull(1) for details."));
		fprintf(stderr, "\n");
		fprintf_ln(stderr, "    git pull %s %s", _("<remote>"), _("<branch>"));
		fprintf(stderr, "\n");
		fprintf_ln(stderr, _("If you wish to set tracking information for this branch you can do so with:"));
		fprintf(stderr, "\n");
		fprintf_ln(stderr, "    git branch --set-upstream-to=%s/%s %s\n",
				remote_name, _("<branch>"), curr_branch->name);
	} else
		fprintf_ln(stderr, _("Your configuration specifies to merge with the ref '%s'\n"
			"from the remote, but no such ref was fetched."),
			*curr_branch->merge_name);
	exit(1);
}

/**
 * Parses argv into [<repo> [<refspecs>...]], returning their values in `repo`
 * as a string and `refspecs` as a null-terminated array of strings. If `repo`
 * is not provided in argv, it is set to NULL.
 */
static void parse_repo_refspecs(int argc, const char **argv, const char **repo,
		const char ***refspecs)
{
	if (argc > 0) {
		*repo = *argv++;
		argc--;
	} else
		*repo = NULL;
	*refspecs = argv;
}

/**
 * Runs git-fetch, returning its exit status. `repo` and `refspecs` are the
 * repository and refspecs to fetch, or NULL if they are not provided.
 */
static int run_fetch(const char *repo, const char **refspecs)
{
	struct child_process cmd = CHILD_PROCESS_INIT;

	strvec_pushl(&cmd.args, "fetch", "--update-head-ok", NULL);

	/* Shared options */
	argv_push_verbosity(&cmd.args);
	if (opt_progress)
		strvec_push(&cmd.args, opt_progress);

	/* Options passed to git-fetch */
	if (opt_all)
		strvec_push(&cmd.args, opt_all);
	if (opt_append)
		strvec_push(&cmd.args, opt_append);
	if (opt_upload_pack)
		strvec_push(&cmd.args, opt_upload_pack);
	argv_push_force(&cmd.args);
	if (opt_tags)
		strvec_push(&cmd.args, opt_tags);
	if (opt_prune)
		strvec_push(&cmd.args, opt_prune);
	if (recurse_submodules_cli != RECURSE_SUBMODULES_DEFAULT)
		switch (recurse_submodules_cli) {
		case RECURSE_SUBMODULES_ON:
			strvec_push(&cmd.args, "--recurse-submodules=on");
			break;
		case RECURSE_SUBMODULES_OFF:
			strvec_push(&cmd.args, "--recurse-submodules=no");
			break;
		case RECURSE_SUBMODULES_ON_DEMAND:
			strvec_push(&cmd.args, "--recurse-submodules=on-demand");
			break;
		default:
			BUG("submodule recursion option not understood");
		}
	if (max_children)
		strvec_push(&cmd.args, max_children);
	if (opt_dry_run)
		strvec_push(&cmd.args, "--dry-run");
	if (opt_keep)
		strvec_push(&cmd.args, opt_keep);
	if (opt_depth)
		strvec_push(&cmd.args, opt_depth);
	if (opt_unshallow)
		strvec_push(&cmd.args, opt_unshallow);
	if (opt_update_shallow)
		strvec_push(&cmd.args, opt_update_shallow);
	if (opt_refmap)
		strvec_push(&cmd.args, opt_refmap);
	if (opt_ipv4)
		strvec_push(&cmd.args, opt_ipv4);
	if (opt_ipv6)
		strvec_push(&cmd.args, opt_ipv6);
	if (opt_show_forced_updates > 0)
		strvec_push(&cmd.args, "--show-forced-updates");
	else if (opt_show_forced_updates == 0)
		strvec_push(&cmd.args, "--no-show-forced-updates");
	if (set_upstream)
		strvec_push(&cmd.args, set_upstream);
	strvec_pushv(&cmd.args, opt_fetch.v);

	if (repo) {
		strvec_push(&cmd.args, repo);
		strvec_pushv(&cmd.args, refspecs);
	} else if (*refspecs)
		BUG("refspecs without repo?");
	cmd.git_cmd = 1;
	cmd.close_object_store = 1;
	return run_command(&cmd);
}

/**
 * "Pulls into void" by branching off merge_head.
 */
static int pull_into_void(const struct object_id *merge_head,
		const struct object_id *curr_head)
{
	if (opt_verify_signatures) {
		struct commit *commit;

		commit = lookup_commit(the_repository, merge_head);
		if (!commit)
			die(_("unable to access commit %s"),
			    oid_to_hex(merge_head));

		verify_merge_signature(commit, opt_verbosity,
				       check_trust_level);
	}

	/*
	 * Two-way merge: we treat the index as based on an empty tree,
	 * and try to fast-forward to HEAD. This ensures we will not lose
	 * index/worktree changes that the user already made on the unborn
	 * branch.
	 */
	if (checkout_fast_forward(the_repository,
				  the_hash_algo->empty_tree,
				  merge_head, 0))
		return 1;

	if (update_ref("initial pull", "HEAD", merge_head, curr_head, 0, UPDATE_REFS_DIE_ON_ERR))
		return 1;

	return 0;
}

static int rebase_submodules(void)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	cp.git_cmd = 1;
	cp.no_stdin = 1;
	strvec_pushl(&cp.args, "submodule", "update",
		     "--recursive", "--rebase", NULL);
	argv_push_verbosity(&cp.args);

	return run_command(&cp);
}

static int update_submodules(void)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	cp.git_cmd = 1;
	cp.no_stdin = 1;
	strvec_pushl(&cp.args, "submodule", "update",
		     "--recursive", "--checkout", NULL);
	argv_push_verbosity(&cp.args);

	return run_command(&cp);
}

/**
 * Runs git-merge, returning its exit status.
 */
static int run_merge(void)
{
	struct child_process cmd = CHILD_PROCESS_INIT;

	strvec_pushl(&cmd.args, "merge", NULL);

	/* Shared options */
	argv_push_verbosity(&cmd.args);
	if (opt_progress)
		strvec_push(&cmd.args, opt_progress);

	/* Options passed to git-merge */
	if (opt_diffstat)
		strvec_push(&cmd.args, opt_diffstat);
	if (opt_log)
		strvec_push(&cmd.args, opt_log);
	if (opt_signoff)
		strvec_push(&cmd.args, opt_signoff);
	if (opt_squash)
		strvec_push(&cmd.args, opt_squash);
	if (opt_commit)
		strvec_push(&cmd.args, opt_commit);
	if (opt_edit)
		strvec_push(&cmd.args, opt_edit);
	if (cleanup_arg)
		strvec_pushf(&cmd.args, "--cleanup=%s", cleanup_arg);
	if (opt_ff)
		strvec_push(&cmd.args, opt_ff);
	if (opt_verify)
		strvec_push(&cmd.args, opt_verify);
	if (opt_verify_signatures)
		strvec_push(&cmd.args, opt_verify_signatures);
	strvec_pushv(&cmd.args, opt_strategies.v);
	strvec_pushv(&cmd.args, opt_strategy_opts.v);
	if (opt_gpg_sign)
		strvec_push(&cmd.args, opt_gpg_sign);
	if (opt_autostash == 0)
		strvec_push(&cmd.args, "--no-autostash");
	else if (opt_autostash == 1)
		strvec_push(&cmd.args, "--autostash");
	if (opt_allow_unrelated_histories > 0)
		strvec_push(&cmd.args, "--allow-unrelated-histories");

	strvec_push(&cmd.args, "FETCH_HEAD");
	cmd.git_cmd = 1;
	return run_command(&cmd);
}

/**
 * Returns remote's upstream branch for the current branch. If remote is NULL,
 * the current branch's configured default remote is used. Returns NULL if
 * `remote` does not name a valid remote, HEAD does not point to a branch,
 * remote is not the branch's configured remote or the branch does not have any
 * configured upstream branch.
 */
static const char *get_upstream_branch(const char *remote)
{
	struct remote *rm;
	struct branch *curr_branch;
	const char *curr_branch_remote;

	rm = remote_get(remote);
	if (!rm)
		return NULL;

	curr_branch = branch_get("HEAD");
	if (!curr_branch)
		return NULL;

	curr_branch_remote = remote_for_branch(curr_branch, NULL);
	assert(curr_branch_remote);

	if (strcmp(curr_branch_remote, rm->name))
		return NULL;

	return branch_get_upstream(curr_branch, NULL);
}

/**
 * Derives the remote-tracking branch from the remote and refspec.
 *
 * FIXME: The current implementation assumes the default mapping of
 * refs/heads/<branch_name> to refs/remotes/<remote_name>/<branch_name>.
 */
static const char *get_tracking_branch(const char *remote, const char *refspec)
{
	struct refspec_item spec;
	const char *spec_src;
	const char *merge_branch;

	refspec_item_init_or_die(&spec, refspec, REFSPEC_FETCH);
	spec_src = spec.src;
	if (!*spec_src || !strcmp(spec_src, "HEAD"))
		spec_src = "HEAD";
	else if (skip_prefix(spec_src, "heads/", &spec_src))
		;
	else if (skip_prefix(spec_src, "refs/heads/", &spec_src))
		;
	else if (starts_with(spec_src, "refs/") ||
		starts_with(spec_src, "tags/") ||
		starts_with(spec_src, "remotes/"))
		spec_src = "";

	if (*spec_src) {
		if (!strcmp(remote, "."))
			merge_branch = mkpath("refs/heads/%s", spec_src);
		else
			merge_branch = mkpath("refs/remotes/%s/%s", remote, spec_src);
	} else
		merge_branch = NULL;

	refspec_item_clear(&spec);
	return merge_branch;
}

/**
 * Given the repo and refspecs, sets fork_point to the point at which the
 * current branch forked from its remote-tracking branch. Returns 0 on success,
 * -1 on failure.
 */
static int get_rebase_fork_point(struct object_id *fork_point, const char *repo,
		const char *refspec)
{
	int ret;
	struct branch *curr_branch;
	const char *remote_branch;
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf sb = STRBUF_INIT;

	curr_branch = branch_get("HEAD");
	if (!curr_branch)
		return -1;

	if (refspec)
		remote_branch = get_tracking_branch(repo, refspec);
	else
		remote_branch = get_upstream_branch(repo);

	if (!remote_branch)
		return -1;

	strvec_pushl(&cp.args, "merge-base", "--fork-point",
		     remote_branch, curr_branch->name, NULL);
	cp.no_stdin = 1;
	cp.no_stderr = 1;
	cp.git_cmd = 1;

	ret = capture_command(&cp, &sb, GIT_MAX_HEXSZ);
	if (ret)
		goto cleanup;

	ret = get_oid_hex(sb.buf, fork_point);
	if (ret)
		goto cleanup;

cleanup:
	strbuf_release(&sb);
	return ret ? -1 : 0;
}

/**
 * Sets merge_base to the octopus merge base of curr_head, merge_head and
 * fork_point. Returns 0 if a merge base is found, 1 otherwise.
 */
static int get_octopus_merge_base(struct object_id *merge_base,
		const struct object_id *curr_head,
		const struct object_id *merge_head,
		const struct object_id *fork_point)
{
	struct commit_list *revs = NULL, *result;

	commit_list_insert(lookup_commit_reference(the_repository, curr_head),
			   &revs);
	commit_list_insert(lookup_commit_reference(the_repository, merge_head),
			   &revs);
	if (!is_null_oid(fork_point))
		commit_list_insert(lookup_commit_reference(the_repository, fork_point),
				   &revs);

	result = get_octopus_merge_bases(revs);
	free_commit_list(revs);
	reduce_heads_replace(&result);

	if (!result)
		return 1;

	oidcpy(merge_base, &result->item->object.oid);
	free_commit_list(result);
	return 0;
}

/**
 * Given the current HEAD oid, the merge head returned from git-fetch and the
 * fork point calculated by get_rebase_fork_point(), compute the <newbase> and
 * <upstream> arguments to use for the upcoming git-rebase invocation.
 */
static int get_rebase_newbase_and_upstream(struct object_id *newbase,
		struct object_id *upstream,
		const struct object_id *curr_head,
		const struct object_id *merge_head,
		const struct object_id *fork_point)
{
	struct object_id oct_merge_base;

	if (!get_octopus_merge_base(&oct_merge_base, curr_head, merge_head, fork_point))
		if (!is_null_oid(fork_point) && oideq(&oct_merge_base, fork_point))
			fork_point = NULL;

	if (fork_point && !is_null_oid(fork_point))
		oidcpy(upstream, fork_point);
	else
		oidcpy(upstream, merge_head);

	oidcpy(newbase, merge_head);

	return 0;
}

/**
 * Given the <newbase> and <upstream> calculated by
 * get_rebase_newbase_and_upstream(), runs git-rebase with the
 * appropriate arguments and returns its exit status.
 */
static int run_rebase(const struct object_id *newbase,
		const struct object_id *upstream)
{
	struct child_process cmd = CHILD_PROCESS_INIT;

	strvec_push(&cmd.args, "rebase");

	/* Shared options */
	argv_push_verbosity(&cmd.args);

	/* Options passed to git-rebase */
	if (opt_rebase == REBASE_MERGES)
		strvec_push(&cmd.args, "--rebase-merges");
	else if (opt_rebase == REBASE_INTERACTIVE)
		strvec_push(&cmd.args, "--interactive");
	if (opt_diffstat)
		strvec_push(&cmd.args, opt_diffstat);
	strvec_pushv(&cmd.args, opt_strategies.v);
	strvec_pushv(&cmd.args, opt_strategy_opts.v);
	if (opt_gpg_sign)
		strvec_push(&cmd.args, opt_gpg_sign);
	if (opt_signoff)
		strvec_push(&cmd.args, opt_signoff);
	if (opt_autostash == 0)
		strvec_push(&cmd.args, "--no-autostash");
	else if (opt_autostash == 1)
		strvec_push(&cmd.args, "--autostash");
	if (opt_verify_signatures &&
	    !strcmp(opt_verify_signatures, "--verify-signatures"))
		warning(_("ignoring --verify-signatures for rebase"));

	strvec_push(&cmd.args, "--onto");
	strvec_push(&cmd.args, oid_to_hex(newbase));

	strvec_push(&cmd.args, oid_to_hex(upstream));

	cmd.git_cmd = 1;
	return run_command(&cmd);
}

static int get_can_ff(struct object_id *orig_head,
		      struct oid_array *merge_heads)
{
	int ret;
	struct commit_list *list = NULL;
	struct commit *merge_head, *head;
	struct object_id *orig_merge_head;

	if (merge_heads->nr > 1)
		return 0;

	orig_merge_head = &merge_heads->oid[0];
	head = lookup_commit_reference(the_repository, orig_head);
	commit_list_insert(head, &list);
	merge_head = lookup_commit_reference(the_repository, orig_merge_head);
	ret = repo_is_descendant_of(the_repository, merge_head, list);
	free_commit_list(list);
	return ret;
}

/*
 * Is orig_head a descendant of _all_ merge_heads?
 * Unfortunately is_descendant_of() cannot be used as it asks
 * if orig_head is a descendant of at least one of them.
 */
static int already_up_to_date(struct object_id *orig_head,
			      struct oid_array *merge_heads)
{
	int i;
	struct commit *ours;

	ours = lookup_commit_reference(the_repository, orig_head);
	for (i = 0; i < merge_heads->nr; i++) {
		struct commit_list *list = NULL;
		struct commit *theirs;
		int ok;

		theirs = lookup_commit_reference(the_repository, &merge_heads->oid[i]);
		commit_list_insert(theirs, &list);
		ok = repo_is_descendant_of(the_repository, ours, list);
		free_commit_list(list);
		if (!ok)
			return 0;
	}
	return 1;
}

static void show_advice_pull_non_ff(void)
{
	advise(_("You have divergent branches and need to specify how to reconcile them.\n"
		 "You can do so by running one of the following commands sometime before\n"
		 "your next pull:\n"
		 "\n"
		 "  git config pull.rebase false  # merge\n"
		 "  git config pull.rebase true   # rebase\n"
		 "  git config pull.ff only       # fast-forward only\n"
		 "\n"
		 "You can replace \"git config\" with \"git config --global\" to set a default\n"
		 "preference for all repositories. You can also pass --rebase, --no-rebase,\n"
		 "or --ff-only on the command line to override the configured default per\n"
		 "invocation.\n"));
}

int cmd_pull(int argc, const char **argv, const char *prefix)
{
	const char *repo, **refspecs;
	struct oid_array merge_heads = OID_ARRAY_INIT;
	struct object_id orig_head, curr_head;
	struct object_id rebase_fork_point;
	int rebase_unspecified = 0;
	int can_ff;
	int divergent;
	int ret;

	if (!getenv("GIT_REFLOG_ACTION"))
		set_reflog_message(argc, argv);

	git_config(git_pull_config, NULL);
	if (the_repository->gitdir) {
		prepare_repo_settings(the_repository);
		the_repository->settings.command_requires_full_index = 0;
	}

	argc = parse_options(argc, argv, prefix, pull_options, pull_usage, 0);

	if (recurse_submodules_cli != RECURSE_SUBMODULES_DEFAULT)
		recurse_submodules = recurse_submodules_cli;

	if (cleanup_arg)
		/*
		 * this only checks the validity of cleanup_arg; we don't need
		 * a valid value for use_editor
		 */
		get_cleanup_mode(cleanup_arg, 0);

	parse_repo_refspecs(argc, argv, &repo, &refspecs);

	if (!opt_ff) {
		opt_ff = xstrdup_or_null(config_get_ff());
		/*
		 * A subtle point: opt_ff was set on the line above via
		 * reading from config.  opt_rebase, in contrast, is set
		 * before this point via command line options.  The setting
		 * of opt_rebase via reading from config (using
		 * config_get_rebase()) does not happen until later.  We
		 * are relying on the next if-condition happening before
		 * the config_get_rebase() call so that an explicit
		 * "--rebase" can override a config setting of
		 * pull.ff=only.
		 */
		if (opt_rebase >= 0 && opt_ff && !strcmp(opt_ff, "--ff-only"))
			opt_ff = "--ff";
	}

	if (opt_rebase < 0)
		opt_rebase = config_get_rebase(&rebase_unspecified);

	if (repo_read_index_unmerged(the_repository))
		die_resolve_conflict("pull");

	if (file_exists(git_path_merge_head(the_repository)))
		die_conclude_merge();

	if (repo_get_oid(the_repository, "HEAD", &orig_head))
		oidclr(&orig_head);

	if (opt_rebase) {
		if (opt_autostash == -1)
			opt_autostash = config_autostash;

		if (is_null_oid(&orig_head) && !is_index_unborn(&the_index))
			die(_("Updating an unborn branch with changes added to the index."));

		if (!opt_autostash)
			require_clean_work_tree(the_repository,
				N_("pull with rebase"),
				_("please commit or stash them."), 1, 0);

		if (get_rebase_fork_point(&rebase_fork_point, repo, *refspecs))
			oidclr(&rebase_fork_point);
	}

	if (run_fetch(repo, refspecs))
		return 1;

	if (opt_dry_run)
		return 0;

	if (repo_get_oid(the_repository, "HEAD", &curr_head))
		oidclr(&curr_head);

	if (!is_null_oid(&orig_head) && !is_null_oid(&curr_head) &&
			!oideq(&orig_head, &curr_head)) {
		/*
		 * The fetch involved updating the current branch.
		 *
		 * The working tree and the index file are still based on
		 * orig_head commit, but we are merging into curr_head.
		 * Update the working tree to match curr_head.
		 */

		warning(_("fetch updated the current branch head.\n"
			"fast-forwarding your working tree from\n"
			"commit %s."), oid_to_hex(&orig_head));

		if (checkout_fast_forward(the_repository, &orig_head,
					  &curr_head, 0))
			die(_("Cannot fast-forward your working tree.\n"
				"After making sure that you saved anything precious from\n"
				"$ git diff %s\n"
				"output, run\n"
				"$ git reset --hard\n"
				"to recover."), oid_to_hex(&orig_head));
	}

	get_merge_heads(&merge_heads);

	if (!merge_heads.nr)
		die_no_merge_candidates(repo, refspecs);

	if (is_null_oid(&orig_head)) {
		if (merge_heads.nr > 1)
			die(_("Cannot merge multiple branches into empty head."));
		ret = pull_into_void(merge_heads.oid, &curr_head);
		goto cleanup;
	}
	if (merge_heads.nr > 1) {
		if (opt_rebase)
			die(_("Cannot rebase onto multiple branches."));
		if (opt_ff && !strcmp(opt_ff, "--ff-only"))
			die(_("Cannot fast-forward to multiple branches."));
	}

	can_ff = get_can_ff(&orig_head, &merge_heads);
	divergent = !can_ff && !already_up_to_date(&orig_head, &merge_heads);

	/* ff-only takes precedence over rebase */
	if (opt_ff && !strcmp(opt_ff, "--ff-only")) {
		if (divergent)
			die_ff_impossible();
		opt_rebase = REBASE_FALSE;
	}
	/* If no action specified and we can't fast forward, then warn. */
	if (!opt_ff && rebase_unspecified && divergent) {
		show_advice_pull_non_ff();
		die(_("Need to specify how to reconcile divergent branches."));
	}

	if (opt_rebase) {
		struct object_id newbase;
		struct object_id upstream;
		get_rebase_newbase_and_upstream(&newbase, &upstream, &curr_head,
						merge_heads.oid, &rebase_fork_point);

		if ((recurse_submodules == RECURSE_SUBMODULES_ON ||
		     recurse_submodules == RECURSE_SUBMODULES_ON_DEMAND) &&
		    submodule_touches_in_range(the_repository, &upstream, &curr_head))
			die(_("cannot rebase with locally recorded submodule modifications"));

		if (can_ff) {
			/* we can fast-forward this without invoking rebase */
			opt_ff = "--ff-only";
			ret = run_merge();
		} else {
			ret = run_rebase(&newbase, &upstream);
		}

		if (!ret && (recurse_submodules == RECURSE_SUBMODULES_ON ||
			     recurse_submodules == RECURSE_SUBMODULES_ON_DEMAND))
			ret = rebase_submodules();

		goto cleanup;
	} else {
		ret = run_merge();
		if (!ret && (recurse_submodules == RECURSE_SUBMODULES_ON ||
			     recurse_submodules == RECURSE_SUBMODULES_ON_DEMAND))
			ret = update_submodules();
		goto cleanup;
	}

cleanup:
	oid_array_clear(&merge_heads);
	return ret;
}
