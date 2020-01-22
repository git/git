#define COMPAT_CODE_ACCESS
#include "../git-compat-util.h"

/* Do the same thing access(2) does, but use the effective uid,
 * and don't make the mistake of telling root that any file is
 * executable.  This version uses stat(2).
 */
int git_access(const char *path, int mode)
{
	struct stat st;

	/* do not interfere a normal user */
	if (geteuid())
		return access(path, mode);

	if (stat(path, &st) < 0)
		return -1;

	/* Root can read or write any file. */
	if (!(mode & X_OK))
		return 0;

	/* Root can execute any file that has any one of the execute
	 * bits set.
	 */
	if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
		return 0;

	errno = EACCES;
	return -1;
}
