#include "../git-compat-util.h"

/* Adapted from libiberty's basename.c.  */
char *gitbasename (char *path)
{
	const char *base;

	if (path)
		skip_dos_drive_prefix(&path);

	if (!path || !*path)
		return ".";

	for (base = path; *path; path++) {
		if (!is_dir_sep(*path))
			continue;
		do {
			path++;
		} while (is_dir_sep(*path));
		if (*path)
			base = path;
		else
			while (--path != base && is_dir_sep(*path))
				*path = '\0';
	}
	return (char *)base;
}
