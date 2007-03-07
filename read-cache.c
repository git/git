/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "cache-tree.h"

/* Index extensions.
 *
 * The first letter should be 'A'..'Z' for extensions that are not
 * necessary for a correct operation (i.e. optimization data).
 * When new extensions are added that _needs_ to be understood in
 * order to correctly interpret the index file, pick character that
 * is outside the range, to cause the reader to abort.
 */

#define CACHE_EXT(s) ( (s[0]<<24)|(s[1]<<16)|(s[2]<<8)|(s[3]) )
#define CACHE_EXT_TREE 0x54524545	/* "TREE" */

struct cache_entry **active_cache;
static time_t index_file_timestamp;
unsigned int active_nr, active_alloc, active_cache_changed;

struct cache_tree *active_cache_tree;

int cache_errno;

static void *cache_mmap;
static size_t cache_mmap_size;

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

	if (assume_unchanged)
		ce->ce_flags |= htons(CE_VALID);
}

static int ce_compare_data(struct cache_entry *ce, struct stat *st)
{
	int match = -1;
	int fd = open(ce->name, O_RDONLY);

	if (fd >= 0) {
		unsigned char sha1[20];
		if (!index_fd(sha1, fd, st, 0, OBJ_BLOB, ce->name))
			match = hashcmp(sha1, ce->sha1);
		/* index_fd() closed the file descriptor already */
	}
	return match;
}

static int ce_compare_link(struct cache_entry *ce, size_t expected_size)
{
	int match = -1;
	char *target;
	void *buffer;
	unsigned long size;
	enum object_type type;
	int len;

	target = xmalloc(expected_size);
	len = readlink(ce->name, target, expected_size);
	if (len != expected_size) {
		free(target);
		return -1;
	}
	buffer = read_sha1_file(ce->sha1, &type, &size);
	if (!buffer) {
		free(target);
		return -1;
	}
	if (size == expected_size)
		match = memcmp(buffer, target, size);
	free(buffer);
	free(target);
	return match;
}

static int ce_modified_check_fs(struct cache_entry *ce, struct stat *st)
{
	switch (st->st_mode & S_IFMT) {
	case S_IFREG:
		if (ce_compare_data(ce, st))
			return DATA_CHANGED;
		break;
	case S_IFLNK:
		if (ce_compare_link(ce, xsize_t(st->st_size)))
			return DATA_CHANGED;
		break;
	default:
		return TYPE_CHANGED;
	}
	return 0;
}

static int ce_match_stat_basic(struct cache_entry *ce, struct stat *st)
{
	unsigned int changed = 0;

	switch (ntohl(ce->ce_mode) & S_IFMT) {
	case S_IFREG:
		changed |= !S_ISREG(st->st_mode) ? TYPE_CHANGED : 0;
		/* We consider only the owner x bit to be relevant for
		 * "mode changes"
		 */
		if (trust_executable_bit &&
		    (0100 & (ntohl(ce->ce_mode) ^ st->st_mode)))
			changed |= MODE_CHANGED;
		break;
	case S_IFLNK:
		if (!S_ISLNK(st->st_mode) &&
		    (has_symlinks || !S_ISREG(st->st_mode)))
			changed |= TYPE_CHANGED;
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

int ce_match_stat(struct cache_entry *ce, struct stat *st, int options)
{
	unsigned int changed;
	int ignore_valid = options & 01;
	int assume_racy_is_modified = options & 02;

	/*
	 * If it's marked as always valid in the index, it's
	 * valid whatever the checked-out copy says.
	 */
	if (!ignore_valid && (ce->ce_flags & htons(CE_VALID)))
		return 0;

	changed = ce_match_stat_basic(ce, st);

	/*
	 * Within 1 second of this sequence:
	 * 	echo xyzzy >file && git-update-index --add file
	 * running this command:
	 * 	echo frotz >file
	 * would give a falsely clean cache entry.  The mtime and
	 * length match the cache, and other stat fields do not change.
	 *
	 * We could detect this at update-index time (the cache entry
	 * being registered/updated records the same time as "now")
	 * and delay the return from git-update-index, but that would
	 * effectively mean we can make at most one commit per second,
	 * which is not acceptable.  Instead, we check cache entries
	 * whose mtime are the same as the index file timestamp more
	 * carefully than others.
	 */
	if (!changed &&
	    index_file_timestamp &&
	    index_file_timestamp <= ntohl(ce->ce_mtime.sec)) {
		if (assume_racy_is_modified)
			changed |= DATA_CHANGED;
		else
			changed |= ce_modified_check_fs(ce, st);
	}

	return changed;
}

int ce_modified(struct cache_entry *ce, struct stat *st, int really)
{
	int changed, changed_fs;
	changed = ce_match_stat(ce, st, really);
	if (!changed)
		return 0;
	/*
	 * If the mode or type has changed, there's no point in trying
	 * to refresh the entry - it's not going to match
	 */
	if (changed & (MODE_CHANGED | TYPE_CHANGED))
		return changed;

	/* Immediately after read-tree or update-index --cacheinfo,
	 * the length field is zero.  For other cases the ce_size
	 * should match the SHA1 recorded in the index entry.
	 */
	if ((changed & DATA_CHANGED) && ce->ce_size != htonl(0))
		return changed;

	changed_fs = ce_modified_check_fs(ce, st);
	if (changed_fs)
		return changed | changed_fs;
	return 0;
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

	/* Compare stages  */
	flags1 &= CE_STAGEMASK;
	flags2 &= CE_STAGEMASK;

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

int remove_file_from_cache(const char *path)
{
	int pos = cache_name_pos(path, strlen(path));
	if (pos < 0)
		pos = -pos-1;
	while (pos < active_nr && !strcmp(active_cache[pos]->name, path))
		remove_cache_entry_at(pos);
	return 0;
}

int add_file_to_index(const char *path, int verbose)
{
	int size, namelen;
	struct stat st;
	struct cache_entry *ce;

	if (lstat(path, &st))
		die("%s: unable to stat (%s)", path, strerror(errno));

	if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode))
		die("%s: can only add regular files or symbolic links", path);

	namelen = strlen(path);
	size = cache_entry_size(namelen);
	ce = xcalloc(1, size);
	memcpy(ce->name, path, namelen);
	ce->ce_flags = htons(namelen);
	fill_stat_cache_info(ce, &st);

	if (trust_executable_bit && has_symlinks)
		ce->ce_mode = create_ce_mode(st.st_mode);
	else {
		/* If there is an existing entry, pick the mode bits and type
		 * from it, otherwise assume unexecutable regular file.
		 */
		struct cache_entry *ent;
		int pos = cache_name_pos(path, namelen);

		ent = (0 <= pos) ? active_cache[pos] : NULL;
		ce->ce_mode = ce_mode_from_stat(ent, st.st_mode);
	}

	if (index_path(ce->sha1, path, &st, 1))
		die("unable to index file %s", path);
	if (add_cache_entry(ce, ADD_CACHE_OK_TO_ADD|ADD_CACHE_OK_TO_REPLACE))
		die("unable to add %s to index",path);
	if (verbose)
		printf("add '%s'\n", path);
	cache_tree_invalidate_path(active_cache_tree, path);
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
		if (!matchlen)
			return 1;
	}
	return 0;
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

int verify_path(const char *path)
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
			if (!ok_to_replace)
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

	/* existing match? Just replace it. */
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
	if (!verify_path(ce->name))
		return -1;

	if (!skip_df_check &&
	    check_file_directory_conflict(ce, pos, ok_to_replace)) {
		if (!ok_to_replace)
			return error("'%s' appears as both a file and as a directory", ce->name);
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
struct cache_entry *refresh_cache_entry(struct cache_entry *ce, int really)
{
	struct stat st;
	struct cache_entry *updated;
	int changed, size;

	if (lstat(ce->name, &st) < 0) {
		cache_errno = errno;
		return NULL;
	}

	changed = ce_match_stat(ce, &st, really);
	if (!changed) {
		if (really && assume_unchanged &&
		    !(ce->ce_flags & htons(CE_VALID)))
			; /* mark this one VALID again */
		else
			return ce;
	}

	if (ce_modified(ce, &st, really)) {
		cache_errno = EINVAL;
		return NULL;
	}

	size = ce_size(ce);
	updated = xmalloc(size);
	memcpy(updated, ce, size);
	fill_stat_cache_info(updated, &st);

	/* In this case, if really is not set, we should leave
	 * CE_VALID bit alone.  Otherwise, paths marked with
	 * --no-assume-unchanged (i.e. things to be edited) will
	 * reacquire CE_VALID bit automatically, which is not
	 * really what we want.
	 */
	if (!really && assume_unchanged && !(ce->ce_flags & htons(CE_VALID)))
		updated->ce_flags &= ~htons(CE_VALID);

	return updated;
}

int refresh_cache(unsigned int flags)
{
	int i;
	int has_errors = 0;
	int really = (flags & REFRESH_REALLY) != 0;
	int allow_unmerged = (flags & REFRESH_UNMERGED) != 0;
	int quiet = (flags & REFRESH_QUIET) != 0;
	int not_new = (flags & REFRESH_IGNORE_MISSING) != 0;

	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce, *new;
		ce = active_cache[i];
		if (ce_stage(ce)) {
			while ((i < active_nr) &&
			       ! strcmp(active_cache[i]->name, ce->name))
				i++;
			i--;
			if (allow_unmerged)
				continue;
			printf("%s: needs merge\n", ce->name);
			has_errors = 1;
			continue;
		}

		new = refresh_cache_entry(ce, really);
		if (new == ce)
			continue;
		if (!new) {
			if (not_new && cache_errno == ENOENT)
				continue;
			if (really && cache_errno == EINVAL) {
				/* If we are doing --really-refresh that
				 * means the index is not valid anymore.
				 */
				ce->ce_flags &= ~htons(CE_VALID);
				active_cache_changed = 1;
			}
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
	if (hashcmp(sha1, (unsigned char *)hdr + size - 20))
		return error("bad index file sha1 signature");
	return 0;
}

static int read_index_extension(const char *ext, void *data, unsigned long sz)
{
	switch (CACHE_EXT(ext)) {
	case CACHE_EXT_TREE:
		active_cache_tree = cache_tree_read(data, sz);
		break;
	default:
		if (*ext < 'A' || 'Z' < *ext)
			return error("index uses %.4s extension, which we do not understand",
				     ext);
		fprintf(stderr, "ignoring %.4s extension\n", ext);
		break;
	}
	return 0;
}

int read_cache(void)
{
	return read_cache_from(get_index_file());
}

/* remember to discard_cache() before reading a different cache! */
int read_cache_from(const char *path)
{
	int fd, i;
	struct stat st;
	unsigned long offset;
	struct cache_header *hdr;

	errno = EBUSY;
	if (cache_mmap)
		return active_nr;

	errno = ENOENT;
	index_file_timestamp = 0;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT)
			return 0;
		die("index file open failed (%s)", strerror(errno));
	}

	if (!fstat(fd, &st)) {
		cache_mmap_size = xsize_t(st.st_size);
		errno = EINVAL;
		if (cache_mmap_size >= sizeof(struct cache_header) + 20)
			cache_mmap = xmmap(NULL, cache_mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
		else
			die("index file smaller than expected");
	} else
		die("cannot stat the open index (%s)", strerror(errno));
	close(fd);

	hdr = cache_mmap;
	if (verify_hdr(hdr, cache_mmap_size) < 0)
		goto unmap;

	active_nr = ntohl(hdr->hdr_entries);
	active_alloc = alloc_nr(active_nr);
	active_cache = xcalloc(active_alloc, sizeof(struct cache_entry *));

	offset = sizeof(*hdr);
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = (struct cache_entry *) ((char *) cache_mmap + offset);
		offset = offset + ce_size(ce);
		active_cache[i] = ce;
	}
	index_file_timestamp = st.st_mtime;
	while (offset <= cache_mmap_size - 20 - 8) {
		/* After an array of active_nr index entries,
		 * there can be arbitrary number of extended
		 * sections, each of which is prefixed with
		 * extension name (4-byte) and section length
		 * in 4-byte network byte order.
		 */
		unsigned long extsize;
		memcpy(&extsize, (char *) cache_mmap + offset + 4, 4);
		extsize = ntohl(extsize);
		if (read_index_extension(((const char *) cache_mmap) + offset,
					 (char *) cache_mmap + offset + 8,
					 extsize) < 0)
			goto unmap;
		offset += 8;
		offset += extsize;
	}
	return active_nr;

unmap:
	munmap(cache_mmap, cache_mmap_size);
	errno = EINVAL;
	die("index file corrupt");
}

int discard_cache(void)
{
	int ret;

	active_nr = active_cache_changed = 0;
	index_file_timestamp = 0;
	cache_tree_free(&active_cache_tree);
	if (cache_mmap == NULL)
		return 0;
	ret = munmap(cache_mmap, cache_mmap_size);
	cache_mmap = NULL;
	cache_mmap_size = 0;

	/* no need to throw away allocated active_cache */
	return ret;
}

#define WRITE_BUFFER_SIZE 8192
static unsigned char write_buffer[WRITE_BUFFER_SIZE];
static unsigned long write_buffer_len;

static int ce_write_flush(SHA_CTX *context, int fd)
{
	unsigned int buffered = write_buffer_len;
	if (buffered) {
		SHA1_Update(context, write_buffer, buffered);
		if (write_in_full(fd, write_buffer, buffered) != buffered)
			return -1;
		write_buffer_len = 0;
	}
	return 0;
}

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
			write_buffer_len = buffered;
			if (ce_write_flush(context, fd))
				return -1;
			buffered = 0;
		}
		write_buffer_len = buffered;
		len -= partial;
		data = (char *) data + partial;
 	}
 	return 0;
}

static int write_index_ext_header(SHA_CTX *context, int fd,
				  unsigned int ext, unsigned int sz)
{
	ext = htonl(ext);
	sz = htonl(sz);
	return ((ce_write(context, fd, &ext, 4) < 0) ||
		(ce_write(context, fd, &sz, 4) < 0)) ? -1 : 0;
}

static int ce_flush(SHA_CTX *context, int fd)
{
	unsigned int left = write_buffer_len;

	if (left) {
		write_buffer_len = 0;
		SHA1_Update(context, write_buffer, left);
	}

	/* Flush first if not enough space for SHA1 signature */
	if (left + 20 > WRITE_BUFFER_SIZE) {
		if (write_in_full(fd, write_buffer, left) != left)
			return -1;
		left = 0;
	}

	/* Append the SHA1 signature at the end */
	SHA1_Final(write_buffer + left, context);
	left += 20;
	return (write_in_full(fd, write_buffer, left) != left) ? -1 : 0;
}

static void ce_smudge_racily_clean_entry(struct cache_entry *ce)
{
	/*
	 * The only thing we care about in this function is to smudge the
	 * falsely clean entry due to touch-update-touch race, so we leave
	 * everything else as they are.  We are called for entries whose
	 * ce_mtime match the index file mtime.
	 */
	struct stat st;

	if (lstat(ce->name, &st) < 0)
		return;
	if (ce_match_stat_basic(ce, &st))
		return;
	if (ce_modified_check_fs(ce, &st)) {
		/* This is "racily clean"; smudge it.  Note that this
		 * is a tricky code.  At first glance, it may appear
		 * that it can break with this sequence:
		 *
		 * $ echo xyzzy >frotz
		 * $ git-update-index --add frotz
		 * $ : >frotz
		 * $ sleep 3
		 * $ echo filfre >nitfol
		 * $ git-update-index --add nitfol
		 *
		 * but it does not.  When the second update-index runs,
		 * it notices that the entry "frotz" has the same timestamp
		 * as index, and if we were to smudge it by resetting its
		 * size to zero here, then the object name recorded
		 * in index is the 6-byte file but the cached stat information
		 * becomes zero --- which would then match what we would
		 * obtain from the filesystem next time we stat("frotz"). 
		 *
		 * However, the second update-index, before calling
		 * this function, notices that the cached size is 6
		 * bytes and what is on the filesystem is an empty
		 * file, and never calls us, so the cached size information
		 * for "frotz" stays 6 which does not match the filesystem.
		 */
		ce->ce_size = htonl(0);
	}
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
		if (index_file_timestamp &&
		    index_file_timestamp <= ntohl(ce->ce_mtime.sec))
			ce_smudge_racily_clean_entry(ce);
		if (ce_write(&c, newfd, ce, ce_size(ce)) < 0)
			return -1;
	}

	/* Write extension data here */
	if (active_cache_tree) {
		unsigned long sz;
		void *data = cache_tree_write(active_cache_tree, &sz);
		if (data &&
		    !write_index_ext_header(&c, newfd, CACHE_EXT_TREE, sz) &&
		    !ce_write(&c, newfd, data, sz))
			free(data);
		else {
			free(data);
			return -1;
		}
	}
	return ce_flush(&c, newfd);
}
