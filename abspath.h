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

/**
 * Add a path to a buffer, converting a relative path to an
 * absolute one in the process.  Symbolic links are not
 * resolved.
 */
void strbuf_add_absolute_path(struct strbuf *sb, const char *path);

/**
 * Canonize `path` (make it absolute, resolve symlinks, remove extra
 * slashes) and append it to `sb`.  Die with an informative error
 * message if there is a problem.
 *
 * The directory part of `path` (i.e., everything up to the last
 * dir_sep) must denote a valid, existing directory, but the last
 * component need not exist.
 *
 * Callers that don't mind links should use the more lightweight
 * strbuf_add_absolute_path() instead.
 */
void strbuf_add_real_path(struct strbuf *sb, const char *path);

#endif /* ABSPATH_H */
