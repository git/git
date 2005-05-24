/*
 * Check-out files from the "current cache directory"
 *
 * Copyright (C) 2005 Linus Torvalds
 *
 * Careful: order of argument flags does matter. For example,
 *
 *	git-checkout-cache -a -f file.c
 *
 * Will first check out all files listed in the cache (but not
 * overwrite any old ones), and then force-checkout "file.c" a
 * second time (ie that one _will_ overwrite any old contents
 * with the same filename).
 *
 * Also, just doing "git-checkout-cache" does nothing. You probably
 * meant "git-checkout-cache -a". And if you want to force it, you
 * want "git-checkout-cache -f -a".
 *
 * Intuitiveness is not the goal here. Repeatability is. The
 * reason for the "no arguments means no work" thing is that
 * from scripts you are supposed to be able to do things like
 *
 *	find . -name '*.h' -print0 | xargs -0 git-checkout-cache -f --
 *
 * which will force all existing *.h files to be replaced with
 * their cached copies. If an empty command line implied "all",
 * then this would force-refresh everything in the cache, which
 * was not the point.
 *
 * Oh, and the "--" is just a good idea when you know the rest
 * will be filenames. Just so that you wouldn't have a filename
 * of "-a" causing problems (not possible in the above example,
 * but get used to it in scripting!).
 */
#include <sys/types.h>
#include <dirent.h>
#include "cache.h"

static int force = 0, quiet = 0, not_new = 0, refresh_cache = 0;
static const char *base_dir = "";
static int base_dir_len = 0;

static void create_directories(const char *path)
{
	int len = strlen(path);
	char *buf = xmalloc(len + 1);
	const char *slash = path;

	while ((slash = strchr(slash+1, '/')) != NULL) {
		len = slash - path;
		memcpy(buf, path, len);
		buf[len] = 0;
		if (mkdir(buf, 0755)) {
			if (errno == EEXIST) {
				struct stat st;
				if (len > base_dir_len && force && !unlink(buf) && !mkdir(buf, 0755))
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
	int fd;

	mode = (mode & 0100) ? 0777 : 0666;
	create_directories(path);
	fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, mode);
	if (fd < 0) {
		if (errno == EISDIR && force) {
			remove_subtree(path);
			fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, mode);
		}
	}
	return fd;
}

static int write_entry(struct cache_entry *ce, const char *path)
{
	int fd;
	void *new;
	unsigned long size;
	long wrote;
	char type[20];
	char target[1024];

	new = read_sha1_file(ce->sha1, type, &size);
	if (!new || strcmp(type, "blob")) {
		if (new)
			free(new);
		return error("git-checkout-cache: unable to read sha1 file of %s (%s)",
			path, sha1_to_hex(ce->sha1));
	}
	switch (ntohl(ce->ce_mode) & S_IFMT) {
	case S_IFREG:
		fd = create_file(path, ntohl(ce->ce_mode));
		if (fd < 0) {
			free(new);
			return error("git-checkout-cache: unable to create file %s (%s)",
				path, strerror(errno));
		}
		wrote = write(fd, new, size);
		close(fd);
		free(new);
		if (wrote != size)
			return error("git-checkout-cache: unable to write file %s", path);
		break;
	case S_IFLNK:
		memcpy(target, new, size);
		target[size] = '\0';
		create_directories(path);
		if (symlink(target, path)) {
			free(new);
			return error("git-checkout-cache: unable to create symlink %s (%s)",
				path, strerror(errno));
		}
		free(new);
		break;
	default:
		free(new);
		return error("git-checkout-cache: unknown file mode for %s", path);
	}

	if (refresh_cache) {
		struct stat st;
		lstat(ce->name, &st);
		fill_stat_cache_info(ce, &st);
	}
	return 0;
}

static int checkout_entry(struct cache_entry *ce)
{
	struct stat st;
	static char path[MAXPATHLEN+1];
	int len = base_dir_len;

	memcpy(path, base_dir, len);
	strcpy(path + len, ce->name);

	if (!lstat(path, &st)) {
		unsigned changed = ce_match_stat(ce, &st);
		if (!changed)
			return 0;
		if (!force) {
			if (!quiet)
				fprintf(stderr, "git-checkout-cache: %s already exists\n", path);
			return 0;
		}

		/*
		 * We unlink the old file, to get the new one with the
		 * right permissions (including umask, which is nasty
		 * to emulate by hand - much easier to let the system
		 * just do the right thing)
		 */
		unlink(path);
	} else if (not_new) 
		return 0;
	return write_entry(ce, path);
}

static int checkout_file(const char *name)
{
	int pos = cache_name_pos(name, strlen(name));
	if (pos < 0) {
		if (!quiet) {
			pos = -pos - 1;
			fprintf(stderr,
				"git-checkout-cache: %s is %s.\n",
				name,
				(pos < active_nr &&
				 !strcmp(active_cache[pos]->name, name)) ?
				"unmerged" : "not in the cache");
		}
		return -1;
	}
	return checkout_entry(active_cache[pos]);
}

static int checkout_all(void)
{
	int i;

	for (i = 0; i < active_nr ; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ce_stage(ce))
			continue;
		if (checkout_entry(ce) < 0)
			return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int i, force_filename = 0;
	struct cache_file cache_file;
	int newfd = -1;

	if (read_cache() < 0) {
		die("invalid cache");
	}

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!force_filename) {
			if (!strcmp(arg, "-a")) {
				checkout_all();
				continue;
			}
			if (!strcmp(arg, "--")) {
				force_filename = 1;
				continue;
			}
			if (!strcmp(arg, "-f")) {
				force = 1;
				continue;
			}
			if (!strcmp(arg, "-q")) {
				quiet = 1;
				continue;
			}
			if (!strcmp(arg, "-n")) {
				not_new = 1;
				continue;
			}
			if (!strcmp(arg, "-u")) {
				refresh_cache = 1;
				if (newfd < 0)
					newfd = hold_index_file_for_update
						(&cache_file,
						 get_index_file());
				if (newfd < 0)
					die("cannot open index.lock file.");
				continue;
			}
			if (!memcmp(arg, "--prefix=", 9)) {
				base_dir = arg+9;
				base_dir_len = strlen(base_dir);
				continue;
			}
		}
		if (base_dir_len) {
			/* when --prefix is specified we do not
			 * want to update cache.
			 */
			if (refresh_cache) {
				close(newfd); newfd = -1;
				rollback_index_file(&cache_file);
			}
			refresh_cache = 0;
		}
		checkout_file(arg);
	}

	if (0 <= newfd &&
	    (write_cache(newfd, active_cache, active_nr) ||
	     commit_index_file(&cache_file)))
		die("Unable to write new cachefile");
	return 0;
}
