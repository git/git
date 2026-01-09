#include "builtin.h"
#include "gettext.h"
#include "parse-options.h"

int cmd_history(int argc,
		const char **argv,
		const char *prefix,
		struct repository *repo UNUSED)
{
	const char * const usage[] = {
		N_("git history [<options>]"),
		NULL,
	};
	struct option options[] = {
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	if (argc)
		usagef("unrecognized argument: %s", argv[0]);
	return 0;
}
