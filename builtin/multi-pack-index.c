#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "parse-options.h"
#include "midx.h"
#include "trace2.h"
#include "object-store.h"

#define BUILTIN_MIDX_WRITE_USAGE \
	N_("git multi-pack-index [<options>] write [--preferred-pack=<pack>]")

#define BUILTIN_MIDX_VERIFY_USAGE \
	N_("git multi-pack-index [<options>] verify")

#define BUILTIN_MIDX_EXPIRE_USAGE \
	N_("git multi-pack-index [<options>] expire")

#define BUILTIN_MIDX_REPACK_USAGE \
	N_("git multi-pack-index [<options>] repack [--batch-size=<size>]")

static char const * const builtin_multi_pack_index_write_usage[] = {
	BUILTIN_MIDX_WRITE_USAGE,
	NULL
};
static char const * const builtin_multi_pack_index_verify_usage[] = {
	BUILTIN_MIDX_VERIFY_USAGE,
	NULL
};
static char const * const builtin_multi_pack_index_expire_usage[] = {
	BUILTIN_MIDX_EXPIRE_USAGE,
	NULL
};
static char const * const builtin_multi_pack_index_repack_usage[] = {
	BUILTIN_MIDX_REPACK_USAGE,
	NULL
};
static char const * const builtin_multi_pack_index_usage[] = {
	BUILTIN_MIDX_WRITE_USAGE,
	BUILTIN_MIDX_VERIFY_USAGE,
	BUILTIN_MIDX_EXPIRE_USAGE,
	BUILTIN_MIDX_REPACK_USAGE,
	NULL
};

static struct opts_multi_pack_index {
	const char *object_dir;
	const char *preferred_pack;
	unsigned long batch_size;
	unsigned flags;
} opts;

static struct option common_opts[] = {
	OPT_FILENAME(0, "object-dir", &opts.object_dir,
	  N_("object directory containing set of packfile and pack-index pairs")),
	OPT_BIT(0, "progress", &opts.flags, N_("force progress reporting"), MIDX_PROGRESS),
	OPT_END(),
};

static struct option *add_common_options(struct option *prev)
{
	return parse_options_concat(common_opts, prev);
}

static int cmd_multi_pack_index_write(int argc, const char **argv)
{
	struct option *options;
	static struct option builtin_multi_pack_index_write_options[] = {
		OPT_STRING(0, "preferred-pack", &opts.preferred_pack,
			   N_("preferred-pack"),
			   N_("pack for reuse when computing a multi-pack bitmap")),
		OPT_BIT(0, "bitmap", &opts.flags, N_("write multi-pack bitmap"),
			MIDX_WRITE_BITMAP | MIDX_WRITE_REV_INDEX),
		OPT_END(),
	};

	options = add_common_options(builtin_multi_pack_index_write_options);

	trace2_cmd_mode(argv[0]);

	argc = parse_options(argc, argv, NULL,
			     options, builtin_multi_pack_index_write_usage,
			     PARSE_OPT_KEEP_UNKNOWN);
	if (argc)
		usage_with_options(builtin_multi_pack_index_write_usage,
				   options);

	FREE_AND_NULL(options);

	return write_midx_file(opts.object_dir, opts.preferred_pack,
			       opts.flags);
}

static int cmd_multi_pack_index_verify(int argc, const char **argv)
{
	struct option *options = common_opts;

	trace2_cmd_mode(argv[0]);

	argc = parse_options(argc, argv, NULL,
			     options, builtin_multi_pack_index_verify_usage,
			     PARSE_OPT_KEEP_UNKNOWN);
	if (argc)
		usage_with_options(builtin_multi_pack_index_verify_usage,
				   options);

	return verify_midx_file(the_repository, opts.object_dir, opts.flags);
}

static int cmd_multi_pack_index_expire(int argc, const char **argv)
{
	struct option *options = common_opts;

	trace2_cmd_mode(argv[0]);

	argc = parse_options(argc, argv, NULL,
			     options, builtin_multi_pack_index_expire_usage,
			     PARSE_OPT_KEEP_UNKNOWN);
	if (argc)
		usage_with_options(builtin_multi_pack_index_expire_usage,
				   options);

	return expire_midx_packs(the_repository, opts.object_dir, opts.flags);
}

static int cmd_multi_pack_index_repack(int argc, const char **argv)
{
	struct option *options;
	static struct option builtin_multi_pack_index_repack_options[] = {
		OPT_MAGNITUDE(0, "batch-size", &opts.batch_size,
		  N_("during repack, collect pack-files of smaller size into a batch that is larger than this size")),
		OPT_END(),
	};

	options = add_common_options(builtin_multi_pack_index_repack_options);

	trace2_cmd_mode(argv[0]);

	argc = parse_options(argc, argv, NULL,
			     options,
			     builtin_multi_pack_index_repack_usage,
			     PARSE_OPT_KEEP_UNKNOWN);
	if (argc)
		usage_with_options(builtin_multi_pack_index_repack_usage,
				   options);

	FREE_AND_NULL(options);

	return midx_repack(the_repository, opts.object_dir,
			   (size_t)opts.batch_size, opts.flags);
}

int cmd_multi_pack_index(int argc, const char **argv,
			 const char *prefix)
{
	struct option *builtin_multi_pack_index_options = common_opts;

	git_config(git_default_config, NULL);

	if (isatty(2))
		opts.flags |= MIDX_PROGRESS;
	argc = parse_options(argc, argv, prefix,
			     builtin_multi_pack_index_options,
			     builtin_multi_pack_index_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (!opts.object_dir)
		opts.object_dir = get_object_directory();

	if (!argc)
		goto usage;

	if (!strcmp(argv[0], "repack"))
		return cmd_multi_pack_index_repack(argc, argv);
	else if (!strcmp(argv[0], "write"))
		return cmd_multi_pack_index_write(argc, argv);
	else if (!strcmp(argv[0], "verify"))
		return cmd_multi_pack_index_verify(argc, argv);
	else if (!strcmp(argv[0], "expire"))
		return cmd_multi_pack_index_expire(argc, argv);

	error(_("unrecognized subcommand: %s"), argv[0]);
usage:
	usage_with_options(builtin_multi_pack_index_usage,
			   builtin_multi_pack_index_options);
}
