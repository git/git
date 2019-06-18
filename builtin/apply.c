#include "cache.h"
#include "builtin.h"
#include "parse-options.h"
#include "lockfile.h"
#include "apply.h"

static const char * const apply_usage[] = {
	N_("git apply [<options>] [<patch>...]"),
	NULL
};

int cmd_apply(int argc, const char **argv, const char *prefix)
{
	int force_apply = 0;
	int options = 0;
	int ret;
	struct apply_state state;

	if (init_apply_state(&state, the_repository, prefix))
		exit(128);

	argc = apply_parse_options(argc, argv,
				   &state, &force_apply, &options,
				   apply_usage);

	if (check_apply_state(&state, force_apply))
		exit(128);

	ret = apply_all_patches(&state, argc, argv, options);

	clear_apply_state(&state);

	return ret;
}
