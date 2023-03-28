#include "test-tool.h"
#include "cache.h"
#include "config.h"
#include "submodule-config.h"
#include "submodule.h"

static void die_usage(int argc, const char **argv, const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "Usage: %s [<commit> <submodulepath>] ...\n", argv[0]);
	exit(1);
}

int cmd__submodule_config(int argc, const char **argv)
{
	const char **arg = argv;
	int my_argc = argc;
	int lookup_name = 0;

	arg++;
	my_argc--;
	while (arg[0] && starts_with(arg[0], "--")) {
		if (!strcmp(arg[0], "--name"))
			lookup_name = 1;
		arg++;
		my_argc--;
	}

	if (my_argc % 2 != 0)
		die_usage(argc, argv, "Wrong number of arguments.");

	setup_git_directory();

	while (*arg) {
		struct object_id commit_oid;
		const struct submodule *submodule;
		const char *commit;
		const char *path_or_name;

		commit = arg[0];
		path_or_name = arg[1];

		if (commit[0] == '\0')
			oidclr(&commit_oid);
		else if (repo_get_oid(the_repository, commit, &commit_oid) < 0)
			die_usage(argc, argv, "Commit not found.");

		if (lookup_name) {
			submodule = submodule_from_name(the_repository,
							&commit_oid, path_or_name);
		} else
			submodule = submodule_from_path(the_repository,
							&commit_oid, path_or_name);
		if (!submodule)
			die_usage(argc, argv, "Submodule not found.");

		printf("Submodule name: '%s' for path '%s'\n", submodule->name,
		       submodule->path);

		arg += 2;
	}

	submodule_free(the_repository);

	return 0;
}
