#ifndef WIN32_PATH_UTILS_H
#define WIN32_PATH_UTILS_H

int win32_has_dos_drive_prefix(const char *path);
#define has_dos_drive_prefix win32_has_dos_drive_prefix

int win32_skip_dos_drive_prefix(char **path);
#define skip_dos_drive_prefix win32_skip_dos_drive_prefix
static inline int win32_is_dir_sep(int c)
{
	return c == '/' || c == '\\';
}
#define is_dir_sep win32_is_dir_sep
static inline char *win32_find_last_dir_sep(const char *path)
{
	char *ret = NULL;
	for (; *path; ++path)
		if (is_dir_sep(*path))
			ret = (char *)path;
	return ret;
}
#define find_last_dir_sep win32_find_last_dir_sep
static inline int win32_has_dir_sep(const char *path)
{
	/*
	 * See how long the non-separator part of the given path is, and
	 * if and only if it covers the whole path (i.e. path[len] is NUL),
	 * there is no separator in the path---otherwise there is a separator.
	 */
	size_t len = strcspn(path, "/\\");
	return !!path[len];
}
#define has_dir_sep(path) win32_has_dir_sep(path)
int win32_offset_1st_component(const char *path);
#define offset_1st_component win32_offset_1st_component

#endif
