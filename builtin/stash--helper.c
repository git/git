#include "../stash.h"
#include <string.h>

static const char builtin_stash__helper_usage[] = {
	"Usage: git stash--helper --non-patch <tmp_indexfile> <i_tree>"
};

int cmd_stash__helper(int argc, const char **argv, const char *prefix)
{
	if (argc == 4 && !strcmp("--non-patch", argv[1]))
		return stash_non_patch(argv[2], argv[3], prefix);
	usage(builtin_stash__helper_usage);
}
