#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "gettext.h"
#include "hash.h"
#include "apply.h"

static const char * const apply_usage[] = {
	N_("git apply [<options>] [<patch>...]"),
	NULL
};

int cmd_apply(int argc,
	      const char **argv,
	      const char *prefix,
	      struct repository *repo UNUSED)
{
	int force_apply = 0;
	int options = 0;
	int ret;
	struct apply_state state;

	if (init_apply_state(&state, the_repository, prefix))
		exit(128);

	/*
	 * We could to redo the "apply.c" machinery to make this
	 * arbitrary fallback unnecessary, but it is dubious that it
	 * is worth the effort.
	 * cf. https://lore.kernel.org/git/xmqqcypfcmn4.fsf@gitster.g/
	 */
	if (!the_hash_algo)
		repo_set_hash_algo(the_repository, GIT_HASH_SHA1);

	argc = apply_parse_options(argc, argv,
				   &state, &force_apply, &options,
				   apply_usage);

	if (check_apply_state(&state, force_apply))
		exit(128);

	ret = apply_all_patches(&state, argc, argv, options);

	clear_apply_state(&state);

	return ret;
}
