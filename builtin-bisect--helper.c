#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "bisect.h"

static const char * const git_bisect_helper_usage[] = {
	"git bisect--helper --next-vars",
	NULL
};

int cmd_bisect__helper(int argc, const char **argv, const char *prefix)
{
	int next_vars = 0;
	struct option options[] = {
		OPT_BOOLEAN(0, "next-vars", &next_vars,
			    "output next bisect step variables"),
		OPT_END()
	};

	argc = parse_options(argc, argv, options, git_bisect_helper_usage, 0);

	if (!next_vars)
		usage_with_options(git_bisect_helper_usage, options);

	/* next-vars */
	return bisect_next_vars(prefix);
}
