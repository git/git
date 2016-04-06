#include "cache.h"

/*
 * Do not use this for inspecting *tracked* content.  When path is a
 * symlink to a directory, we do not want to say it is a directory when
 * dealing with tracked content in the working tree.
 */
int is_directory(const char *path)
{
	struct stat st;
	return (!stat(path, &st) && S_ISDIR(st.st_mode));
}

/* We allow "recursive" symbolic links. Only within reason, though. */
#define MAXDEPTH 5

/*
 * Return the real path (i.e., absolute path, with symlinks resolved
 * and extra slashes removed) equivalent to the specified path.  (If
 * you want an absolute path but don't mind links, use
 * absolute_path().)  The return value is a pointer to a static
 * buffer.
 *
 * The input and all intermediate paths must be shorter than MAX_PATH.
 * The directory part of path (i.e., everything up to the last
 * dir_sep) must denote a valid, existing directory, but the last
 * component need not exist.  If die_on_error is set, then die with an
 * informative error message if there is a problem.  Otherwise, return
 * NULL on errors (without generating any output).
 *
 * If path is our buffer, then return path, as it's already what the
 * user wants.
 */
static const char *real_path_internal(const char *path, int die_on_error)
{
	static struct strbuf sb = STRBUF_INIT;
	char *retval = NULL;

	/*
	 * If we have to temporarily chdir(), store the original CWD
	 * here so that we can chdir() back to it at the end of the
	 * function:
	 */
	struct strbuf cwd = STRBUF_INIT;

	int depth = MAXDEPTH;
	char *last_elem = NULL;
	struct stat st;

	/* We've already done it */
	if (path == sb.buf)
		return path;

	if (!*path) {
		if (die_on_error)
			die("The empty string is not a valid path");
		else
			goto error_out;
	}

	strbuf_reset(&sb);
	strbuf_addstr(&sb, path);

	while (depth--) {
		if (!is_directory(sb.buf)) {
			char *last_slash = find_last_dir_sep(sb.buf);
			if (last_slash) {
				last_elem = xstrdup(last_slash + 1);
				strbuf_setlen(&sb, last_slash - sb.buf + 1);
			} else {
				last_elem = xmemdupz(sb.buf, sb.len);
				strbuf_reset(&sb);
			}
		}

		if (sb.len) {
			if (!cwd.len && strbuf_getcwd(&cwd)) {
				if (die_on_error)
					die_errno("Could not get current working directory");
				else
					goto error_out;
			}

			if (chdir(sb.buf)) {
				if (die_on_error)
					die_errno("Could not switch to '%s'",
						  sb.buf);
				else
					goto error_out;
			}
		}
		if (strbuf_getcwd(&sb)) {
			if (die_on_error)
				die_errno("Could not get current working directory");
			else
				goto error_out;
		}

		if (last_elem) {
			if (sb.len && !is_dir_sep(sb.buf[sb.len - 1]))
				strbuf_addch(&sb, '/');
			strbuf_addstr(&sb, last_elem);
			free(last_elem);
			last_elem = NULL;
		}

		if (!lstat(sb.buf, &st) && S_ISLNK(st.st_mode)) {
			struct strbuf next_sb = STRBUF_INIT;
			ssize_t len = strbuf_readlink(&next_sb, sb.buf, 0);
			if (len < 0) {
				if (die_on_error)
					die_errno("Invalid symlink '%s'",
						  sb.buf);
				else
					goto error_out;
			}
			strbuf_swap(&sb, &next_sb);
			strbuf_release(&next_sb);
		} else
			break;
	}

	retval = sb.buf;
error_out:
	free(last_elem);
	if (cwd.len && chdir(cwd.buf))
		die_errno("Could not change back to '%s'", cwd.buf);
	strbuf_release(&cwd);

	return retval;
}

const char *real_path(const char *path)
{
	return real_path_internal(path, 1);
}

const char *real_path_if_valid(const char *path)
{
	return real_path_internal(path, 0);
}

/*
 * Use this to get an absolute path from a relative one. If you want
 * to resolve links, you should use real_path.
 */
const char *absolute_path(const char *path)
{
	static struct strbuf sb = STRBUF_INIT;
	strbuf_reset(&sb);
	strbuf_add_absolute_path(&sb, path);
	return sb.buf;
}

/*
 * Unlike prefix_path, this should be used if the named file does
 * not have to interact with index entry; i.e. name of a random file
 * on the filesystem.
 */
const char *prefix_filename(const char *pfx, int pfx_len, const char *arg)
{
	static struct strbuf path = STRBUF_INIT;
#ifndef GIT_WINDOWS_NATIVE
	if (!pfx_len || is_absolute_path(arg))
		return arg;
	strbuf_reset(&path);
	strbuf_add(&path, pfx, pfx_len);
	strbuf_addstr(&path, arg);
#else
	/* don't add prefix to absolute paths, but still replace '\' by '/' */
	strbuf_reset(&path);
	if (is_absolute_path(arg))
		pfx_len = 0;
	else if (pfx_len)
		strbuf_add(&path, pfx, pfx_len);
	strbuf_addstr(&path, arg);
	convert_slashes(path.buf + pfx_len);
#endif
	return path.buf;
}
