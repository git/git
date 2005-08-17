#include "cache.h"

const char **get_pathspec(const char *prefix, char **pathspec)
{
	char *entry = *pathspec;
	char **p;
	int prefixlen;

	if (!prefix) {
		char **p;
		if (!entry)
			return NULL;
		p = pathspec;
		do {
			if (*entry != '.')
				continue;
			/* fixup ? */
		} while ((entry = *++p) != NULL);
		return (const char **) pathspec;
	}

	if (!entry) {
		static const char *spec[2];
		spec[0] = prefix;
		spec[1] = NULL;
		return spec;
	}

	/* Otherwise we have to re-write the entries.. */
	prefixlen = strlen(prefix);
	p = pathspec;
	do {
		int speclen, len = prefixlen;
		char *n;

		for (;;) {
			if (!strcmp(entry, ".")) {
				entry++;
				break;
			}
			if (!strncmp(entry, "./", 2)) {
				entry += 2;
				continue;
			}
			if (!strncmp(entry, "../", 3)) {
				do {
					if (!len)
						die("'%s' is outside repository", *p);
					len--;
				} while (len && prefix[len-1] != '/');
				entry += 3;
				continue;
			}
			break;
		}
		speclen = strlen(entry);
		n = xmalloc(speclen + len + 1);
		
		memcpy(n, prefix, len);
		memcpy(n + len, entry, speclen+1);
		*p = n;
	} while ((entry = *++p) != NULL);
	return (const char **) pathspec;
}

const char *setup_git_directory(void)
{
	static char cwd[PATH_MAX+1];
	int len, offset;

	/*
	 * If GIT_DIR is set explicitly, we're not going
	 * to do any discovery
	 */
	if (gitenv(GIT_DIR_ENVIRONMENT))
		return NULL;

	if (!getcwd(cwd, sizeof(cwd)) || cwd[0] != '/')
		die("Unable to read current working directory");

	offset = len = strlen(cwd);
	for (;;) {
		/*
		 * We always want to see a .git/HEAD and a .git/refs/
		 * subdirectory
		 */
		if (!access(".git/HEAD", R_OK) && !access(".git/refs/", X_OK)) {
			/*
			 * Then we need either a GIT_OBJECT_DIRECTORY define
			 * or a .git/objects/ directory
			 */
			if (gitenv(DB_ENVIRONMENT) || !access(".git/objects/", X_OK))
				break;
		}
		chdir("..");
		do {
			if (!offset)
				die("Not a git repository");
		} while (cwd[--offset] != '/');
	}

	if (offset == len)
		return NULL;

	/* Make "offset" point to past the '/', and add a '/' at the end */
	offset++;
	cwd[len++] = '/';
	cwd[len] = 0;
	return cwd + offset;
}
