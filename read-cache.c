/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

struct cache_entry **active_cache = NULL;
unsigned int active_nr = 0, active_alloc = 0, active_cache_changed = 0;

/*
 * This only updates the "non-critical" parts of the directory
 * cache, ie the parts that aren't tracked by GIT, and only used
 * to validate the cache.
 */
void fill_stat_cache_info(struct cache_entry *ce, struct stat *st)
{
	ce->ce_ctime.sec = htonl(st->st_ctime);
	ce->ce_mtime.sec = htonl(st->st_mtime);
#ifdef USE_NSEC
	ce->ce_ctime.nsec = htonl(st->st_ctim.tv_nsec);
	ce->ce_mtime.nsec = htonl(st->st_mtim.tv_nsec);
#endif
	ce->ce_dev = htonl(st->st_dev);
	ce->ce_ino = htonl(st->st_ino);
	ce->ce_uid = htonl(st->st_uid);
	ce->ce_gid = htonl(st->st_gid);
	ce->ce_size = htonl(st->st_size);
}

int ce_match_stat(struct cache_entry *ce, struct stat *st)
{
	unsigned int changed = 0;

	switch (ntohl(ce->ce_mode) & S_IFMT) {
	case S_IFREG:
		changed |= !S_ISREG(st->st_mode) ? TYPE_CHANGED : 0;
		/* We consider only the owner x bit to be relevant for "mode changes" */
		if (0100 & (ntohl(ce->ce_mode) ^ st->st_mode))
			changed |= MODE_CHANGED;
		break;
	case S_IFLNK:
		changed |= !S_ISLNK(st->st_mode) ? TYPE_CHANGED : 0;
		break;
	default:
		die("internal error: ce_mode is %o", ntohl(ce->ce_mode));
	}
	if (ce->ce_mtime.sec != htonl(st->st_mtime))
		changed |= MTIME_CHANGED;
	if (ce->ce_ctime.sec != htonl(st->st_ctime))
		changed |= CTIME_CHANGED;

#ifdef USE_NSEC
	/*
	 * nsec seems unreliable - not all filesystems support it, so
	 * as long as it is in the inode cache you get right nsec
	 * but after it gets flushed, you get zero nsec.
	 */
	if (ce->ce_mtime.nsec != htonl(st->st_mtim.tv_nsec))
		changed |= MTIME_CHANGED;
	if (ce->ce_ctime.nsec != htonl(st->st_ctim.tv_nsec))
		changed |= CTIME_CHANGED;
#endif	

	if (ce->ce_uid != htonl(st->st_uid) ||
	    ce->ce_gid != htonl(st->st_gid))
		changed |= OWNER_CHANGED;
	if (ce->ce_ino != htonl(st->st_ino))
		changed |= INODE_CHANGED;

#ifdef USE_STDEV
	/*
	 * st_dev breaks on network filesystems where different
	 * clients will have different views of what "device"
	 * the filesystem is on
	 */
	if (ce->ce_dev != htonl(st->st_dev))
		changed |= INODE_CHANGED;
#endif

	if (ce->ce_size != htonl(st->st_size))
		changed |= DATA_CHANGED;
	return changed;
}

int base_name_compare(const char *name1, int len1, int mode1,
		      const char *name2, int len2, int mode2)
{
	unsigned char c1, c2;
	int len = len1 < len2 ? len1 : len2;
	int cmp;

	cmp = memcmp(name1, name2, len);
	if (cmp)
		return cmp;
	c1 = name1[len];
	c2 = name2[len];
	if (!c1 && S_ISDIR(mode1))
		c1 = '/';
	if (!c2 && S_ISDIR(mode2))
		c2 = '/';
	return (c1 < c2) ? -1 : (c1 > c2) ? 1 : 0;
}

int cache_name_compare(const char *name1, int flags1, const char *name2, int flags2)
{
	int len1 = flags1 & CE_NAMEMASK;
	int len2 = flags2 & CE_NAMEMASK;
	int len = len1 < len2 ? len1 : len2;
	int cmp;

	cmp = memcmp(name1, name2, len);
	if (cmp)
		return cmp;
	if (len1 < len2)
		return -1;
	if (len1 > len2)
		return 1;
	if (flags1 < flags2)
		return -1;
	if (flags1 > flags2)
		return 1;
	return 0;
}

int cache_name_pos(const char *name, int namelen)
{
	int first, last;

	first = 0;
	last = active_nr;
	while (last > first) {
		int next = (last + first) >> 1;
		struct cache_entry *ce = active_cache[next];
		int cmp = cache_name_compare(name, namelen, ce->name, ntohs(ce->ce_flags));
		if (!cmp)
			return next;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}
	return -first-1;
}

/* Remove entry, return true if there are more entries to go.. */
int remove_cache_entry_at(int pos)
{
	active_cache_changed = 1;
	active_nr--;
	if (pos >= active_nr)
		return 0;
	memmove(active_cache + pos, active_cache + pos + 1, (active_nr - pos) * sizeof(struct cache_entry *));
	return 1;
}

int remove_file_from_cache(char *path)
{
	int pos = cache_name_pos(path, strlen(path));
	if (pos < 0)
		pos = -pos-1;
	while (pos < active_nr && !strcmp(active_cache[pos]->name, path))
		remove_cache_entry_at(pos);
	return 0;
}

int ce_same_name(struct cache_entry *a, struct cache_entry *b)
{
	int len = ce_namelen(a);
	return ce_namelen(b) == len && !memcmp(a->name, b->name, len);
}

int ce_path_match(const struct cache_entry *ce, const char **pathspec)
{
	const char *match, *name;
	int len;

	if (!pathspec)
		return 1;

	len = ce_namelen(ce);
	name = ce->name;
	while ((match = *pathspec++) != NULL) {
		int matchlen = strlen(match);
		if (matchlen > len)
			continue;
		if (memcmp(name, match, matchlen))
			continue;
		if (matchlen && name[matchlen-1] == '/')
			return 1;
		if (name[matchlen] == '/' || !name[matchlen])
			return 1;
	}
	return 0;
}

/*
 * Do we have another file that has the beginning components being a
 * proper superset of the name we're trying to add?
 */
static int has_file_name(const struct cache_entry *ce, int pos, int ok_to_replace)
{
	int retval = 0;
	int len = ce_namelen(ce);
	int stage = ce_stage(ce);
	const char *name = ce->name;

	while (pos < active_nr) {
		struct cache_entry *p = active_cache[pos++];

		if (len >= ce_namelen(p))
			break;
		if (memcmp(name, p->name, len))
			break;
		if (ce_stage(p) != stage)
			continue;
		if (p->name[len] != '/')
			continue;
		retval = -1;
		if (!ok_to_replace)
			break;
		remove_cache_entry_at(--pos);
	}
	return retval;
}

/*
 * Do we have another file with a pathname that is a proper
 * subset of the name we're trying to add?
 */
static int has_dir_name(const struct cache_entry *ce, int pos, int ok_to_replace)
{
	int retval = 0;
	int stage = ce_stage(ce);
	const char *name = ce->name;
	const char *slash = name + ce_namelen(ce);

	for (;;) {
		int len;

		for (;;) {
			if (*--slash == '/')
				break;
			if (slash <= ce->name)
				return retval;
		}
		len = slash - name;

		pos = cache_name_pos(name, ntohs(create_ce_flags(len, stage)));
		if (pos >= 0) {
			retval = -1;
			if (ok_to_replace)
				break;
			remove_cache_entry_at(pos);
			continue;
		}

		/*
		 * Trivial optimization: if we find an entry that
		 * already matches the sub-directory, then we know
		 * we're ok, and we can exit.
		 */
		pos = -pos-1;
		while (pos < active_nr) {
			struct cache_entry *p = active_cache[pos];
			if ((ce_namelen(p) <= len) ||
			    (p->name[len] != '/') ||
			    memcmp(p->name, name, len))
				break; /* not our subdirectory */
			if (ce_stage(p) == stage)
				/* p is at the same stage as our entry, and
				 * is a subdirectory of what we are looking
				 * at, so we cannot have conflicts at our
				 * level or anything shorter.
				 */
				return retval;
			pos++;
		}
	}
	return retval;
}

/* We may be in a situation where we already have path/file and path
 * is being added, or we already have path and path/file is being
 * added.  Either one would result in a nonsense tree that has path
 * twice when git-write-tree tries to write it out.  Prevent it.
 * 
 * If ok-to-replace is specified, we remove the conflicting entries
 * from the cache so the caller should recompute the insert position.
 * When this happens, we return non-zero.
 */
static int check_file_directory_conflict(const struct cache_entry *ce, int pos, int ok_to_replace)
{
	/*
	 * We check if the path is a sub-path of a subsequent pathname
	 * first, since removing those will not change the position
	 * in the array
	 */
	int retval = has_file_name(ce, pos, ok_to_replace);
	/*
	 * Then check if the path might have a clashing sub-directory
	 * before it.
	 */
	return retval + has_dir_name(ce, pos, ok_to_replace);
}

int add_cache_entry(struct cache_entry *ce, int option)
{
	int pos;
	int ok_to_add = option & ADD_CACHE_OK_TO_ADD;
	int ok_to_replace = option & ADD_CACHE_OK_TO_REPLACE;
	int skip_df_check = option & ADD_CACHE_SKIP_DFCHECK;
	pos = cache_name_pos(ce->name, ntohs(ce->ce_flags));

	/* existing match? Just replace it */
	if (pos >= 0) {
		active_cache_changed = 1;
		active_cache[pos] = ce;
		return 0;
	}
	pos = -pos-1;

	/*
	 * Inserting a merged entry ("stage 0") into the index
	 * will always replace all non-merged entries..
	 */
	if (pos < active_nr && ce_stage(ce) == 0) {
		while (ce_same_name(active_cache[pos], ce)) {
			ok_to_add = 1;
			if (!remove_cache_entry_at(pos))
				break;
		}
	}

	if (!ok_to_add)
		return -1;

	if (!skip_df_check && check_file_directory_conflict(ce, pos, ok_to_replace)) {
		if (!ok_to_replace)
			return -1;
		pos = cache_name_pos(ce->name, ntohs(ce->ce_flags));
		pos = -pos-1;
	}

	/* Make sure the array is big enough .. */
	if (active_nr == active_alloc) {
		active_alloc = alloc_nr(active_alloc);
		active_cache = xrealloc(active_cache, active_alloc * sizeof(struct cache_entry *));
	}

	/* Add it in.. */
	active_nr++;
	if (active_nr > pos)
		memmove(active_cache + pos + 1, active_cache + pos, (active_nr - pos - 1) * sizeof(ce));
	active_cache[pos] = ce;
	active_cache_changed = 1;
	return 0;
}

static int verify_hdr(struct cache_header *hdr, unsigned long size)
{
	SHA_CTX c;
	unsigned char sha1[20];

	if (hdr->hdr_signature != htonl(CACHE_SIGNATURE))
		return error("bad signature");
	if (hdr->hdr_version != htonl(2))
		return error("bad index version");
	SHA1_Init(&c);
	SHA1_Update(&c, hdr, size - 20);
	SHA1_Final(sha1, &c);
	if (memcmp(sha1, (void *)hdr + size - 20, 20))
		return error("bad index file sha1 signature");
	return 0;
}

int read_cache(void)
{
	int fd, i;
	struct stat st;
	unsigned long size, offset;
	void *map;
	struct cache_header *hdr;

	errno = EBUSY;
	if (active_cache)
		return error("more than one cachefile");
	errno = ENOENT;
	fd = open(get_index_file(), O_RDONLY);
	if (fd < 0)
		return (errno == ENOENT) ? 0 : error("open failed");

	size = 0; // avoid gcc warning
	map = MAP_FAILED;
	if (!fstat(fd, &st)) {
		size = st.st_size;
		errno = EINVAL;
		if (size >= sizeof(struct cache_header) + 20)
			map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	}
	close(fd);
	if (map == MAP_FAILED)
		return error("mmap failed");

	hdr = map;
	if (verify_hdr(hdr, size) < 0)
		goto unmap;

	active_nr = ntohl(hdr->hdr_entries);
	active_alloc = alloc_nr(active_nr);
	active_cache = calloc(active_alloc, sizeof(struct cache_entry *));

	offset = sizeof(*hdr);
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = map + offset;
		offset = offset + ce_size(ce);
		active_cache[i] = ce;
	}
	return active_nr;

unmap:
	munmap(map, size);
	errno = EINVAL;
	return error("verify header failed");
}

#define WRITE_BUFFER_SIZE 8192
static unsigned char write_buffer[WRITE_BUFFER_SIZE];
static unsigned long write_buffer_len;

static int ce_write(SHA_CTX *context, int fd, void *data, unsigned int len)
{
	while (len) {
		unsigned int buffered = write_buffer_len;
		unsigned int partial = WRITE_BUFFER_SIZE - buffered;
		if (partial > len)
			partial = len;
		memcpy(write_buffer + buffered, data, partial);
		buffered += partial;
		if (buffered == WRITE_BUFFER_SIZE) {
			SHA1_Update(context, write_buffer, WRITE_BUFFER_SIZE);
			if (write(fd, write_buffer, WRITE_BUFFER_SIZE) != WRITE_BUFFER_SIZE)
				return -1;
			buffered = 0;
		}
		write_buffer_len = buffered;
		len -= partial;
		data += partial;
 	}
 	return 0;
}

static int ce_flush(SHA_CTX *context, int fd)
{
	unsigned int left = write_buffer_len;

	if (left) {
		write_buffer_len = 0;
		SHA1_Update(context, write_buffer, left);
	}

	/* Append the SHA1 signature at the end */
	SHA1_Final(write_buffer + left, context);
	left += 20;
	if (write(fd, write_buffer, left) != left)
		return -1;
	return 0;
}

int write_cache(int newfd, struct cache_entry **cache, int entries)
{
	SHA_CTX c;
	struct cache_header hdr;
	int i, removed;

	for (i = removed = 0; i < entries; i++)
		if (!cache[i]->ce_mode)
			removed++;

	hdr.hdr_signature = htonl(CACHE_SIGNATURE);
	hdr.hdr_version = htonl(2);
	hdr.hdr_entries = htonl(entries - removed);

	SHA1_Init(&c);
	if (ce_write(&c, newfd, &hdr, sizeof(hdr)) < 0)
		return -1;

	for (i = 0; i < entries; i++) {
		struct cache_entry *ce = cache[i];
		if (!ce->ce_mode)
			continue;
		if (ce_write(&c, newfd, ce, ce_size(ce)) < 0)
			return -1;
	}
	return ce_flush(&c, newfd);
}
