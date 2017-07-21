#include "git-compat-util.h"
#include "thread-utils.h"

int cmd_main(int argc, const char **argv)
{
	printf("%d\n", online_cpus());
	return 0;
}
