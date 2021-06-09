#include "test-tool.h"
#include "submodule-config.h"

static void die_usage(const char **argv, const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "Usage: %s <submodulepath> <config name>\n", argv[0]);
	exit(1);
}

int cmd__submodule_nested_repo_config(int argc, const char **argv)
{
	struct repository subrepo;
	const struct submodule *sub;

	if (argc < 3)
		die_usage(argv, "Wrong number of arguments.");

	setup_git_directory();

	sub = submodule_from_path(the_repository, null_oid(), argv[1]);
	if (repo_submodule_init(&subrepo, the_repository, sub)) {
		die_usage(argv, "Submodule not found.");
	}

	/* Read the config of _child_ submodules. */
	print_config_from_gitmodules(&subrepo, argv[2]);

	submodule_free(the_repository);

	return 0;
}
