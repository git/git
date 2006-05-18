/*
 * GIT - The information manager from hell
 */

#include "cache.h"
#include "refs.h"
#include "builtin.h"

int cmd_check_ref_format(int argc, const char **argv, char **envp)
{
	if (argc != 2)
		usage("git check-ref-format refname");
	return !!check_ref_format(argv[1]);
}
