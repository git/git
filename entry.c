#include "cache.h"
#include "blob.h"
#include "dir.h"

static void create_directories(const char *path, int path_len,
			       const struct checkout *state)
{
	char *buf = xmalloc(path_len + 1);
	int len = 0;

	while (len < path_len) {
		do {
			buf[len] = path[len];
			len++;
		} while (len < path_len && path[len] != '/');
		if (len >= path_len)
			break;
		buf[len] = 0;

		/*
		 * For 'checkout-index --prefix=<dir>', <dir> is
		 * allowed to be a symlink to an existing directory,
		 * and we set 'state->base_dir_len' below, such that
		 * we test the path components of the prefix with the
		 * stat() function instead of the lstat() function.
		 */
		if (has_dirs_only_path(buf, len, state->base_dir_len))
			continue; /* ok, it is already a directory. */

		/*
		 * If this mkdir() would fail, it could be that there
		 * is already a symlink or something else exists
		 * there, therefore we then try to unlink it and try
		 * one more time to create the directory.
		 */
		if (mkdir(buf, 0777)) {
			if (errno == EEXIST && state->force &&
			    !unlink_or_warn(buf) && !mkdir(buf, 0777))
				continue;
			die_errno("cannot create directory at '%s'", buf);
		}
	}
	free(buf);
}

static void remove_subtree(const char *path)
{
	DIR *dir = opendir(path);
	struct dirent *de;
	char pathbuf[PATH_MAX];
	char *name;

	if (!dir)
		die_errno("cannot opendir '%s'", path);
	strcpy(pathbuf, path);
	name = pathbuf + strlen(path);
	*name++ = '/';
	while ((de = readdir(dir)) != NULL) {
		struct stat st;
		if (is_dot_or_dotdot(de->d_name))
			continue;
		strcpy(name, de->d_name);
		if (lstat(pathbuf, &st))
			die_errno("cannot lstat '%s'", pathbuf);
		if (S_ISDIR(st.st_mode))
			remove_subtree(pathbuf);
		else if (unlink(pathbuf))
			die_errno("cannot unlink '%s'", pathbuf);
	}
	closedir(dir);
	if (rmdir(path))
		die_errno("cannot rmdir '%s'", path);
}

static int create_file(const char *path, unsigned int mode)
{
	mode = (mode & 0100) ? 0777 : 0666;
	return open(path, O_WRONLY | O_CREAT | O_EXCL, mode);
}

static void *read_blob_entry(struct cache_entry *ce, unsigned long *size)
{
	enum object_type type;
	void *new = read_sha1_file(ce->sha1, &type, size);

	if (new) {
		if (type == OBJ_BLOB)
			return new;
		free(new);
	}
	return NULL;
}

static int write_entry(struct cache_entry *ce, char *path, const struct checkout *state, int to_tempfile)
{
	unsigned int ce_mode_s_ifmt = ce->ce_mode & S_IFMT;
	int fd, ret, fstat_done = 0;
	char *new;
	struct strbuf buf = STRBUF_INIT;
	unsigned long size;
	size_t wrote, newsize = 0;
	struct stat st;

	switch (ce_mode_s_ifmt) {
	case S_IFREG:
	case S_IFLNK:
		new = read_blob_entry(ce, &size);
		if (!new)
			return error("git checkout-index: unable to read sha1 file of %s (%s)",
				path, sha1_to_hex(ce->sha1));

		if (ce_mode_s_ifmt == S_IFLNK && has_symlinks && !to_tempfile) {
			ret = symlink(new, path);
			free(new);
			if (ret)
				return error("git checkout-index: unable to create symlink %s (%s)",
					     path, strerror(errno));
			break;
		}

		/*
		 * Convert from git internal format to working tree format
		 */
		if (ce_mode_s_ifmt == S_IFREG &&
		    convert_to_working_tree(ce->name, new, size, &buf)) {
			free(new);
			new = strbuf_detach(&buf, &newsize);
			size = newsize;
		}

		if (to_tempfile) {
			if (ce_mode_s_ifmt == S_IFREG)
				strcpy(path, ".merge_file_XXXXXX");
			else
				strcpy(path, ".merge_link_XXXXXX");
			fd = mkstemp(path);
		} else if (ce_mode_s_ifmt == S_IFREG) {
			fd = create_file(path, ce->ce_mode);
		} else {
			fd = create_file(path, 0666);
		}
		if (fd < 0) {
			free(new);
			return error("git checkout-index: unable to create file %s (%s)",
				path, strerror(errno));
		}

		wrote = write_in_full(fd, new, size);
		/* use fstat() only when path == ce->name */
		if (fstat_is_reliable() &&
		    state->refresh_cache && !to_tempfile && !state->base_dir_len) {
			fstat(fd, &st);
			fstat_done = 1;
		}
		close(fd);
		free(new);
		if (wrote != size)
			return error("git checkout-index: unable to write file %s", path);
		break;
	case S_IFGITLINK:
		if (to_tempfile)
			return error("git checkout-index: cannot create temporary subproject %s", path);
		if (mkdir(path, 0777) < 0)
			return error("git checkout-index: cannot create subproject directory %s", path);
		break;
	default:
		return error("git checkout-index: unknown file mode for %s", path);
	}

	if (state->refresh_cache) {
		if (!fstat_done)
			lstat(ce->name, &st);
		fill_stat_cache_info(ce, &st);
	}
	return 0;
}

/*
 * This is like 'lstat()', except it refuses to follow symlinks
 * in the path, after skipping "skiplen".
 */
static int check_path(const char *path, int len, struct stat *st, int skiplen)
{
	const char *slash = path + len;

	while (path < slash && *slash != '/')
		slash--;
	if (!has_dirs_only_path(path, slash - path, skiplen)) {
		errno = ENOENT;
		return -1;
	}
	return lstat(path, st);
}

int checkout_entry(struct cache_entry *ce, const struct checkout *state, char *topath)
{
	static char path[PATH_MAX + 1];
	struct stat st;
	int len = state->base_dir_len;

	if (topath)
		return write_entry(ce, topath, state, 1);

	memcpy(path, state->base_dir, len);
	strcpy(path + len, ce->name);
	len += ce_namelen(ce);

	if (!check_path(path, len, &st, state->base_dir_len)) {
		unsigned changed = ce_match_stat(ce, &st, CE_MATCH_IGNORE_VALID|CE_MATCH_IGNORE_SKIP_WORKTREE);
		if (!changed)
			return 0;
		if (!state->force) {
			if (!state->quiet)
				fprintf(stderr, "git-checkout-index: %s already exists\n", path);
			return -1;
		}

		/*
		 * We unlink the old file, to get the new one with the
		 * right permissions (including umask, which is nasty
		 * to emulate by hand - much easier to let the system
		 * just do the right thing)
		 */
		if (S_ISDIR(st.st_mode)) {
			/* If it is a gitlink, leave it alone! */
			if (S_ISGITLINK(ce->ce_mode))
				return 0;
			if (!state->force)
				return error("%s is a directory", path);
			remove_subtree(path);
		} else if (unlink(path))
			return error("unable to unlink old '%s' (%s)", path, strerror(errno));
	} else if (state->not_new)
		return 0;
	create_directories(path, len, state);
	return write_entry(ce, path, state, 0);
}
