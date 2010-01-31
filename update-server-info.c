#include "cache.h"
#include "exec_cmd.h"

static const char update_server_info_usage[] =
"git update-server-info [--force]";

int main(int ac, char **av)
{
	int i;
	int force = 0;
	for (i = 1; i < ac; i++) {
		if (av[i][0] == '-') {
			if (!strcmp("--force", av[i]) ||
			    !strcmp("-f", av[i]))
				force = 1;
			else
				usage(update_server_info_usage);
		}
	}
	if (i != ac)
		usage(update_server_info_usage);

	git_extract_argv0_path(av[0]);

	setup_git_directory();

	return !!update_server_info(force);
}
