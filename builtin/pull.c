/*
 * Builtin "git pull"
 *
 * Based on git-pull.sh by Junio C Hamano
 *
 * Fetch one or more remote refs and merge it/them into the current HEAD.
 */
#include "cache.h"
#include "builtin.h"
#include "parse-options.h"
#include "exec_cmd.h"
#include "run-command.h"

static const char * const pull_usage[] = {
	N_("git pull [options] [<repository> [<refspec>...]]"),
	NULL
};

/* Shared options */
static int opt_verbosity;
static char *opt_progress;

/* Options passed to git-merge */
static char *opt_diffstat;
static char *opt_log;
static char *opt_squash;
static char *opt_commit;
static char *opt_edit;
static char *opt_ff;
static char *opt_verify_signatures;
static struct argv_array opt_strategies = ARGV_ARRAY_INIT;
static struct argv_array opt_strategy_opts = ARGV_ARRAY_INIT;
static char *opt_gpg_sign;

/* Options passed to git-fetch */
static char *opt_all;
static char *opt_append;
static char *opt_upload_pack;
static int opt_force;
static char *opt_tags;
static char *opt_prune;
static char *opt_recurse_submodules;
static int opt_dry_run;
static char *opt_keep;
static char *opt_depth;
static char *opt_unshallow;
static char *opt_update_shallow;
static char *opt_refmap;

static struct option pull_options[] = {
	/* Shared options */
	OPT__VERBOSITY(&opt_verbosity),
	OPT_PASSTHRU(0, "progress", &opt_progress, NULL,
		N_("force progress reporting"),
		PARSE_OPT_NOARG),

	/* Options passed to git-merge */
	OPT_GROUP(N_("Options related to merging")),
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
	OPT_PASSTHRU(0, "squash", &opt_squash, NULL,
		N_("create a single commit instead of doing a merge"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "commit", &opt_commit, NULL,
		N_("perform a commit if the merge succeeds (default)"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "edit", &opt_edit, NULL,
		N_("edit message before committing"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "ff", &opt_ff, NULL,
		N_("allow fast-forward"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "ff-only", &opt_ff, NULL,
		N_("abort if fast-forward is not possible"),
		PARSE_OPT_NOARG | PARSE_OPT_NONEG),
	OPT_PASSTHRU(0, "verify-signatures", &opt_verify_signatures, NULL,
		N_("verify that the named commit has a valid GPG signature"),
		PARSE_OPT_NOARG),
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
	OPT__FORCE(&opt_force, N_("force overwrite of local branch")),
	OPT_PASSTHRU('t', "tags", &opt_tags, NULL,
		N_("fetch all tags and associated objects"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU('p', "prune", &opt_prune, NULL,
		N_("prune remote-tracking branches no longer on remote"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "recurse-submodules", &opt_recurse_submodules,
		N_("on-demand"),
		N_("control recursive fetching of submodules"),
		PARSE_OPT_OPTARG),
	OPT_BOOL(0, "dry-run", &opt_dry_run,
		N_("dry run")),
	OPT_PASSTHRU('k', "keep", &opt_keep, NULL,
		N_("keep downloaded pack"),
		PARSE_OPT_NOARG),
	OPT_PASSTHRU(0, "depth", &opt_depth, N_("depth"),
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

	OPT_END()
};

/**
 * Pushes "-q" or "-v" switches into arr to match the opt_verbosity level.
 */
static void argv_push_verbosity(struct argv_array *arr)
{
	int verbosity;

	for (verbosity = opt_verbosity; verbosity > 0; verbosity--)
		argv_array_push(arr, "-v");

	for (verbosity = opt_verbosity; verbosity < 0; verbosity++)
		argv_array_push(arr, "-q");
}

/**
 * Pushes "-f" switches into arr to match the opt_force level.
 */
static void argv_push_force(struct argv_array *arr)
{
	int force = opt_force;
	while (force-- > 0)
		argv_array_push(arr, "-f");
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
	struct argv_array args = ARGV_ARRAY_INIT;
	int ret;

	argv_array_pushl(&args, "fetch", "--update-head-ok", NULL);

	/* Shared options */
	argv_push_verbosity(&args);
	if (opt_progress)
		argv_array_push(&args, opt_progress);

	/* Options passed to git-fetch */
	if (opt_all)
		argv_array_push(&args, opt_all);
	if (opt_append)
		argv_array_push(&args, opt_append);
	if (opt_upload_pack)
		argv_array_push(&args, opt_upload_pack);
	argv_push_force(&args);
	if (opt_tags)
		argv_array_push(&args, opt_tags);
	if (opt_prune)
		argv_array_push(&args, opt_prune);
	if (opt_recurse_submodules)
		argv_array_push(&args, opt_recurse_submodules);
	if (opt_dry_run)
		argv_array_push(&args, "--dry-run");
	if (opt_keep)
		argv_array_push(&args, opt_keep);
	if (opt_depth)
		argv_array_push(&args, opt_depth);
	if (opt_unshallow)
		argv_array_push(&args, opt_unshallow);
	if (opt_update_shallow)
		argv_array_push(&args, opt_update_shallow);
	if (opt_refmap)
		argv_array_push(&args, opt_refmap);

	if (repo) {
		argv_array_push(&args, repo);
		argv_array_pushv(&args, refspecs);
	} else if (*refspecs)
		die("BUG: refspecs without repo?");
	ret = run_command_v_opt(args.argv, RUN_GIT_CMD);
	argv_array_clear(&args);
	return ret;
}

/**
 * Runs git-merge, returning its exit status.
 */
static int run_merge(void)
{
	int ret;
	struct argv_array args = ARGV_ARRAY_INIT;

	argv_array_pushl(&args, "merge", NULL);

	/* Shared options */
	argv_push_verbosity(&args);
	if (opt_progress)
		argv_array_push(&args, opt_progress);

	/* Options passed to git-merge */
	if (opt_diffstat)
		argv_array_push(&args, opt_diffstat);
	if (opt_log)
		argv_array_push(&args, opt_log);
	if (opt_squash)
		argv_array_push(&args, opt_squash);
	if (opt_commit)
		argv_array_push(&args, opt_commit);
	if (opt_edit)
		argv_array_push(&args, opt_edit);
	if (opt_ff)
		argv_array_push(&args, opt_ff);
	if (opt_verify_signatures)
		argv_array_push(&args, opt_verify_signatures);
	argv_array_pushv(&args, opt_strategies.argv);
	argv_array_pushv(&args, opt_strategy_opts.argv);
	if (opt_gpg_sign)
		argv_array_push(&args, opt_gpg_sign);

	argv_array_push(&args, "FETCH_HEAD");
	ret = run_command_v_opt(args.argv, RUN_GIT_CMD);
	argv_array_clear(&args);
	return ret;
}

int cmd_pull(int argc, const char **argv, const char *prefix)
{
	const char *repo, **refspecs;

	if (!getenv("_GIT_USE_BUILTIN_PULL")) {
		const char *path = mkpath("%s/git-pull", git_exec_path());

		if (sane_execvp(path, (char **)argv) < 0)
			die_errno("could not exec %s", path);
	}

	argc = parse_options(argc, argv, prefix, pull_options, pull_usage, 0);

	parse_repo_refspecs(argc, argv, &repo, &refspecs);

	if (run_fetch(repo, refspecs))
		return 1;

	if (opt_dry_run)
		return 0;

	return run_merge();
}
