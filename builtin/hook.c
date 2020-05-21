#include "cache.h"

#include "builtin.h"
#include "parse-options.h"

static const char * const builtin_hook_usage[] = {
	N_("git hook"),
	NULL
};

int cmd_hook(int argc, const char **argv, const char *prefix)
{
	struct option builtin_hook_options[] = {
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, builtin_hook_options,
			     builtin_hook_usage, 0);

	return 0;
}
