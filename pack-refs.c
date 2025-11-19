#include "builtin.h"
#include "config.h"
#include "environment.h"
#include "pack-refs.h"
#include "parse-options.h"
#include "refs.h"
#include "revision.h"

int pack_refs_core(int argc,
		   const char **argv,
		   const char *prefix,
		   struct repository *repo,
		   const char * const *usage_opts)
{
	struct ref_exclusions excludes = REF_EXCLUSIONS_INIT;
	struct string_list included_refs = STRING_LIST_INIT_NODUP;
	struct refs_optimize_opts optimize_opts = {
		.exclusions = &excludes,
		.includes = &included_refs,
		.flags = REFS_OPTIMIZE_PRUNE,
	};
	struct string_list option_excluded_refs = STRING_LIST_INIT_NODUP;
	struct string_list_item *item;
	int pack_all = 0;
	int ret;

	struct option opts[] = {
		OPT_BOOL(0, "all",   &pack_all, N_("pack everything")),
		OPT_BIT(0, "prune", &optimize_opts.flags, N_("prune loose refs (default)"), REFS_OPTIMIZE_PRUNE),
		OPT_BIT(0, "auto", &optimize_opts.flags, N_("auto-pack refs as needed"), REFS_OPTIMIZE_AUTO),
		OPT_STRING_LIST(0, "include", optimize_opts.includes, N_("pattern"),
			N_("references to include")),
		OPT_STRING_LIST(0, "exclude", &option_excluded_refs, N_("pattern"),
			N_("references to exclude")),
		OPT_END(),
	};
	repo_config(repo, git_default_config, NULL);
	if (parse_options(argc, argv, prefix, opts, usage_opts, 0))
		usage_with_options(usage_opts, opts);

	for_each_string_list_item(item, &option_excluded_refs)
		add_ref_exclusion(optimize_opts.exclusions, item->string);

	if (pack_all)
		string_list_append(optimize_opts.includes, "*");

	if (!optimize_opts.includes->nr)
		string_list_append(optimize_opts.includes, "refs/tags/*");

	ret = refs_optimize(get_main_ref_store(repo), &optimize_opts);

	clear_ref_exclusions(&excludes);
	string_list_clear(&included_refs, 0);
	string_list_clear(&option_excluded_refs, 0);
	return ret;
}
