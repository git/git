#include "../../git-compat-util.h"

int win32_skip_dos_drive_prefix(char **path)
{
	int ret = has_dos_drive_prefix(*path);
	*path += ret;
	return ret;
}

int win32_offset_1st_component(const char *path)
{
	char *pos = (char *)path;

	/* unc paths */
	if (!skip_dos_drive_prefix(&pos) &&
			is_dir_sep(pos[0]) && is_dir_sep(pos[1])) {
		/* skip server name */
		pos = strpbrk(pos + 2, "\\/");
		if (!pos)
			return 0; /* Error: malformed unc path */

		do {
			pos++;
		} while (*pos && !is_dir_sep(*pos));
	}

	return pos + is_dir_sep(*pos) - path;
}
