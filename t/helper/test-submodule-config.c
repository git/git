#include "test-tool.h"
#include "cache.h"
#include "config.h"
#include "submodule-config.h"
#include "submodule.h"

static void die_usage(int argc, const char **argv, const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "Usage: %s [<cummit> <submodulepath>] ...\n", argv[0]);
	exit(1);
}

int cmd__submodule_config(int argc, const char **argv)
{
	const char **arg = argv;
	int my_argc = argc;
	int output_url = 0;
	int lookup_name = 0;

	arg++;
	my_argc--;
	while (arg[0] && starts_with(arg[0], "--")) {
		if (!strcmp(arg[0], "--url"))
			output_url = 1;
		if (!strcmp(arg[0], "--name"))
			lookup_name = 1;
		arg++;
		my_argc--;
	}

	if (my_argc % 2 != 0)
		die_usage(argc, argv, "Wrong number of arguments.");

	setup_git_directory();

	while (*arg) {
		struct object_id cummit_oid;
		const struct submodule *submodule;
		const char *cummit;
		const char *path_or_name;

		cummit = arg[0];
		path_or_name = arg[1];

		if (cummit[0] == '\0')
			oidclr(&cummit_oid);
		else if (get_oid(cummit, &cummit_oid) < 0)
			die_usage(argc, argv, "cummit not found.");

		if (lookup_name) {
			submodule = submodule_from_name(the_repository,
							&cummit_oid, path_or_name);
		} else
			submodule = submodule_from_path(the_repository,
							&cummit_oid, path_or_name);
		if (!submodule)
			die_usage(argc, argv, "Submodule not found.");

		if (output_url)
			printf("Submodule url: '%s' for path '%s'\n",
					submodule->url, submodule->path);
		else
			printf("Submodule name: '%s' for path '%s'\n",
					submodule->name, submodule->path);

		arg += 2;
	}

	submodule_free(the_repository);

	return 0;
}
