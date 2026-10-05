#include "builtin.h"
#include "config.h"
#include "diff-hunks.h"
#include "gettext.h"
#include "parse-options.h"
#include "repository.h"

static const char * const diff_hunks_usage[] = {
	N_("git diff-hunks verify"),
	N_("git diff-hunks clear"),
	NULL
};

static int cmd_diff_hunks_verify(int argc, const char **argv,
				 const char *prefix UNUSED,
				 struct repository *r)
{
	struct option options[] = { OPT_END() };

	argc = parse_options(argc, argv, NULL, options, diff_hunks_usage, 0);
	if (argc)
		usage_with_options(diff_hunks_usage, options);
	return diff_hunks_verify(r) ? 1 : 0;
}

static int cmd_diff_hunks_clear(int argc, const char **argv,
				const char *prefix UNUSED,
				struct repository *r)
{
	struct option options[] = { OPT_END() };

	argc = parse_options(argc, argv, NULL, options, diff_hunks_usage, 0);
	if (argc)
		usage_with_options(diff_hunks_usage, options);
	return diff_hunks_clear(r) ? 1 : 0;
}

int cmd_diff_hunks(int argc, const char **argv, const char *prefix,
		   struct repository *repo)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("verify", &fn, cmd_diff_hunks_verify),
		OPT_SUBCOMMAND("clear", &fn, cmd_diff_hunks_clear),
		OPT_END()
	};

	repo_config(repo, git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, options, diff_hunks_usage, 0);

	return fn(argc, argv, prefix, repo);
}
