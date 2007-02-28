#include "cache.h"
#include "blob.h"

static void create_directories(const char *path, struct checkout *state)
{
	int len = strlen(path);
	char *buf = xmalloc(len + 1);
	const char *slash = path;

	while ((slash = strchr(slash+1, '/')) != NULL) {
		len = slash - path;
		memcpy(buf, path, len);
		buf[len] = 0;
		if (mkdir(buf, 0777)) {
			if (errno == EEXIST) {
				struct stat st;
				if (len > state->base_dir_len && state->force && !unlink(buf) && !mkdir(buf, 0777))
					continue;
				if (!stat(buf, &st) && S_ISDIR(st.st_mode))
					continue; /* ok */
			}
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
		die("cannot opendir %s", path);
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
			die("cannot lstat %s", pathbuf);
		if (S_ISDIR(st.st_mode))
			remove_subtree(pathbuf);
		else if (unlink(pathbuf))
			die("cannot unlink %s", pathbuf);
	}
	closedir(dir);
	if (rmdir(path))
		die("cannot rmdir %s", path);
}

static int create_file(const char *path, unsigned int mode)
{
	mode = (mode & 0100) ? 0777 : 0666;
	return open(path, O_WRONLY | O_CREAT | O_EXCL, mode);
}

static int write_entry(struct cache_entry *ce, char *path, struct checkout *state, int to_tempfile)
{
	int fd;
	void *new;
	unsigned long size;
	long wrote;
	enum object_type type;

	new = read_sha1_file(ce->sha1, &type, &size);
	if (!new || type != OBJ_BLOB) {
		if (new)
			free(new);
		return error("git-checkout-index: unable to read sha1 file of %s (%s)",
			path, sha1_to_hex(ce->sha1));
	}
	switch (ntohl(ce->ce_mode) & S_IFMT) {
		char *buf;
		unsigned long nsize;

	case S_IFREG:
		if (to_tempfile) {
			strcpy(path, ".merge_file_XXXXXX");
			fd = mkstemp(path);
		} else
			fd = create_file(path, ntohl(ce->ce_mode));
		if (fd < 0) {
			free(new);
			return error("git-checkout-index: unable to create file %s (%s)",
				path, strerror(errno));
		}

		/*
		 * Convert from git internal format to working tree format
		 */
		buf = new;
		nsize = size;
		if (convert_to_working_tree(ce->name, &buf, &nsize)) {
			free(new);
			new = buf;
			size = nsize;
		}

		wrote = write_in_full(fd, new, size);
		close(fd);
		free(new);
		if (wrote != size)
			return error("git-checkout-index: unable to write file %s", path);
		break;
	case S_IFLNK:
		if (to_tempfile) {
			strcpy(path, ".merge_link_XXXXXX");
			fd = mkstemp(path);
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
	default:
		free(new);
		return error("git-checkout-index: unknown file mode for %s", path);
	}

	if (state->refresh_cache) {
		struct stat st;
		lstat(ce->name, &st);
		fill_stat_cache_info(ce, &st);
	}
	return 0;
}

int checkout_entry(struct cache_entry *ce, struct checkout *state, char *topath)
{
	static char path[PATH_MAX + 1];
	struct stat st;
	int len = state->base_dir_len;

	if (topath)
		return write_entry(ce, topath, state, 1);

	memcpy(path, state->base_dir, len);
	strcpy(path + len, ce->name);

	if (!lstat(path, &st)) {
		unsigned changed = ce_match_stat(ce, &st, 1);
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
		unlink(path);
		if (S_ISDIR(st.st_mode)) {
			if (!state->force)
				return error("%s is a directory", path);
			remove_subtree(path);
		}
	} else if (state->not_new)
		return 0;
	create_directories(path, state);
	return write_entry(ce, path, state, 0);
}
