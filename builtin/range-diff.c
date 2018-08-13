#include "cache.h"
#include "builtin.h"
#include "parse-options.h"

static const char * const builtin_range_diff_usage[] = {
N_("git range-diff [<options>] <old-base>..<old-tip> <new-base>..<new-tip>"),
N_("git range-diff [<options>] <old-tip>...<new-tip>"),
N_("git range-diff [<options>] <base> <old-tip> <new-tip>"),
NULL
};

int cmd_range_diff(int argc, const char **argv, const char *prefix)
{
	int creation_factor = 60;
	struct option options[] = {
		OPT_INTEGER(0, "creation-factor", &creation_factor,
			    N_("Percentage by which creation is weighted")),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, options,
			     builtin_range_diff_usage, 0);

	return 0;
}
