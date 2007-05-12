#include "cache.h"

int has_symlink_leading_path(const char *name, char *last_symlink)
{
	char path[PATH_MAX];
	const char *sp, *ep;
	char *dp;

	sp = name;
	dp = path;

	if (last_symlink && *last_symlink) {
		size_t last_len = strlen(last_symlink);
		size_t len = strlen(name);
		if (last_len < len &&
		    !strncmp(name, last_symlink, last_len) &&
		    name[last_len] == '/')
			return 1;
		*last_symlink = '\0';
	}

	while (1) {
		size_t len;
		struct stat st;

		ep = strchr(sp, '/');
		if (!ep)
			break;
		len = ep - sp;
		if (PATH_MAX <= dp + len - path + 2)
			return 0; /* new name is longer than that??? */
		memcpy(dp, sp, len);
		dp[len] = 0;

		if (lstat(path, &st))
			return 0;
		if (S_ISLNK(st.st_mode)) {
			if (last_symlink)
				strcpy(last_symlink, path);
			return 1;
		}

		dp[len++] = '/';
		dp = dp + len;
		sp = ep + 1;
	}
	return 0;
}
