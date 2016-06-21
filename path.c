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
	const char *git_dir = gitenv(GIT_DIR_ENVIRONMENT) ? : DEFAULT_GIT_DIR_ENVIRONMENT;
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
	} else
		len -= snprintf(pch, len, "%s/", env);

	safe_strncpy(pch, template, len);

	return mkstemp(path);
}


char *safe_strncpy(char *dest, const char *src, size_t n)
{
	strncpy(dest, src, n);
	dest[n - 1] = '\0';

	return dest;
}
