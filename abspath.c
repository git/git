#include "cache.h"

/* We allow "recursive" symbolic links. Only within reason, though. */
#define MAXDEPTH 5

const char *make_absolute_path(const char *path)
{
	static char bufs[2][PATH_MAX + 1], *buf = bufs[0], *next_buf = bufs[1];
	char cwd[1024] = "";
	int buf_index = 1, len;

	int depth = MAXDEPTH;
	char *last_elem = NULL;
	struct stat st;

	if (strlcpy(buf, path, PATH_MAX) >= PATH_MAX)
		die ("Too long path: %.*s", 60, path);

	while (depth--) {
		if (stat(buf, &st) || !S_ISDIR(st.st_mode)) {
			char *last_slash = strrchr(buf, '/');
			if (last_slash) {
				*last_slash = '\0';
				last_elem = xstrdup(last_slash + 1);
			} else {
				last_elem = xstrdup(buf);
				*buf = '\0';
			}
		}

		if (*buf) {
			if (!*cwd && !getcwd(cwd, sizeof(cwd)))
				die ("Could not get current working directory");

			if (chdir(buf))
				die ("Could not switch to '%s'", buf);
		}
		if (!getcwd(buf, PATH_MAX))
			die ("Could not get current working directory");

		if (last_elem) {
			int len = strlen(buf);
			if (len + strlen(last_elem) + 2 > PATH_MAX)
				die ("Too long path name: '%s/%s'",
						buf, last_elem);
			buf[len] = '/';
			strcpy(buf + len + 1, last_elem);
			free(last_elem);
			last_elem = NULL;
		}

		if (!lstat(buf, &st) && S_ISLNK(st.st_mode)) {
			len = readlink(buf, next_buf, PATH_MAX);
			if (len < 0)
				die ("Invalid symlink: %s", buf);
			next_buf[len] = '\0';
			buf = next_buf;
			buf_index = 1 - buf_index;
			next_buf = bufs[buf_index];
		} else
			break;
	}

	if (*cwd && chdir(cwd))
		die ("Could not change back to '%s'", cwd);

	return buf;
}
