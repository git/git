#include "builtin.h"
#include "parse-options.h"

int cmd_walken(int argc, const char **argv, const char *prefix)
{
	const char * const walken_usage[] = {
		N_("git walken"),
		NULL,
	};
	struct option options[] = {
		OPT_END()
	};


	argc = parse_options(argc, argv, prefix, options, walken_usage, 0);

	trace_printf(_("cmd_walken running...\n"));
	return 0;
}
