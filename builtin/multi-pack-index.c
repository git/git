#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "parse-options.h"

static char const * const builtin_multi_pack_index_usage[] = {
	N_("git multi-pack-index [--object-dir=<dir>]"),
	NULL
};

static struct opts_multi_pack_index {
	const char *object_dir;
} opts;

int cmd_multi_pack_index(int argc, const char **argv,
			 const char *prefix)
{
	static struct option builtin_multi_pack_index_options[] = {
		OPT_FILENAME(0, "object-dir", &opts.object_dir,
		  N_("object directory containing set of packfile and pack-index pairs")),
		OPT_END(),
	};

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix,
			     builtin_multi_pack_index_options,
			     builtin_multi_pack_index_usage, 0);

	if (!opts.object_dir)
		opts.object_dir = get_object_directory();

	return 0;
}
