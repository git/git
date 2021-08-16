#include "test-tool.h"
#include "cache.h"
#include "config.h"
#include "blob.h"
#include "commit.h"
#include "tree.h"
#include "sparse-index.h"
#include "parse-options.h"

static const char *read_cache_usage[] = {
	"test-tool read-cache [<options>...]",
	NULL
};

static void print_cache_entry(struct cache_entry *ce)
{
	const char *type;
	printf("%06o ", ce->ce_mode & 0177777);

	if (S_ISSPARSEDIR(ce->ce_mode))
		type = tree_type;
	else if (S_ISGITLINK(ce->ce_mode))
		type = commit_type;
	else
		type = blob_type;

	printf("%s %s\t%s\n",
	       type,
	       oid_to_hex(&ce->oid),
	       ce->name);
}

static void print_cache(struct index_state *istate)
{
	int i;
	for (i = 0; i < istate->cache_nr; i++)
		print_cache_entry(istate->cache[i]);
}

int cmd__read_cache(int argc, const char **argv)
{
	struct repository *r = the_repository;
	int table = 0, expand = 0;
	struct option options[] = {
		OPT_BOOL(0, "table", &table,
			 "print a dump of the cache"),
		OPT_BOOL(0, "expand", &expand,
			 "call ensure_full_index()"),
		OPT_END()
	};

	argc = parse_options(argc, argv, "test-tools", options, read_cache_usage, 0);
	if (argc > 0)
		usage_msg_opt("Too many arguments.", read_cache_usage, options);

	initialize_the_repository();
	prepare_repo_settings(r);
	r->settings.command_requires_full_index = 0;

	setup_git_directory();
	git_config(git_default_config, NULL);
	repo_read_index(r);

	if (expand)
		ensure_full_index(r->index);

	if (table)
		print_cache(r->index);
	discard_index(r->index);

	return 0;
}
