#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "parse-options.h"
#include "midx.h"

static char const * const builtin_multi_pack_index_usage[] = {
	N_("git multi-pack-index [--object-dir=<dir>] (write|verify|expire|repack --batch-size=<size>)"),
	NULL
};

static struct opts_multi_pack_index {
	const char *object_dir;
	unsigned long batch_size;
} opts;

int cmd_multi_pack_index(int argc, const char **argv,
			 const char *prefix)
{
	static struct option builtin_multi_pack_index_options[] = {
		OPT_FILENAME(0, "object-dir", &opts.object_dir,
		  N_("object directory containing set of packfile and pack-index pairs")),
		OPT_MAGNITUDE(0, "batch-size", &opts.batch_size,
		  N_("during repack, collect pack-files of smaller size into a batch that is larger than this size")),
		OPT_END(),
	};

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix,
			     builtin_multi_pack_index_options,
			     builtin_multi_pack_index_usage, 0);

	if (!opts.object_dir)
		opts.object_dir = get_object_directory();

	if (argc == 0)
		usage_with_options(builtin_multi_pack_index_usage,
				   builtin_multi_pack_index_options);

	if (argc > 1) {
		die(_("too many arguments"));
		return 1;
	}

	if (!strcmp(argv[0], "repack"))
		return midx_repack(opts.object_dir, (size_t)opts.batch_size);
	if (opts.batch_size)
		die(_("--batch-size option is only for 'repack' subcommand"));

	if (!strcmp(argv[0], "write"))
		return write_midx_file(opts.object_dir);
	if (!strcmp(argv[0], "verify"))
		return verify_midx_file(opts.object_dir);
	if (!strcmp(argv[0], "expire"))
		return expire_midx_packs(opts.object_dir);

	die(_("unrecognized subcommand: %s"), argv[0]);
}
