#include "cache.h"

struct pathname {
	int len;
	char path[PATH_MAX];
};

/* Return matching pathname prefix length, or zero if not matching */
static inline int match_pathname(int len, const char *name, struct pathname *match)
{
	int match_len = match->len;
	return (len > match_len &&
		name[match_len] == '/' &&
		!memcmp(name, match->path, match_len)) ? match_len : 0;
}

static inline void set_pathname(int len, const char *name, struct pathname *match)
{
	if (len < PATH_MAX) {
		match->len = len;
		memcpy(match->path, name, len);
		match->path[len] = 0;
	}
}

int has_symlink_leading_path(int len, const char *name)
{
	static struct pathname link, nonlink;
	char path[PATH_MAX];
	struct stat st;
	char *sp;
	int known_dir;

	/*
	 * See if the last known symlink cache matches.
	 */
	if (match_pathname(len, name, &link))
		return 1;

	/*
	 * Get rid of the last known directory part
	 */
	known_dir = match_pathname(len, name, &nonlink);

	while ((sp = strchr(name + known_dir + 1, '/')) != NULL) {
		int thislen = sp - name ;
		memcpy(path, name, thislen);
		path[thislen] = 0;

		if (lstat(path, &st))
			return 0;
		if (S_ISDIR(st.st_mode)) {
			set_pathname(thislen, path, &nonlink);
			known_dir = thislen;
			continue;
		}
		if (S_ISLNK(st.st_mode)) {
			set_pathname(thislen, path, &link);
			return 1;
		}
		break;
	}
	return 0;
}
