#include "test-tool.h"
#include "git-compat-util.h"
#include "thread-utils.h"

int cmd__online_cpus(int argc UNUSED, const char **argv UNUSED)
{
	printf("%d\n", online_cpus());
	return 0;
}
