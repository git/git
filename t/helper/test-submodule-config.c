#include "cache.h"
#include "submodule-config.h"
#include "submodule.h"

static void die_usage(int argc, const char **argv, const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "Usage: %s [<commit> <submodulepath>] ...\n", argv[0]);
	exit(1);
}

static int git_test_config(const char *var, const char *value, void *cb)
{
	return parse_submodule_config_option(var, value);
}

int cmd_main(int argc, const char **argv)
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
	gitmodules_config();
	git_config(git_test_config, NULL);

	while (*arg) {
		unsigned char commit_sha1[20];
		const struct submodule *submodule;
		const char *commit;
		const char *path_or_name;

		commit = arg[0];
		path_or_name = arg[1];

		if (commit[0] == '\0')
			hashclr(commit_sha1);
		else if (get_sha1(commit, commit_sha1) < 0)
			die_usage(argc, argv, "Commit not found.");

		if (lookup_name) {
			submodule = submodule_from_name(commit_sha1, path_or_name);
		} else
			submodule = submodule_from_path(commit_sha1, path_or_name);
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

	submodule_free();

	return 0;
}
