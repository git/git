#include "../git-compat-util.h"
#include "../cache.h"

int cygwin_offset_1st_component(const char *path)
{
	const char *pos = path;
	/* unc paths */
	if (is_dir_sep(pos[0]) && is_dir_sep(pos[1])) {
		/* skip server name */
		pos = strchr(pos + 2, '/');
		if (!pos)
			return 0; /* Error: malformed unc path */

		do {
			pos++;
		} while (*pos && pos[0] != '/');
	}
	return pos + is_dir_sep(*pos) - path;
}
