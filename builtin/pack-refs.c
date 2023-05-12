#include "builtin.h"
#include "config.h"
#include "gettext.h"
#include "parse-options.h"
#include "refs.h"
#include "repository.h"
#include "revision.h"

static char const * const pack_refs_usage[] = {
	N_("git pack-refs [--all] [--no-prune] [--exclude <pattern>]"),
	NULL
};

int cmd_pack_refs(int argc, const char **argv, const char *prefix)
{
	unsigned int flags = PACK_REFS_PRUNE;
	static struct ref_exclusions excludes = REF_EXCLUSIONS_INIT;
	struct pack_refs_opts pack_refs_opts = {.exclusions = &excludes, .flags = flags};
	static struct string_list option_excluded_refs = STRING_LIST_INIT_NODUP;
	struct string_list_item *item;

	struct option opts[] = {
		OPT_BIT(0, "all",   &pack_refs_opts.flags, N_("pack everything"), PACK_REFS_ALL),
		OPT_BIT(0, "prune", &pack_refs_opts.flags, N_("prune loose refs (default)"), PACK_REFS_PRUNE),
		OPT_STRING_LIST(0, "exclude", &option_excluded_refs, N_("pattern"),
			N_("references to exclude")),
		OPT_END(),
	};
	git_config(git_default_config, NULL);
	if (parse_options(argc, argv, prefix, opts, pack_refs_usage, 0))
		usage_with_options(pack_refs_usage, opts);

	for_each_string_list_item(item, &option_excluded_refs)
		add_ref_exclusion(pack_refs_opts.exclusions, item->string);

	return refs_pack_refs(get_main_ref_store(the_repository), &pack_refs_opts);
}
