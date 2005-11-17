#include "cache.h"

static const char git_config_set_usage[] =
"git-config-set name [value [value_regex]] | --unset name [value_regex]";

int main(int argc, const char **argv)
{
	setup_git_directory();
	switch (argc) {
	case 2:
		return git_config_set(argv[1], NULL);
	case 3:
		if (!strcmp(argv[1], "--unset"))
			return git_config_set(argv[2], NULL);
		else
			return git_config_set(argv[1], argv[2]);
	case 4:
		if (!strcmp(argv[1], "--unset"))
			return git_config_set_multivar(argv[2], NULL, argv[3]);
		else
			return git_config_set_multivar(argv[1], argv[2], argv[3]);
	default:
		usage(git_config_set_usage);
	}
	return 0;
}
