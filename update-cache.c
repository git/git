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
 * like "update-cache *" and suddenly having all the object
 * files be revision controlled.
 */
static int allow_add = 0, allow_remove = 0;

static int index_fd(const char *path, int namelen, struct cache_entry *ce, int fd, struct stat *st)
{
	z_stream stream;
	unsigned long size = st->st_size;
	int max_out_bytes = namelen + size + 200;
	void *out = malloc(max_out_bytes);
	void *metadata = malloc(namelen + 200);
	void *in;
	SHA_CTX c;

	in = "";
	if (size)
		in = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (!out || (int)(long)in == -1)
		return -1;

	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, Z_BEST_COMPRESSION);

	/*
	 * ASCII size + nul byte
	 */	
	stream.next_in = metadata;
	stream.avail_in = 1+sprintf(metadata, "blob %lu", size);
	stream.next_out = out;
	stream.avail_out = max_out_bytes;
	while (deflate(&stream, 0) == Z_OK)
		/* nothing */;

	/*
	 * File content
	 */
	stream.next_in = in;
	stream.avail_in = size;
	while (deflate(&stream, Z_FINISH) == Z_OK)
		/*nothing */;

	deflateEnd(&stream);
	
	SHA1_Init(&c);
	SHA1_Update(&c, out, stream.total_out);
	SHA1_Final(ce->sha1, &c);

	return write_sha1_buffer(ce->sha1, out, stream.total_out);
}

/*
 * This only updates the "non-critical" parts of the directory
 * cache, ie the parts that aren't tracked by GIT, and only used
 * to validate the cache.
 */
static void fill_stat_cache_info(struct cache_entry *ce, struct stat *st)
{
	ce->ce_ctime.sec = htonl(st->st_ctime);
	ce->ce_mtime.sec = htonl(st->st_mtime);
#ifdef NSEC
	ce->ce_ctime.nsec = htonl(st->st_ctim.tv_nsec);
	ce->ce_mtime.nsec = htonl(st->st_mtim.tv_nsec);
#endif
	ce->ce_dev = htonl(st->st_dev);
	ce->ce_ino = htonl(st->st_ino);
	ce->ce_uid = htonl(st->st_uid);
	ce->ce_gid = htonl(st->st_gid);
	ce->ce_size = htonl(st->st_size);
}

static int add_file_to_cache(char *path)
{
	int size, namelen;
	struct cache_entry *ce;
	struct stat st;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT) {
			if (allow_remove)
				return remove_file_from_cache(path);
		}
		return -1;
	}
	if (fstat(fd, &st) < 0) {
		close(fd);
		return -1;
	}
	namelen = strlen(path);
	size = cache_entry_size(namelen);
	ce = malloc(size);
	memset(ce, 0, size);
	memcpy(ce->name, path, namelen);
	fill_stat_cache_info(ce, &st);
	ce->ce_mode = htonl(st.st_mode);
	ce->ce_flags = htons(namelen);

	if (index_fd(path, namelen, ce, fd, &st) < 0)
		return -1;

	return add_cache_entry(ce, allow_add);
}

static int match_data(int fd, void *buffer, unsigned long size)
{
	while (size) {
		char compare[1024];
		int ret = read(fd, compare, sizeof(compare));

		if (ret <= 0 || ret > size || memcmp(buffer, compare, ret))
			return -1;
		size -= ret;
		buffer += ret;
	}
	return 0;
}

static int compare_data(struct cache_entry *ce, unsigned long expected_size)
{
	int match = -1;
	int fd = open(ce->name, O_RDONLY);

	if (fd >= 0) {
		void *buffer;
		unsigned long size;
		char type[10];

		buffer = read_sha1_file(ce->sha1, type, &size);
		if (buffer) {
			if (size == expected_size && !strcmp(type, "blob"))
				match = match_data(fd, buffer, size);
			free(buffer);
		}
		close(fd);
	}
	return match;
}

/*
 * "refresh" does not calculate a new sha1 file or bring the
 * cache up-to-date for mode/content changes. But what it
 * _does_ do is to "re-match" the stat information of a file
 * with the cache, so that you can refresh the cache for a
 * file that hasn't been changed but where the stat entry is
 * out of date.
 *
 * For example, you'd want to do this after doing a "read-tree",
 * to link up the stat cache details with the proper files.
 */
static struct cache_entry *refresh_entry(struct cache_entry *ce)
{
	struct stat st;
	struct cache_entry *updated;
	int changed, size;

	if (stat(ce->name, &st) < 0)
		return NULL;

	changed = cache_match_stat(ce, &st);
	if (!changed)
		return ce;

	/*
	 * If the mode has changed, there's no point in trying
	 * to refresh the entry - it's not going to match
	 */
	if (changed & MODE_CHANGED)
		return NULL;

	if (compare_data(ce, st.st_size))
		return NULL;

	size = ce_size(ce);
	updated = malloc(size);
	memcpy(updated, ce, size);
	fill_stat_cache_info(updated, &st);
	return updated;
}

static void refresh_cache(void)
{
	int i;

	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		struct cache_entry *new = refresh_entry(ce);

		if (!new) {
			printf("%s: needs update\n", ce->name);
			continue;
		}
		active_cache[i] = new;
	}
}

/*
 * We fundamentally don't like some paths: we don't want
 * dot or dot-dot anywhere, and in fact, we don't even want
 * any other dot-files (.git or anything else). They
 * are hidden, for chist sake.
 *
 * Also, we don't want double slashes or slashes at the
 * end that can make pathnames ambiguous.
 */
static int verify_path(char *path)
{
	char c;

	goto inside;
	for (;;) {
		if (!c)
			return 1;
		if (c == '/') {
inside:
			c = *path++;
			if (c != '/' && c != '.' && c != '\0')
				continue;
			return 0;
		}
		c = *path++;
	}
}

static int add_cacheinfo(char *arg1, char *arg2, char *arg3)
{
	int size, len;
	unsigned int mode;
	unsigned char sha1[20];
	struct cache_entry *ce;

	if (sscanf(arg1, "%o", &mode) != 1)
		return -1;
	printf("got mode %o\n", mode);
	if (get_sha1_hex(arg2, sha1))
		return -1;
	printf("got sha1 %s\n", sha1_to_hex(sha1));
	if (!verify_path(arg3))
		return -1;
	printf("got path %s\n", arg3);

	len = strlen(arg3);
	size = cache_entry_size(len);
	ce = malloc(size);
	memset(ce, 0, size);

	memcpy(ce->sha1, sha1, 20);
	memcpy(ce->name, arg3, len);
	ce->ce_flags = htons(len);
	ce->ce_mode = htonl(mode);
	return add_cache_entry(ce, allow_add);
}

static int remove_lock = 0;

static void remove_lock_file(void)
{
	if (remove_lock)
		unlink(".git/index.lock");
}

int main(int argc, char **argv)
{
	int i, newfd, entries;
	int allow_options = 1;

	newfd = open(".git/index.lock", O_RDWR | O_CREAT | O_EXCL, 0600);
	if (newfd < 0)
		die("unable to create new cachefile");

	atexit(remove_lock_file);
	remove_lock = 1;

	entries = read_cache();
	if (entries < 0)
		die("cache corrupted");

	for (i = 1 ; i < argc; i++) {
		char *path = argv[i];

		if (allow_options && *path == '-') {
			if (!strcmp(path, "--")) {
				allow_options = 0;
				continue;
			}
			if (!strcmp(path, "--add")) {
				allow_add = 1;
				continue;
			}
			if (!strcmp(path, "--remove")) {
				allow_remove = 1;
				continue;
			}
			if (!strcmp(path, "--refresh")) {
				refresh_cache();
				continue;
			}
			if (!strcmp(path, "--cacheinfo")) {
				if (i+3 >= argc || add_cacheinfo(argv[i+1], argv[i+2], argv[i+3]))
					die("update-cache: --cacheinfo <mode> <sha1> <path>");
				i += 3;
				continue;
			}
			die("unknown option %s", path);
		}
		if (!verify_path(path)) {
			fprintf(stderr, "Ignoring path %s\n", argv[i]);
			continue;
		}
		if (add_file_to_cache(path))
			die("Unable to add %s to database", path);
	}
	if (write_cache(newfd, active_cache, active_nr) ||
	    rename(".git/index.lock", ".git/index"))
		die("Unable to write new cachefile");

	remove_lock = 0;
	return 0;
}
