#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "config.h"
#include "gettext.h"
#include "parse-options.h"
#include "refs.h"
#include "revision.h"

static char const * const pack_refs_usage[] = {
	N_("git pack-refs [--all] [--no-prune] [--auto] [--include <pattern>] [--exclude <pattern>]"),
	NULL
};

int cmd_pack_refs(int argc,
		  const char **argv,
		  const char *prefix,
		  struct repository *repo UNUSED)
{
	struct ref_exclusions excludes = REF_EXCLUSIONS_INIT;
	struct string_list included_refs = STRING_LIST_INIT_NODUP;
	struct pack_refs_opts pack_refs_opts = {
		.exclusions = &excludes,
		.includes = &included_refs,
		.flags = PACK_REFS_PRUNE,
	};
	struct string_list option_excluded_refs = STRING_LIST_INIT_NODUP;
	struct string_list_item *item;
	int pack_all = 0;
	int ret;

	struct option opts[] = {
		OPT_BOOL(0, "all",   &pack_all, N_("pack everything")),
		OPT_BIT(0, "prune", &pack_refs_opts.flags, N_("prune loose refs (default)"), PACK_REFS_PRUNE),
		OPT_BIT(0, "auto", &pack_refs_opts.flags, N_("auto-pack refs as needed"), PACK_REFS_AUTO),
		OPT_STRING_LIST(0, "include", pack_refs_opts.includes, N_("pattern"),
			N_("references to include")),
		OPT_STRING_LIST(0, "exclude", &option_excluded_refs, N_("pattern"),
			N_("references to exclude")),
		OPT_END(),
	};
	git_config(git_default_config, NULL);
	if (parse_options(argc, argv, prefix, opts, pack_refs_usage, 0))
		usage_with_options(pack_refs_usage, opts);

	for_each_string_list_item(item, &option_excluded_refs)
		add_ref_exclusion(pack_refs_opts.exclusions, item->string);

	if (pack_all)
		string_list_append(pack_refs_opts.includes, "*");

	if (!pack_refs_opts.includes->nr)
		string_list_append(pack_refs_opts.includes, "refs/tags/*");

	ret = refs_pack_refs(get_main_ref_store(the_repository), &pack_refs_opts);

	clear_ref_exclusions(&excludes);
	string_list_clear(&included_refs, 0);
	string_list_clear(&option_excluded_refs, 0);
	return ret;
}
