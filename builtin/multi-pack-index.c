#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "abspath.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "parse-options.h"
#include "midx.h"
#include "strbuf.h"
#include "trace2.h"
#include "odb.h"
#include "replace-object.h"
#include "repository.h"

#define BUILTIN_MIDX_WRITE_USAGE \
	N_("git multi-pack-index [<options>] write [--preferred-pack=<pack>]\n" \
	   "  [--[no-]bitmap] [--[no-]incremental] [--[no-]stdin-packs]\n" \
	   "  [--refs-snapshot=<path>]")

#define BUILTIN_MIDX_COMPACT_USAGE \
	N_("git multi-pack-index [<options>] compact [--[no-]incremental]\n" \
	   "  [--[no-]bitmap] <from> <to>")

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
static char const * const builtin_multi_pack_index_compact_usage[] = {
	BUILTIN_MIDX_COMPACT_USAGE,
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
	BUILTIN_MIDX_COMPACT_USAGE,
	BUILTIN_MIDX_VERIFY_USAGE,
	BUILTIN_MIDX_EXPIRE_USAGE,
	BUILTIN_MIDX_REPACK_USAGE,
	NULL
};

static struct opts_multi_pack_index {
	char *object_dir;
	const char *preferred_pack;
	char *refs_snapshot;
	unsigned long batch_size;
	unsigned flags;
	int stdin_packs;
} opts;


static int parse_object_dir(const struct option *opt, const char *arg,
			    int unset)
{
	char **value = opt->value;
	free(*value);
	if (unset)
		*value = xstrdup(the_repository->objects->sources->path);
	else
		*value = real_pathdup(arg, 1);
	return 0;
}

static struct odb_source *handle_object_dir_option(struct repository *repo)
{
	struct odb_source *source = odb_find_source(repo->objects, opts.object_dir);
	if (!source)
		source = odb_add_to_alternates_memory(repo->objects, opts.object_dir);
	return source;
}

static struct option common_opts[] = {
	OPT_CALLBACK(0, "object-dir", &opts.object_dir,
	  N_("directory"),
	  N_("object directory containing set of packfile and pack-index pairs"),
	  parse_object_dir),
	OPT_BIT(0, "progress", &opts.flags, N_("force progress reporting"),
		MIDX_PROGRESS),
	OPT_END(),
};

static struct option *add_common_options(struct option *prev)
{
	return parse_options_concat(common_opts, prev);
}

static int git_multi_pack_index_write_config(const char *var, const char *value,
					     const struct config_context *ctx UNUSED,
					     void *cb UNUSED)
{
	if (!strcmp(var, "pack.writebitmaphashcache")) {
		if (git_config_bool(var, value))
			opts.flags |= MIDX_WRITE_BITMAP_HASH_CACHE;
		else
			opts.flags &= ~MIDX_WRITE_BITMAP_HASH_CACHE;
	}

	if (!strcmp(var, "pack.writebitmaplookuptable")) {
		if (git_config_bool(var, value))
			opts.flags |= MIDX_WRITE_BITMAP_LOOKUP_TABLE;
		else
			opts.flags &= ~MIDX_WRITE_BITMAP_LOOKUP_TABLE;
	}

	/*
	 * We should never make a fall-back call to 'git_default_config', since
	 * this was already called in 'cmd_multi_pack_index()'.
	 */
	return 0;
}

static void read_packs_from_stdin(struct string_list *to)
{
	struct strbuf buf = STRBUF_INIT;
	while (strbuf_getline(&buf, stdin) != EOF)
		string_list_append(to, buf.buf);
	string_list_sort(to);

	strbuf_release(&buf);
}

static int cmd_multi_pack_index_write(int argc, const char **argv,
				      const char *prefix,
				      struct repository *repo)
{
	struct option *options;
	static struct option builtin_multi_pack_index_write_options[] = {
		OPT_STRING(0, "preferred-pack", &opts.preferred_pack,
			   N_("preferred-pack"),
			   N_("pack for reuse when computing a multi-pack bitmap")),
		OPT_BIT(0, "bitmap", &opts.flags, N_("write multi-pack bitmap"),
			MIDX_WRITE_BITMAP | MIDX_WRITE_REV_INDEX),
		OPT_BIT(0, "incremental", &opts.flags,
			N_("write a new incremental MIDX"), MIDX_WRITE_INCREMENTAL),
		OPT_BOOL(0, "stdin-packs", &opts.stdin_packs,
			 N_("write multi-pack index containing only given indexes")),
		OPT_FILENAME(0, "refs-snapshot", &opts.refs_snapshot,
			     N_("refs snapshot for selecting bitmap commits")),
		OPT_END(),
	};
	struct odb_source *source;
	int ret;

	opts.flags |= MIDX_WRITE_BITMAP_HASH_CACHE;

	repo_config(the_repository, git_multi_pack_index_write_config, NULL);

	options = add_common_options(builtin_multi_pack_index_write_options);

	trace2_cmd_mode(argv[0]);

	if (isatty(2))
		opts.flags |= MIDX_PROGRESS;
	argc = parse_options(argc, argv, prefix,
			     options, builtin_multi_pack_index_write_usage,
			     0);
	if (argc)
		usage_with_options(builtin_multi_pack_index_write_usage,
				   options);
	source = handle_object_dir_option(repo);

	FREE_AND_NULL(options);

	if (opts.stdin_packs) {
		struct string_list packs = STRING_LIST_INIT_DUP;

		read_packs_from_stdin(&packs);

		ret = write_midx_file_only(source, &packs,
					   opts.preferred_pack,
					   opts.refs_snapshot, opts.flags);

		string_list_clear(&packs, 0);
		free(opts.refs_snapshot);

		return ret;

	}

	ret = write_midx_file(source, opts.preferred_pack,
			      opts.refs_snapshot, opts.flags);

	free(opts.refs_snapshot);
	return ret;
}

static int cmd_multi_pack_index_compact(int argc, const char **argv,
					const char *prefix,
					struct repository *repo)
{
	struct multi_pack_index *m, *cur;
	struct multi_pack_index *from_midx = NULL;
	struct multi_pack_index *to_midx = NULL;
	struct odb_source *source;
	int ret;

	struct option *options;
	static struct option builtin_multi_pack_index_compact_options[] = {
		OPT_BIT(0, "bitmap", &opts.flags, N_("write multi-pack bitmap"),
			MIDX_WRITE_BITMAP | MIDX_WRITE_REV_INDEX),
		OPT_BIT(0, "incremental", &opts.flags,
			N_("write a new incremental MIDX"), MIDX_WRITE_INCREMENTAL),
		OPT_END(),
	};

	repo_config(repo, git_multi_pack_index_write_config, NULL);

	options = add_common_options(builtin_multi_pack_index_compact_options);

	trace2_cmd_mode(argv[0]);

	if (isatty(2))
		opts.flags |= MIDX_PROGRESS;
	argc = parse_options(argc, argv, prefix,
			     options, builtin_multi_pack_index_compact_usage,
			     0);

	if (argc != 2)
		usage_with_options(builtin_multi_pack_index_compact_usage,
				   options);
	source = handle_object_dir_option(the_repository);

	FREE_AND_NULL(options);

	m = get_multi_pack_index(source);

	for (cur = m; cur && !(from_midx && to_midx); cur = cur->base_midx) {
		const char *midx_csum = midx_get_checksum_hex(cur);

		if (!from_midx && !strcmp(midx_csum, argv[0]))
			from_midx = cur;
		if (!to_midx && !strcmp(midx_csum, argv[1]))
			to_midx = cur;
	}

	if (!from_midx)
		die(_("could not find MIDX: %s"), argv[0]);
	if (!to_midx)
		die(_("could not find MIDX: %s"), argv[1]);
	if (from_midx == to_midx)
		die(_("MIDX compaction endpoints must be unique"));

	for (m = from_midx; m; m = m->base_midx) {
		if (m == to_midx)
			die(_("MIDX %s must be an ancestor of %s"), argv[0], argv[1]);
	}

	ret = write_midx_file_compact(source, from_midx, to_midx, opts.flags);

	return ret;
}

static int cmd_multi_pack_index_verify(int argc, const char **argv,
				       const char *prefix,
				       struct repository *repo UNUSED)
{
	struct option *options;
	static struct option builtin_multi_pack_index_verify_options[] = {
		OPT_END(),
	};
	struct odb_source *source;

	options = add_common_options(builtin_multi_pack_index_verify_options);

	trace2_cmd_mode(argv[0]);

	if (isatty(2))
		opts.flags |= MIDX_PROGRESS;
	argc = parse_options(argc, argv, prefix,
			     options, builtin_multi_pack_index_verify_usage,
			     0);
	if (argc)
		usage_with_options(builtin_multi_pack_index_verify_usage,
				   options);
	source = handle_object_dir_option(the_repository);

	FREE_AND_NULL(options);

	return verify_midx_file(source, opts.flags);
}

static int cmd_multi_pack_index_expire(int argc, const char **argv,
				       const char *prefix,
				       struct repository *repo UNUSED)
{
	struct option *options;
	static struct option builtin_multi_pack_index_expire_options[] = {
		OPT_END(),
	};
	struct odb_source *source;

	options = add_common_options(builtin_multi_pack_index_expire_options);

	trace2_cmd_mode(argv[0]);

	if (isatty(2))
		opts.flags |= MIDX_PROGRESS;
	argc = parse_options(argc, argv, prefix,
			     options, builtin_multi_pack_index_expire_usage,
			     0);
	if (argc)
		usage_with_options(builtin_multi_pack_index_expire_usage,
				   options);
	source = handle_object_dir_option(the_repository);

	FREE_AND_NULL(options);

	return expire_midx_packs(source, opts.flags);
}

static int cmd_multi_pack_index_repack(int argc, const char **argv,
				       const char *prefix,
				       struct repository *repo UNUSED)
{
	struct option *options;
	static struct option builtin_multi_pack_index_repack_options[] = {
		OPT_UNSIGNED(0, "batch-size", &opts.batch_size,
		  N_("during repack, collect pack-files of smaller size into a batch that is larger than this size")),
		OPT_END(),
	};
	struct odb_source *source;

	options = add_common_options(builtin_multi_pack_index_repack_options);

	trace2_cmd_mode(argv[0]);

	if (isatty(2))
		opts.flags |= MIDX_PROGRESS;
	argc = parse_options(argc, argv, prefix,
			     options,
			     builtin_multi_pack_index_repack_usage,
			     0);
	if (argc)
		usage_with_options(builtin_multi_pack_index_repack_usage,
				   options);
	source = handle_object_dir_option(the_repository);

	FREE_AND_NULL(options);

	return midx_repack(source, (size_t)opts.batch_size, opts.flags);
}

int cmd_multi_pack_index(int argc,
			 const char **argv,
			 const char *prefix,
			 struct repository *repo)
{
	int res;
	parse_opt_subcommand_fn *fn = NULL;
	struct option builtin_multi_pack_index_options[] = {
		OPT_SUBCOMMAND("repack", &fn, cmd_multi_pack_index_repack),
		OPT_SUBCOMMAND("write", &fn, cmd_multi_pack_index_write),
		OPT_SUBCOMMAND("compact", &fn, cmd_multi_pack_index_compact),
		OPT_SUBCOMMAND("verify", &fn, cmd_multi_pack_index_verify),
		OPT_SUBCOMMAND("expire", &fn, cmd_multi_pack_index_expire),
		OPT_END(),
	};
	struct option *options = parse_options_concat(builtin_multi_pack_index_options, common_opts);

	disable_replace_refs();

	repo_config(the_repository, git_default_config, NULL);

	if (the_repository &&
	    the_repository->objects &&
	    the_repository->objects->sources)
		opts.object_dir = xstrdup(the_repository->objects->sources->path);

	argc = parse_options(argc, argv, prefix, options,
			     builtin_multi_pack_index_usage, 0);
	FREE_AND_NULL(options);

	res = fn(argc, argv, prefix, repo);

	free(opts.object_dir);
	return res;
}
