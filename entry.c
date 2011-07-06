#include "cache.h"
#include "blob.h"

static void create_directories(const char *path, const struct checkout *state)
{
	int len = strlen(path);
	char *buf = xmalloc(len + 1);
	const char *slash = path;

	while ((slash = strchr(slash+1, '/')) != NULL) {
		struct stat st;
		int stat_status;

		len = slash - path;
		memcpy(buf, path, len);
		buf[len] = 0;

		if (len <= state->base_dir_len)
			/*
			 * checkout-index --prefix=<dir>; <dir> is
			 * allowed to be a symlink to an existing
			 * directory.
			 */
			stat_status = stat(buf, &st);
		else
			/*
			 * if there currently is a symlink, we would
			 * want to replace it with a real directory.
			 */
			stat_status = lstat(buf, &st);

		if (!stat_status && S_ISDIR(st.st_mode))
			continue; /* ok, it is already a directory. */

		/*
		 * We know stat_status == 0 means something exists
		 * there and this mkdir would fail, but that is an
		 * error codepath; we do not care, as we unlink and
		 * mkdir again in such a case.
		 */
		if (mkdir(buf, 0777)) {
			if (errno == EEXIST && state->force &&
			    !unlink(buf) && !mkdir(buf, 0777))
				continue;
			die("cannot create directory at %s", buf);
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
		die("cannot opendir %s (%s)", path, strerror(errno));
	strcpy(pathbuf, path);
	name = pathbuf + strlen(path);
	*name++ = '/';
	while ((de = readdir(dir)) != NULL) {
		struct stat st;
		if ((de->d_name[0] == '.') &&
		    ((de->d_name[1] == 0) ||
		     ((de->d_name[1] == '.') && de->d_name[2] == 0)))
			continue;
		strcpy(name, de->d_name);
		if (lstat(pathbuf, &st))
			die("cannot lstat %s (%s)", pathbuf, strerror(errno));
		if (S_ISDIR(st.st_mode))
			remove_subtree(pathbuf);
		else if (unlink(pathbuf))
			die("cannot unlink %s (%s)", pathbuf, strerror(errno));
	}
	closedir(dir);
	if (rmdir(path))
		die("cannot rmdir %s (%s)", path, strerror(errno));
}

static int create_file(const char *path, unsigned int mode)
{
	mode = (mode & 0100) ? 0777 : 0666;
	return open(path, O_WRONLY | O_CREAT | O_EXCL, mode);
}

static void *read_blob_entry(struct cache_entry *ce, const char *path, unsigned long *size)
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
	int fd;
	long wrote;

	switch (ce->ce_mode & S_IFMT) {
		char *new;
		struct strbuf buf;
		unsigned long size;

	case S_IFREG:
		new = read_blob_entry(ce, path, &size);
		if (!new)
			return error("git-checkout-index: unable to read sha1 file of %s (%s)",
				path, sha1_to_hex(ce->sha1));

		/*
		 * Convert from git internal format to working tree format
		 */
		strbuf_init(&buf, 0);
		if (convert_to_working_tree(ce->name, new, size, &buf)) {
			size_t newsize = 0;
			free(new);
			new = strbuf_detach(&buf, &newsize);
			size = newsize;
		}

		if (to_tempfile) {
			strcpy(path, ".merge_file_XXXXXX");
			fd = mkstemp(path);
		} else
			fd = create_file(path, ce->ce_mode);
		if (fd < 0) {
			free(new);
			return error("git-checkout-index: unable to create file %s (%s)",
				path, strerror(errno));
		}

		wrote = write_in_full(fd, new, size);
		close(fd);
		free(new);
		if (wrote != size)
			return error("git-checkout-index: unable to write file %s", path);
		break;
	case S_IFLNK:
		new = read_blob_entry(ce, path, &size);
		if (!new)
			return error("git-checkout-index: unable to read sha1 file of %s (%s)",
				path, sha1_to_hex(ce->sha1));
		if (to_tempfile || !has_symlinks) {
			if (to_tempfile) {
				strcpy(path, ".merge_link_XXXXXX");
				fd = mkstemp(path);
			} else
				fd = create_file(path, 0666);
			if (fd < 0) {
				free(new);
				return error("git-checkout-index: unable to create "
						 "file %s (%s)", path, strerror(errno));
			}
			wrote = write_in_full(fd, new, size);
			close(fd);
			free(new);
			if (wrote != size)
				return error("git-checkout-index: unable to write file %s",
					path);
		} else {
			wrote = symlink(new, path);
			free(new);
			if (wrote)
				return error("git-checkout-index: unable to create "
						 "symlink %s (%s)", path, strerror(errno));
		}
		break;
	case S_IFGITLINK:
		if (to_tempfile)
			return error("git-checkout-index: cannot create temporary subproject %s", path);
		if (mkdir(path, 0777) < 0)
			return error("git-checkout-index: cannot create subproject directory %s", path);
		break;
	default:
		return error("git-checkout-index: unknown file mode for %s", path);
	}

	if (state->refresh_cache) {
		struct stat st;
		lstat(ce->name, &st);
		fill_stat_cache_info(ce, &st);
	}
	return 0;
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

	if (!lstat(path, &st)) {
		unsigned changed = ce_match_stat(ce, &st, CE_MATCH_IGNORE_VALID);
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
	create_directories(path, state);
	return write_entry(ce, path, state, 0);
}
