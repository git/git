#include "builtin.h"
#include "gettext.h"
#include "parse-options.h"
#include "prune-packed.h"

static const char * const prune_packed_usage[] = {
	"git prune-packed [-n | --dry-run] [-q | --quiet]",
	NULL
};

int cmd_prune_packed(int argc,
		     const char **argv,
		     const char *prefix,
		     struct repository *repo UNUSED)
{
	int opts = isatty(2) ? PRUNE_PACKED_VERBOSE : 0;
	const struct option prune_packed_options[] = {
		OPT_BIT('n', "dry-run", &opts, N_("dry run"),
			PRUNE_PACKED_DRY_RUN),
		OPT_NEGBIT('q', "quiet", &opts, N_("be quiet"),
			   PRUNE_PACKED_VERBOSE),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, prune_packed_options,
			     prune_packed_usage, 0);

	if (argc > 0)
		die(_("'git prune-packed' takes no arguments"));

	prune_packed_objects(opts);
	return 0;
}
