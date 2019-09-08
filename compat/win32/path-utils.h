#define has_dos_drive_prefix(path) \
	(isalpha(*(path)) && (path)[1] == ':' ? 2 : 0)
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
int win32_offset_1st_component(const char *path);
#define offset_1st_component win32_offset_1st_component
