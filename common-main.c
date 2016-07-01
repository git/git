#include "git-compat-util.h"
#include "exec_cmd.h"

int main(int argc, char **av)
{
	/*
	 * This const trickery is explained in
	 * 84d32bf7678259c08406571cd6ce4b7a6724dcba
	 */
	const char **argv = (const char **)av;

	argv[0] = git_extract_argv0_path(argv[0]);

	return cmd_main(argc, argv);
}
