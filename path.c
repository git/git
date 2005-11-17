/*
 * I'm tired of doing "vsnprintf()" etc just to open a
 * file, so here's a "return static buffer with printf"
 * interface for paths.
 *
 * It's obviously not thread-safe. Sue me. But it's quite
 * useful for doing things like
 *
 *   f = open(mkpath("%s/%s.git", base, name), O_RDONLY);
 *
 * which is what it's designed for.
 */
#include "cache.h"
#include <pwd.h>

static char pathname[PATH_MAX];
static char bad_path[] = "/bad-path/";

static char *cleanup_path(char *path)
{
	/* Clean it up */
	if (!memcmp(path, "./", 2)) {
		path += 2;
		while (*path == '/')
			path++;
	}
	return path;
}

char *mkpath(const char *fmt, ...)
{
	va_list args;
	unsigned len;

	va_start(args, fmt);
	len = vsnprintf(pathname, PATH_MAX, fmt, args);
	va_end(args);
	if (len >= PATH_MAX)
		return bad_path;
	return cleanup_path(pathname);
}

char *git_path(const char *fmt, ...)
{
	const char *git_dir = get_git_dir();
	va_list args;
	unsigned len;

	len = strlen(git_dir);
	if (len > PATH_MAX-100)
		return bad_path;
	memcpy(pathname, git_dir, len);
	if (len && git_dir[len-1] != '/')
		pathname[len++] = '/';
	va_start(args, fmt);
	len += vsnprintf(pathname + len, PATH_MAX - len, fmt, args);
	va_end(args);
	if (len >= PATH_MAX)
		return bad_path;
	return cleanup_path(pathname);
}


/* git_mkstemp() - create tmp file honoring TMPDIR variable */
int git_mkstemp(char *path, size_t len, const char *template)
{
	char *env, *pch = path;

	if ((env = getenv("TMPDIR")) == NULL) {
		strcpy(pch, "/tmp/");
		len -= 5;
		pch += 5;
	} else {
		size_t n = snprintf(pch, len, "%s/", env);

		len -= n;
		pch += n;
	}

	safe_strncpy(pch, template, len);

	return mkstemp(path);
}


char *safe_strncpy(char *dest, const char *src, size_t n)
{
	strncpy(dest, src, n);
	dest[n - 1] = '\0';

	return dest;
}

static char *current_dir()
{
	return getcwd(pathname, sizeof(pathname));
}

/* Take a raw path from is_git_repo() and canonicalize it using Linus'
 * idea of a blind chdir() and getcwd(). */
static const char *canonical_path(char *path, int strict)
{
	char *dir = path;

	if(strict && *dir != '/')
		return NULL;

	if(*dir == '~') {		/* user-relative path */
		struct passwd *pw;
		char *slash = strchr(dir, '/');

		dir++;
		/* '~/' and '~' (no slash) means users own home-dir */
		if(!*dir || *dir == '/')
			pw = getpwuid(getuid());
		else {
			if (slash) {
				*slash = '\0';
				pw = getpwnam(dir);
				*slash = '/';
			}
			else
				pw = getpwnam(dir);
		}

		/* make sure we got something back that we can chdir() to */
		if(!pw || chdir(pw->pw_dir) < 0)
			return NULL;

		if(!slash || !slash[1]) /* no path following username */
			return current_dir();

		dir = slash + 1;
	}

	/* ~foo/path/to/repo is now path/to/repo and we're in foo's homedir */
	if(chdir(dir) < 0)
		return NULL;

	return current_dir();
}

char *enter_repo(char *path, int strict)
{
	if(!path)
		return NULL;

	if(!canonical_path(path, strict)) {
		if(strict || !canonical_path(mkpath("%s.git", path), strict))
			return NULL;
	}

	/* This is perfectly safe, and people tend to think of the directory
	 * where they ran git-init-db as their repository, so humour them. */
	(void)chdir(".git");

	if(access("objects", X_OK) == 0 && access("refs", X_OK) == 0) {
		putenv("GIT_DIR=.");
		return current_dir();
	}

	return NULL;
}
