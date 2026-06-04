#include "git-compat-util.h"
#include "common-init.h"

int main(int argc, const char **argv)
{
	int result;

	init_git(argv);
	result = cmd_main(argc, argv);

	/* Not exit(3), but a wrapper calling our common_exit() */
	exit(result);
}
