#ifndef ABSPATH_H
#define ABSPATH_H

struct worktree;

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

/**
 * worktree_real_pathdup - Duplicate the absolute path of a worktree.
 *
 * @wt_path: The path to the worktree. This can be either an absolute or
 *           relative path.
 *
 * Return: A newly allocated string containing the absolute path. If the input
 *         path is already absolute, it returns a duplicate of the input path.
 *         If the path is relative, it constructs the absolute path by appending
 *         the relative path to the repository directory. The repository
 *         directory is derived from get_git_common_dir(), and the '.git' suffix
 *         is removed if present.
 *
 *         The returned path is resolved into its canonical form using
 *         strbuf_realpath_forgiving to handle symbolic links or non-existent
 *         paths gracefully.
 *
 * The caller is responsible for freeing the returned string when it is no
 * longer needed.
 */
char *worktree_real_pathdup(const char *wt_path);

/**
 * worktree_real_pathdup_for_wt - Duplicate the absolute path of a worktree from
 *                                a worktree structure.
 *
 * @wt: A pointer to the worktree structure.
 *
 * Return: A newly allocated string containing the absolute path of the worktree.
 *         If the worktree's path is relative, it constructs the absolute path
 *         by appending the relative path to the repository directory (derived
 *         from get_git_common_dir()). If the path is already absolute, it returns
 *         a duplicate of the worktree's path.
 *
 * The caller is responsible for freeing the returned string when it is no
 * longer needed.
 *
 * This function is similar to worktree_real_pathdup() but takes a pointer to a
 * worktree structure instead of a raw path.
 */
char *worktree_real_pathdup_for_wt(struct worktree *wt);

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
