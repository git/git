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

static struct option pull_options[] = {
	/* Shared options */
	OPT__VERBOSITY(&opt_verbosity),
	OPT_PASSTHRU(0, "progress", &opt_progress, NULL,
		N_("force progress reporting"),
		PARSE_OPT_NOARG),

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

	return run_merge();
}
