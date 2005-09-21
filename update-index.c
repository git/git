/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

/*
 * Default to not allowing changes to the list of files. The
 * tool doesn't actually care, but this makes it harder to add
 * files to the revision control by mistake by doing something
 * like "git-update-index *" and suddenly having all the object
 * files be revision controlled.
 */
static int allow_add = 0, allow_remove = 0, allow_replace = 0, not_new = 0, quiet = 0, info_only = 0;
static int force_remove;

/* Three functions to allow overloaded pointer return; see linux/err.h */
static inline void *ERR_PTR(long error)
{
	return (void *) error;
}

static inline long PTR_ERR(const void *ptr)
{
	return (long) ptr;
}

static inline long IS_ERR(const void *ptr)
{
	return (unsigned long)ptr > (unsigned long)-1000L;
}

static int add_file_to_cache(const char *path)
{
	int size, namelen, option, status;
	struct cache_entry *ce;
	struct stat st;
	int fd;
	char *target;

	status = lstat(path, &st);
	if (status < 0 || S_ISDIR(st.st_mode)) {
		/* When we used to have "path" and now we want to add
		 * "path/file", we need a way to remove "path" before
		 * being able to add "path/file".  However,
		 * "git-update-index --remove path" would not work.
		 * --force-remove can be used but this is more user
		 * friendly, especially since we can do the opposite
		 * case just fine without --force-remove.
		 */
		if (status == 0 || (errno == ENOENT || errno == ENOTDIR)) {
			if (allow_remove) {
				if (remove_file_from_cache(path))
					return error("%s: cannot remove from the index",
					             path);
				else
					return 0;
			} else if (status < 0) {
				return error("%s: does not exist and --remove not passed",
				             path);
			}
		}
		if (0 == status)
			return error("%s: is a directory - add files inside instead",
			             path);
		else
			return error("lstat(\"%s\"): %s", path,
				     strerror(errno));
	}
	namelen = strlen(path);
	size = cache_entry_size(namelen);
	ce = xmalloc(size);
	memset(ce, 0, size);
	memcpy(ce->name, path, namelen);
	fill_stat_cache_info(ce, &st);
	ce->ce_mode = create_ce_mode(st.st_mode);
	ce->ce_flags = htons(namelen);
	switch (st.st_mode & S_IFMT) {
	case S_IFREG:
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return error("open(\"%s\"): %s", path, strerror(errno));
		if (index_fd(ce->sha1, fd, &st, !info_only, NULL) < 0)
			return error("%s: failed to insert into database", path);
		break;
	case S_IFLNK:
		target = xmalloc(st.st_size+1);
		if (readlink(path, target, st.st_size+1) != st.st_size) {
			char *errstr = strerror(errno);
			free(target);
			return error("readlink(\"%s\"): %s", path,
			             errstr);
		}
		if (info_only) {
			unsigned char hdr[50];
			int hdrlen;
			write_sha1_file_prepare(target, st.st_size, "blob",
						ce->sha1, hdr, &hdrlen);
		} else if (write_sha1_file(target, st.st_size, "blob", ce->sha1))
			return error("%s: failed to insert into database", path);
		free(target);
		break;
	default:
		return error("%s: unsupported file type", path);
	}
	option = allow_add ? ADD_CACHE_OK_TO_ADD : 0;
	option |= allow_replace ? ADD_CACHE_OK_TO_REPLACE : 0;
	if (add_cache_entry(ce, option))
		return error("%s: cannot add to the index - missing --add option?",
			     path);
	return 0;
}

/*
 * "refresh" does not calculate a new sha1 file or bring the
 * cache up-to-date for mode/content changes. But what it
 * _does_ do is to "re-match" the stat information of a file
 * with the cache, so that you can refresh the cache for a
 * file that hasn't been changed but where the stat entry is
 * out of date.
 *
 * For example, you'd want to do this after doing a "git-read-tree",
 * to link up the stat cache details with the proper files.
 */
static struct cache_entry *refresh_entry(struct cache_entry *ce)
{
	struct stat st;
	struct cache_entry *updated;
	int changed, size;

	if (lstat(ce->name, &st) < 0)
		return ERR_PTR(-errno);

	changed = ce_match_stat(ce, &st);
	if (!changed)
		return ce;

	if (ce_modified(ce, &st))
		return ERR_PTR(-EINVAL);

	size = ce_size(ce);
	updated = xmalloc(size);
	memcpy(updated, ce, size);
	fill_stat_cache_info(updated, &st);
	return updated;
}

static int refresh_cache(void)
{
	int i;
	int has_errors = 0;

	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce, *new;
		ce = active_cache[i];
		if (ce_stage(ce)) {
			printf("%s: needs merge\n", ce->name);
			has_errors = 1;
			while ((i < active_nr) &&
			       ! strcmp(active_cache[i]->name, ce->name))
				i++;
			i--;
			continue;
		}

		new = refresh_entry(ce);
		if (IS_ERR(new)) {
			if (not_new && PTR_ERR(new) == -ENOENT)
				continue;
			if (quiet)
				continue;
			printf("%s: needs update\n", ce->name);
			has_errors = 1;
			continue;
		}
		active_cache_changed = 1;
		/* You can NOT just free active_cache[i] here, since it
		 * might not be necessarily malloc()ed but can also come
		 * from mmap(). */
		active_cache[i] = new;
	}
	return has_errors;
}

/*
 * We fundamentally don't like some paths: we don't want
 * dot or dot-dot anywhere, and for obvious reasons don't
 * want to recurse into ".git" either.
 *
 * Also, we don't want double slashes or slashes at the
 * end that can make pathnames ambiguous.
 */
static int verify_dotfile(const char *rest)
{
	/*
	 * The first character was '.', but that
	 * has already been discarded, we now test
	 * the rest.
	 */
	switch (*rest) {
	/* "." is not allowed */
	case '\0': case '/':
		return 0;

	/*
	 * ".git" followed by  NUL or slash is bad. This
	 * shares the path end test with the ".." case.
	 */
	case 'g':
		if (rest[1] != 'i')
			break;
		if (rest[2] != 't')
			break;
		rest += 2;
	/* fallthrough */
	case '.':
		if (rest[1] == '\0' || rest[1] == '/')
			return 0;
	}
	return 1;
}

static int verify_path(const char *path)
{
	char c;

	goto inside;
	for (;;) {
		if (!c)
			return 1;
		if (c == '/') {
inside:
			c = *path++;
			switch (c) {
			default:
				continue;
			case '/': case '\0':
				break;
			case '.':
				if (verify_dotfile(path))
					continue;
			}
			return 0;
		}
		c = *path++;
	}
}

static int add_cacheinfo(const char *arg1, const char *arg2, const char *arg3)
{
	int size, len, option;
	unsigned int mode;
	unsigned char sha1[20];
	struct cache_entry *ce;

	if (sscanf(arg1, "%o", &mode) != 1)
		return -1;
	if (get_sha1_hex(arg2, sha1))
		return -1;
	if (!verify_path(arg3))
		return -1;

	len = strlen(arg3);
	size = cache_entry_size(len);
	ce = xmalloc(size);
	memset(ce, 0, size);

	memcpy(ce->sha1, sha1, 20);
	memcpy(ce->name, arg3, len);
	ce->ce_flags = htons(len);
	ce->ce_mode = create_ce_mode(mode);
	option = allow_add ? ADD_CACHE_OK_TO_ADD : 0;
	option |= allow_replace ? ADD_CACHE_OK_TO_REPLACE : 0;
	return add_cache_entry(ce, option);
}

static struct cache_file cache_file;

int main(int argc, const char **argv)
{
	int i, newfd, entries, has_errors = 0;
	int allow_options = 1;
	const char *prefix = setup_git_directory();

	newfd = hold_index_file_for_update(&cache_file, get_index_file());
	if (newfd < 0)
		die("unable to create new cachefile");

	entries = read_cache();
	if (entries < 0)
		die("cache corrupted");

	for (i = 1 ; i < argc; i++) {
		const char *path = argv[i];

		if (allow_options && *path == '-') {
			if (!strcmp(path, "--")) {
				allow_options = 0;
				continue;
			}
			if (!strcmp(path, "-q")) {
				quiet = 1;
				continue;
			}
			if (!strcmp(path, "--add")) {
				allow_add = 1;
				continue;
			}
			if (!strcmp(path, "--replace")) {
				allow_replace = 1;
				continue;
			}
			if (!strcmp(path, "--remove")) {
				allow_remove = 1;
				continue;
			}
			if (!strcmp(path, "--refresh")) {
				has_errors |= refresh_cache();
				continue;
			}
			if (!strcmp(path, "--cacheinfo")) {
				if (i+3 >= argc)
					die("git-update-index: --cacheinfo <mode> <sha1> <path>");
				if (add_cacheinfo(argv[i+1], argv[i+2], argv[i+3]))
					die("git-update-index: --cacheinfo cannot add %s", argv[i+3]);
				i += 3;
				continue;
			}
			if (!strcmp(path, "--info-only")) {
				info_only = 1;
				continue;
			}
			if (!strcmp(path, "--force-remove")) {
				force_remove = 1;
				continue;
			}

			if (!strcmp(path, "--ignore-missing")) {
				not_new = 1;
				continue;
			}
			die("unknown option %s", path);
		}
		path = prefix_path(prefix, prefix ? strlen(prefix) : 0, path);
		if (!verify_path(path)) {
			fprintf(stderr, "Ignoring path %s\n", argv[i]);
			continue;
		}
		if (force_remove) {
			if (remove_file_from_cache(path))
				die("git-update-index: unable to remove %s", path);
			continue;
		}
		if (add_file_to_cache(path))
			die("Unable to process file %s", path);
	}
	if (write_cache(newfd, active_cache, active_nr) ||
	    commit_index_file(&cache_file))
		die("Unable to write new cachefile");

	return has_errors ? 1 : 0;
}
