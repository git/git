#define USE_THE_REPOSITORY_VARIABLE

#include "../../git-compat-util.h"
#include "../../environment.h"

int win32_has_dos_drive_prefix(const char *path)
{
	int i;

	/*
	 * Does it start with an ASCII letter (i.e. highest bit not set),
	 * followed by a colon?
	 */
	if (!(0x80 & (unsigned char)*path))
		return *path && path[1] == ':' ? 2 : 0;

	/*
	 * While drive letters must be letters of the English alphabet, it is
	 * possible to assign virtually _any_ Unicode character via `subst` as
	 * a drive letter to "virtual drives". Even `1`, or `ä`. Or fun stuff
	 * like this:
	 *
	 *      subst ֍: %USERPROFILE%\Desktop
	 */
	for (i = 1; i < 4 && (0x80 & (unsigned char)path[i]); i++)
		; /* skip first UTF-8 character */
	return path[i] == ':' ? i + 1 : 0;
}

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

int win32_fspathncmp(const char *a, const char *b, size_t count)
{
	int diff;

	for (;;) {
		if (!count--)
			return 0;
		if (!*a)
			return *b ? -1 : 0;
		if (!*b)
			return +1;

		if (is_dir_sep(*a)) {
			if (!is_dir_sep(*b))
				return -1;
			a++;
			b++;
			continue;
		} else if (is_dir_sep(*b))
			return +1;

		diff = ignore_case ?
			(unsigned char)tolower(*a) - (int)(unsigned char)tolower(*b) :
			(unsigned char)*a - (int)(unsigned char)*b;
		if (diff)
			return diff;
		a++;
		b++;
	}
}

int win32_fspathcmp(const char *a, const char *b)
{
	return win32_fspathncmp(a, b, (size_t)-1);
}
