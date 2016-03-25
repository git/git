#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "bisect.h"

static const char * const git_bisect_helper_usage[] = {
	N_("git bisect--helper --next-all [--no-checkout]"),
	NULL
};

enum sub_commands {
	NEXT_ALL = 1
};

int cmd_bisect__helper(int argc, const char **argv, const char *prefix)
{
	int sub_command = 0;
	int no_checkout = 0;
	struct option options[] = {
		OPT_CMDMODE(0, "next-all", &sub_command,
			 N_("perform 'git bisect next'"), NEXT_ALL),
		OPT_BOOL(0, "no-checkout", &no_checkout,
			 N_("update BISECT_HEAD instead of checking out the current commit")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_bisect_helper_usage, 0);

	if (!sub_command)
		usage_with_options(git_bisect_helper_usage, options);

	switch (sub_command) {
	case NEXT_ALL:
		return bisect_next_all(prefix, no_checkout);
	}
	return 1;
}
