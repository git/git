#include "../git-compat-util.h"
#undef mkdir

/* for platforms that can't deal with a trailing '/' */
int compat_mkdir_wo_trailing_slash(const char *dir, mode_t mode)
{
	int retval;
	char *tmp_dir = NULL;
	size_t len = strlen(dir);

	if (len && dir[len-1] == '/') {
		if (!(tmp_dir = strdup(dir)))
			return -1;
		tmp_dir[len-1] = '\0';
	}
	else
		tmp_dir = (char *)dir;

	retval = mkdir(tmp_dir, mode);
	if (tmp_dir != dir)
		free(tmp_dir);

	return retval;
}
