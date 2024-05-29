#include "../git-compat-util.h"
#include "../strbuf.h"

/* Adapted from libiberty's basename.c.  */
char *gitbasename (char *path)
{
	const char *base;

	if (path)
		skip_dos_drive_prefix(&path);

	if (!path || !*path)
		/*
		 * basename(3P) is mis-specified because it returns a
		 * non-constant pointer even though it is specified to return a
		 * pointer to internal memory at times. The cast is a result of
		 * that.
		 */
		return (char *) ".";

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

char *gitdirname(char *path)
{
	static struct strbuf buf = STRBUF_INIT;
	char *p = path, *slash = NULL, c;
	int dos_drive_prefix;

	if (!p)
		/*
		 * dirname(3P) is mis-specified because it returns a
		 * non-constant pointer even though it is specified to return a
		 * pointer to internal memory at times. The cast is a result of
		 * that.
		 */
		return (char *) ".";

	if ((dos_drive_prefix = skip_dos_drive_prefix(&p)) && !*p)
		goto dot;

	/*
	 * POSIX.1-2001 says dirname("/") should return "/", and dirname("//")
	 * should return "//", but dirname("///") should return "/" again.
	 */
	if (is_dir_sep(*p)) {
		if (!p[1] || (is_dir_sep(p[1]) && !p[2]))
			return path;
		slash = ++p;
	}
	while ((c = *(p++)))
		if (is_dir_sep(c)) {
			char *tentative = p - 1;

			/* POSIX.1-2001 says to ignore trailing slashes */
			while (is_dir_sep(*p))
				p++;
			if (*p)
				slash = tentative;
		}

	if (slash) {
		*slash = '\0';
		return path;
	}

dot:
	strbuf_reset(&buf);
	strbuf_addf(&buf, "%.*s.", dos_drive_prefix, path);
	return buf.buf;
}
