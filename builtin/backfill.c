#include "builtin.h"
#include "config.h"
#include "parse-options.h"
#include "repository.h"
#include "object.h"

static const char * const builtin_backfill_usage[] = {
	N_("git backfill [<options>]"),
	NULL
};

int cmd_backfill(int argc, const char **argv, const char *prefix, struct repository *repo)
{
	struct option options[] = {
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_backfill_usage, options);

	argc = parse_options(argc, argv, prefix, options, builtin_backfill_usage,
			     0);

	repo_config(repo, git_default_config, NULL);

	die(_("not implemented"));

	return 0;
}
