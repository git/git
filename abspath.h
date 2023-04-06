#ifndef ABSPATH_H
#define ABSPATH_H

int is_directory(const char *);
char *strbuf_realpath(struct strbuf *resolved, const char *path,
		      int die_on_error);
char *strbuf_realpath_forgiving(struct strbuf *resolved, const char *path,
				int die_on_error);
char *real_pathdup(const char *path, int die_on_error);
const char *absolute_path(const char *path);
char *absolute_pathdup(const char *path);

/*
 * Concatenate "prefix" (if len is non-zero) and "path", with no
 * connecting characters (so "prefix" should end with a "/").
 * Unlike prefix_path, this should be used if the named file does
 * not have to interact with index entry; i.e. name of a random file
 * on the filesystem.
 *
 * The return value is always a newly allocated string (even if the
 * prefix was empty).
 */
char *prefix_filename(const char *prefix, const char *path);

/* Likewise, but path=="-" always yields "-" */
char *prefix_filename_except_for_dash(const char *prefix, const char *path);

static inline int is_absolute_path(const char *path)
{
	return is_dir_sep(path[0]) || has_dos_drive_prefix(path);
}

#endif /* ABSPATH_H */
