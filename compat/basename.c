#include "../git-compat-util.h"

/* Adapted from libiberty's basename.c.  */
char *gitbasename (char *path)
{
	const char *base;
	skip_dos_drive_prefix(&path);
	for (base = path; *path; path++) {
		if (is_dir_sep(*path))
			base = path + 1;
	}
	return (char *)base;
}
