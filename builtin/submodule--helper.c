#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "quote.h"
#include "pathspec.h"
#include "dir.h"
#include "utf8.h"

struct module_list {
	const struct cache_entry **entries;
	int alloc, nr;
};
#define MODULE_LIST_INIT { NULL, 0, 0 }

static int module_list_compute(int argc, const char **argv,
			       const char *prefix,
			       struct pathspec *pathspec,
			       struct module_list *list)
{
	int i, result = 0;
	char *max_prefix, *ps_matched = NULL;
	int max_prefix_len;
	parse_pathspec(pathspec, 0,
		       PATHSPEC_PREFER_FULL |
		       PATHSPEC_STRIP_SUBMODULE_SLASH_CHEAP,
		       prefix, argv);

	/* Find common prefix for all pathspec's */
	max_prefix = common_prefix(pathspec);
	max_prefix_len = max_prefix ? strlen(max_prefix) : 0;

	if (pathspec->nr)
		ps_matched = xcalloc(pathspec->nr, 1);

	if (read_cache() < 0)
		die(_("index file corrupt"));

	for (i = 0; i < active_nr; i++) {
		const struct cache_entry *ce = active_cache[i];

		if (!S_ISGITLINK(ce->ce_mode) ||
		    !match_pathspec(pathspec, ce->name, ce_namelen(ce),
				    max_prefix_len, ps_matched, 1))
			continue;

		ALLOC_GROW(list->entries, list->nr + 1, list->alloc);
		list->entries[list->nr++] = ce;
		while (i + 1 < active_nr &&
		       !strcmp(ce->name, active_cache[i + 1]->name))
			/*
			 * Skip entries with the same name in different stages
			 * to make sure an entry is returned only once.
			 */
			i++;
	}
	free(max_prefix);

	if (ps_matched && report_path_error(ps_matched, pathspec, prefix))
		result = -1;

	free(ps_matched);

	return result;
}

static int module_list(int argc, const char **argv, const char *prefix)
{
	int i;
	struct pathspec pathspec;
	struct module_list list = MODULE_LIST_INIT;

	struct option module_list_options[] = {
		OPT_STRING(0, "prefix", &prefix,
			   N_("path"),
			   N_("alternative anchor for relative paths")),
		OPT_END()
	};

	const char *const git_submodule_helper_usage[] = {
		N_("git submodule--helper list [--prefix=<path>] [<path>...]"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, module_list_options,
			     git_submodule_helper_usage, 0);

	if (module_list_compute(argc, argv, prefix, &pathspec, &list) < 0) {
		printf("#unmatched\n");
		return 1;
	}

	for (i = 0; i < list.nr; i++) {
		const struct cache_entry *ce = list.entries[i];

		if (ce_stage(ce))
			printf("%06o %s U\t", ce->ce_mode, sha1_to_hex(null_sha1));
		else
			printf("%06o %s %d\t", ce->ce_mode, sha1_to_hex(ce->sha1), ce_stage(ce));

		utf8_fprintf(stdout, "%s\n", ce->name);
	}
	return 0;
}


struct cmd_struct {
	const char *cmd;
	int (*fn)(int, const char **, const char *);
};

static struct cmd_struct commands[] = {
	{"list", module_list},
};

int cmd_submodule__helper(int argc, const char **argv, const char *prefix)
{
	int i;
	if (argc < 2)
		die(_("fatal: submodule--helper subcommand must be "
		      "called with a subcommand"));

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		if (!strcmp(argv[1], commands[i].cmd))
			return commands[i].fn(argc - 1, argv + 1, prefix);

	die(_("fatal: '%s' is not a valid submodule--helper "
	      "subcommand"), argv[1]);
}
