#include "cache.h"
#include "builtin.h"
#include "parse-options.h"

static const char * const update_server_info_usage[] = {
	"git update-server-info [--force]",
	NULL
};

int cmd_update_server_info(int argc, const char **argv, const char *prefix)
{
	int force = 0;
	struct option options[] = {
		OPT_BOOLEAN('f', "force", &force,
			"update the info files from scratch"),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     update_server_info_usage, 0);
	if (argc > 0)
		usage_with_options(update_server_info_usage, options);

	return !!update_server_info(force);
}
