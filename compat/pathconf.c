#include "git-compat-util.h"

/*
 * Minimal stub for platforms without pathconf() (e.g. Windows),
 * to fall back to NAME_MAX from limits.h or compat/posix.h.
 */
long git_pathconf(const char *path UNUSED, int name UNUSED)
{
	return -1;
}
