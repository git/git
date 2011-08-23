/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 *
 * This handles basic git sha1 object files - packing, unpacking,
 * creation etc.
 */
#include "cache.h"
#include "delta.h"
#include "pack.h"
#include "blob.h"
#include "commit.h"
#include "run-command.h"
#include "tag.h"
#include "tree.h"
#include "tree-walk.h"
#include "refs.h"
#include "pack-revindex.h"
#include "sha1-lookup.h"

#ifndef O_NOATIME
#if defined(__linux__) && (defined(__i386__) || defined(__PPC__))
#define O_NOATIME 01000000
#else
#define O_NOATIME 0
#endif
#endif

#define SZ_FMT PRIuMAX
static inline uintmax_t sz_fmt(size_t s) { return s; }

const unsigned char null_sha1[20];

/*
 * This is meant to hold a *small* number of objects that you would
 * want read_sha1_file() to be able to return, but yet you do not want
 * to write them into the object store (e.g. a browse-only
 * application).
 */
static struct cached_object {
	unsigned char sha1[20];
	enum object_type type;
	void *buf;
	unsigned long size;
} *cached_objects;
static int cached_object_nr, cached_object_alloc;

static struct cached_object empty_tree = {
	EMPTY_TREE_SHA1_BIN_LITERAL,
	OBJ_TREE,
	"",
	0
};

static struct cached_object *find_cached_object(const unsigned char *sha1)
{
	int i;
	struct cached_object *co = cached_objects;

	for (i = 0; i < cached_object_nr; i++, co++) {
		if (!hashcmp(co->sha1, sha1))
			return co;
	}
	if (!hashcmp(sha1, empty_tree.sha1))
		return &empty_tree;
	return NULL;
}

int mkdir_in_gitdir(const char *path)
{
	if (mkdir(path, 0777)) {
		int saved_errno = errno;
		struct stat st;
		struct strbuf sb = STRBUF_INIT;

		if (errno != EEXIST)
			return -1;
		/*
		 * Are we looking at a path in a symlinked worktree
		 * whose original repository does not yet have it?
		 * e.g. .git/rr-cache pointing at its original
		 * repository in which the user hasn't performed any
		 * conflict resolution yet?
		 */
		if (lstat(path, &st) || !S_ISLNK(st.st_mode) ||
		    strbuf_readlink(&sb, path, st.st_size) ||
		    !is_absolute_path(sb.buf) ||
		    mkdir(sb.buf, 0777)) {
			strbuf_release(&sb);
			errno = saved_errno;
			return -1;
		}
		strbuf_release(&sb);
	}
	return adjust_shared_perm(path);
}

int safe_create_leading_directories(char *path)
{
	char *pos = path + offset_1st_component(path);
	struct stat st;

	while (pos) {
		pos = strchr(pos, '/');
		if (!pos)
			break;
		while (*++pos == '/')
			;
		if (!*pos)
			break;
		*--pos = '\0';
		if (!stat(path, &st)) {
			/* path exists */
			if (!S_ISDIR(st.st_mode)) {
				*pos = '/';
				return -3;
			}
		}
		else if (mkdir(path, 0777)) {
			*pos = '/';
			return -1;
		}
		else if (adjust_shared_perm(path)) {
			*pos = '/';
			return -2;
		}
		*pos++ = '/';
	}
	return 0;
}

int safe_create_leading_directories_const(const char *path)
{
	/* path points to cache entries, so xstrdup before messing with it */
	char *buf = xstrdup(path);
	int result = safe_create_leading_directories(buf);
	free(buf);
	return result;
}

static void fill_sha1_path(char *pathbuf, const unsigned char *sha1)
{
	int i;
	for (i = 0; i < 20; i++) {
		static char hex[] = "0123456789abcdef";
		unsigned int val = sha1[i];
		char *pos = pathbuf + i*2 + (i > 0);
		*pos++ = hex[val >> 4];
		*pos = hex[val & 0xf];
	}
}

/*
 * NOTE! This returns a statically allocated buffer, so you have to be
 * careful about using it. Do an "xstrdup()" if you need to save the
 * filename.
 *
 * Also note that this returns the location for creating.  Reading
 * SHA1 file can happen from any alternate directory listed in the
 * DB_ENVIRONMENT environment variable if it is not found in
 * the primary object database.
 */
char *sha1_file_name(const unsigned char *sha1)
{
	static char buf[PATH_MAX];
	const char *objdir;
	int len;

	objdir = get_object_directory();
	len = strlen(objdir);

	/* '/' + sha1(2) + '/' + sha1(38) + '\0' */
	if (len + 43 > PATH_MAX)
		die("insanely long object directory %s", objdir);
	memcpy(buf, objdir, len);
	buf[len] = '/';
	buf[len+3] = '/';
	buf[len+42] = '\0';
	fill_sha1_path(buf + len + 1, sha1);
	return buf;
}

static char *sha1_get_pack_name(const unsigned char *sha1,
				char **name, char **base, const char *which)
{
	static const char hex[] = "0123456789abcdef";
	char *buf;
	int i;

	if (!*base) {
		const char *sha1_file_directory = get_object_directory();
		int len = strlen(sha1_file_directory);
		*base = xmalloc(len + 60);
		sprintf(*base, "%s/pack/pack-1234567890123456789012345678901234567890.%s",
			sha1_file_directory, which);
		*name = *base + len + 11;
	}

	buf = *name;

	for (i = 0; i < 20; i++) {
		unsigned int val = *sha1++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}

	return *base;
}

char *sha1_pack_name(const unsigned char *sha1)
{
	static char *name, *base;

	return sha1_get_pack_name(sha1, &name, &base, "pack");
}

char *sha1_pack_index_name(const unsigned char *sha1)
{
	static char *name, *base;

	return sha1_get_pack_name(sha1, &name, &base, "idx");
}

struct alternate_object_database *alt_odb_list;
static struct alternate_object_database **alt_odb_tail;

static void read_info_alternates(const char * alternates, int depth);
static int git_open_noatime(const char *name);

/*
 * Prepare alternate object database registry.
 *
 * The variable alt_odb_list points at the list of struct
 * alternate_object_database.  The elements on this list come from
 * non-empty elements from colon separated ALTERNATE_DB_ENVIRONMENT
 * environment variable, and $GIT_OBJECT_DIRECTORY/info/alternates,
 * whose contents is similar to that environment variable but can be
 * LF separated.  Its base points at a statically allocated buffer that
 * contains "/the/directory/corresponding/to/.git/objects/...", while
 * its name points just after the slash at the end of ".git/objects/"
 * in the example above, and has enough space to hold 40-byte hex
 * SHA1, an extra slash for the first level indirection, and the
 * terminating NUL.
 */
static int link_alt_odb_entry(const char * entry, int len, const char * relative_base, int depth)
{
	const char *objdir = get_object_directory();
	struct alternate_object_database *ent;
	struct alternate_object_database *alt;
	/* 43 = 40-byte + 2 '/' + terminating NUL */
	int pfxlen = len;
	int entlen = pfxlen + 43;
	int base_len = -1;

	if (!is_absolute_path(entry) && relative_base) {
		/* Relative alt-odb */
		if (base_len < 0)
			base_len = strlen(relative_base) + 1;
		entlen += base_len;
		pfxlen += base_len;
	}
	ent = xmalloc(sizeof(*ent) + entlen);

	if (!is_absolute_path(entry) && relative_base) {
		memcpy(ent->base, relative_base, base_len - 1);
		ent->base[base_len - 1] = '/';
		memcpy(ent->base + base_len, entry, len);
	}
	else
		memcpy(ent->base, entry, pfxlen);

	ent->name = ent->base + pfxlen + 1;
	ent->base[pfxlen + 3] = '/';
	ent->base[pfxlen] = ent->base[entlen-1] = 0;

	/* Detect cases where alternate disappeared */
	if (!is_directory(ent->base)) {
		error("object directory %s does not exist; "
		      "check .git/objects/info/alternates.",
		      ent->base);
		free(ent);
		return -1;
	}

	/* Prevent the common mistake of listing the same
	 * thing twice, or object directory itself.
	 */
	for (alt = alt_odb_list; alt; alt = alt->next) {
		if (!memcmp(ent->base, alt->base, pfxlen)) {
			free(ent);
			return -1;
		}
	}
	if (!memcmp(ent->base, objdir, pfxlen)) {
		free(ent);
		return -1;
	}

	/* add the alternate entry */
	*alt_odb_tail = ent;
	alt_odb_tail = &(ent->next);
	ent->next = NULL;

	/* recursively add alternates */
	read_info_alternates(ent->base, depth + 1);

	ent->base[pfxlen] = '/';

	return 0;
}

static void link_alt_odb_entries(const char *alt, const char *ep, int sep,
				 const char *relative_base, int depth)
{
	const char *cp, *last;

	if (depth > 5) {
		error("%s: ignoring alternate object stores, nesting too deep.",
				relative_base);
		return;
	}

	last = alt;
	while (last < ep) {
		cp = last;
		if (cp < ep && *cp == '#') {
			while (cp < ep && *cp != sep)
				cp++;
			last = cp + 1;
			continue;
		}
		while (cp < ep && *cp != sep)
			cp++;
		if (last != cp) {
			if (!is_absolute_path(last) && depth) {
				error("%s: ignoring relative alternate object store %s",
						relative_base, last);
			} else {
				link_alt_odb_entry(last, cp - last,
						relative_base, depth);
			}
		}
		while (cp < ep && *cp == sep)
			cp++;
		last = cp;
	}
}

static void read_info_alternates(const char * relative_base, int depth)
{
	char *map;
	size_t mapsz;
	struct stat st;
	const char alt_file_name[] = "info/alternates";
	/* Given that relative_base is no longer than PATH_MAX,
	   ensure that "path" has enough space to append "/", the
	   file name, "info/alternates", and a trailing NUL.  */
	char path[PATH_MAX + 1 + sizeof alt_file_name];
	int fd;

	sprintf(path, "%s/%s", relative_base, alt_file_name);
	fd = git_open_noatime(path);
	if (fd < 0)
		return;
	if (fstat(fd, &st) || (st.st_size == 0)) {
		close(fd);
		return;
	}
	mapsz = xsize_t(st.st_size);
	map = xmmap(NULL, mapsz, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	link_alt_odb_entries(map, map + mapsz, '\n', relative_base, depth);

	munmap(map, mapsz);
}

void add_to_alternates_file(const char *reference)
{
	struct lock_file *lock = xcalloc(1, sizeof(struct lock_file));
	int fd = hold_lock_file_for_append(lock, git_path("objects/info/alternates"), LOCK_DIE_ON_ERROR);
	char *alt = mkpath("%s/objects\n", reference);
	write_or_die(fd, alt, strlen(alt));
	if (commit_lock_file(lock))
		die("could not close alternates file");
	if (alt_odb_tail)
		link_alt_odb_entries(alt, alt + strlen(alt), '\n', NULL, 0);
}

void foreach_alt_odb(alt_odb_fn fn, void *cb)
{
	struct alternate_object_database *ent;

	prepare_alt_odb();
	for (ent = alt_odb_list; ent; ent = ent->next)
		if (fn(ent, cb))
			return;
}

void prepare_alt_odb(void)
{
	const char *alt;

	if (alt_odb_tail)
		return;

	alt = getenv(ALTERNATE_DB_ENVIRONMENT);
	if (!alt) alt = "";

	alt_odb_tail = &alt_odb_list;
	link_alt_odb_entries(alt, alt + strlen(alt), PATH_SEP, NULL, 0);

	read_info_alternates(get_object_directory(), 0);
}

static int has_loose_object_local(const unsigned char *sha1)
{
	char *name = sha1_file_name(sha1);
	return !access(name, F_OK);
}

int has_loose_object_nonlocal(const unsigned char *sha1)
{
	struct alternate_object_database *alt;
	prepare_alt_odb();
	for (alt = alt_odb_list; alt; alt = alt->next) {
		fill_sha1_path(alt->name, sha1);
		if (!access(alt->base, F_OK))
			return 1;
	}
	return 0;
}

static int has_loose_object(const unsigned char *sha1)
{
	return has_loose_object_local(sha1) ||
	       has_loose_object_nonlocal(sha1);
}

static unsigned int pack_used_ctr;
static unsigned int pack_mmap_calls;
static unsigned int peak_pack_open_windows;
static unsigned int pack_open_windows;
static unsigned int pack_open_fds;
static unsigned int pack_max_fds;
static size_t peak_pack_mapped;
static size_t pack_mapped;
struct packed_git *packed_git;

void pack_report(void)
{
	fprintf(stderr,
		"pack_report: getpagesize()            = %10" SZ_FMT "\n"
		"pack_report: core.packedGitWindowSize = %10" SZ_FMT "\n"
		"pack_report: core.packedGitLimit      = %10" SZ_FMT "\n",
		sz_fmt(getpagesize()),
		sz_fmt(packed_git_window_size),
		sz_fmt(packed_git_limit));
	fprintf(stderr,
		"pack_report: pack_used_ctr            = %10u\n"
		"pack_report: pack_mmap_calls          = %10u\n"
		"pack_report: pack_open_windows        = %10u / %10u\n"
		"pack_report: pack_mapped              = "
			"%10" SZ_FMT " / %10" SZ_FMT "\n",
		pack_used_ctr,
		pack_mmap_calls,
		pack_open_windows, peak_pack_open_windows,
		sz_fmt(pack_mapped), sz_fmt(peak_pack_mapped));
}

static int check_packed_git_idx(const char *path,  struct packed_git *p)
{
	void *idx_map;
	struct pack_idx_header *hdr;
	size_t idx_size;
	uint32_t version, nr, i, *index;
	int fd = git_open_noatime(path);
	struct stat st;

	if (fd < 0)
		return -1;
	if (fstat(fd, &st)) {
		close(fd);
		return -1;
	}
	idx_size = xsize_t(st.st_size);
	if (idx_size < 4 * 256 + 20 + 20) {
		close(fd);
		return error("index file %s is too small", path);
	}
	idx_map = xmmap(NULL, idx_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	hdr = idx_map;
	if (hdr->idx_signature == htonl(PACK_IDX_SIGNATURE)) {
		version = ntohl(hdr->idx_version);
		if (version < 2 || version > 2) {
			munmap(idx_map, idx_size);
			return error("index file %s is version %"PRIu32
				     " and is not supported by this binary"
				     " (try upgrading GIT to a newer version)",
				     path, version);
		}
	} else
		version = 1;

	nr = 0;
	index = idx_map;
	if (version > 1)
		index += 2;  /* skip index header */
	for (i = 0; i < 256; i++) {
		uint32_t n = ntohl(index[i]);
		if (n < nr) {
			munmap(idx_map, idx_size);
			return error("non-monotonic index %s", path);
		}
		nr = n;
	}

	if (version == 1) {
		/*
		 * Total size:
		 *  - 256 index entries 4 bytes each
		 *  - 24-byte entries * nr (20-byte sha1 + 4-byte offset)
		 *  - 20-byte SHA1 of the packfile
		 *  - 20-byte SHA1 file checksum
		 */
		if (idx_size != 4*256 + nr * 24 + 20 + 20) {
			munmap(idx_map, idx_size);
			return error("wrong index v1 file size in %s", path);
		}
	} else if (version == 2) {
		/*
		 * Minimum size:
		 *  - 8 bytes of header
		 *  - 256 index entries 4 bytes each
		 *  - 20-byte sha1 entry * nr
		 *  - 4-byte crc entry * nr
		 *  - 4-byte offset entry * nr
		 *  - 20-byte SHA1 of the packfile
		 *  - 20-byte SHA1 file checksum
		 * And after the 4-byte offset table might be a
		 * variable sized table containing 8-byte entries
		 * for offsets larger than 2^31.
		 */
		unsigned long min_size = 8 + 4*256 + nr*(20 + 4 + 4) + 20 + 20;
		unsigned long max_size = min_size;
		if (nr)
			max_size += (nr - 1)*8;
		if (idx_size < min_size || idx_size > max_size) {
			munmap(idx_map, idx_size);
			return error("wrong index v2 file size in %s", path);
		}
		if (idx_size != min_size &&
		    /*
		     * make sure we can deal with large pack offsets.
		     * 31-bit signed offset won't be enough, neither
		     * 32-bit unsigned one will be.
		     */
		    (sizeof(off_t) <= 4)) {
			munmap(idx_map, idx_size);
			return error("pack too large for current definition of off_t in %s", path);
		}
	}

	p->index_version = version;
	p->index_data = idx_map;
	p->index_size = idx_size;
	p->num_objects = nr;
	return 0;
}

int open_pack_index(struct packed_git *p)
{
	char *idx_name;
	int ret;

	if (p->index_data)
		return 0;

	idx_name = xstrdup(p->pack_name);
	strcpy(idx_name + strlen(idx_name) - strlen(".pack"), ".idx");
	ret = check_packed_git_idx(idx_name, p);
	free(idx_name);
	return ret;
}

static void scan_windows(struct packed_git *p,
	struct packed_git **lru_p,
	struct pack_window **lru_w,
	struct pack_window **lru_l)
{
	struct pack_window *w, *w_l;

	for (w_l = NULL, w = p->windows; w; w = w->next) {
		if (!w->inuse_cnt) {
			if (!*lru_w || w->last_used < (*lru_w)->last_used) {
				*lru_p = p;
				*lru_w = w;
				*lru_l = w_l;
			}
		}
		w_l = w;
	}
}

static int unuse_one_window(struct packed_git *current, int keep_fd)
{
	struct packed_git *p, *lru_p = NULL;
	struct pack_window *lru_w = NULL, *lru_l = NULL;

	if (current)
		scan_windows(current, &lru_p, &lru_w, &lru_l);
	for (p = packed_git; p; p = p->next)
		scan_windows(p, &lru_p, &lru_w, &lru_l);
	if (lru_p) {
		munmap(lru_w->base, lru_w->len);
		pack_mapped -= lru_w->len;
		if (lru_l)
			lru_l->next = lru_w->next;
		else {
			lru_p->windows = lru_w->next;
			if (!lru_p->windows && lru_p->pack_fd != -1
				&& lru_p->pack_fd != keep_fd) {
				close(lru_p->pack_fd);
				pack_open_fds--;
				lru_p->pack_fd = -1;
			}
		}
		free(lru_w);
		pack_open_windows--;
		return 1;
	}
	return 0;
}

void release_pack_memory(size_t need, int fd)
{
	size_t cur = pack_mapped;
	while (need >= (cur - pack_mapped) && unuse_one_window(NULL, fd))
		; /* nothing */
}

void *xmmap(void *start, size_t length,
	int prot, int flags, int fd, off_t offset)
{
	void *ret = mmap(start, length, prot, flags, fd, offset);
	if (ret == MAP_FAILED) {
		if (!length)
			return NULL;
		release_pack_memory(length, fd);
		ret = mmap(start, length, prot, flags, fd, offset);
		if (ret == MAP_FAILED)
			die_errno("Out of memory? mmap failed");
	}
	return ret;
}

void close_pack_windows(struct packed_git *p)
{
	while (p->windows) {
		struct pack_window *w = p->windows;

		if (w->inuse_cnt)
			die("pack '%s' still has open windows to it",
			    p->pack_name);
		munmap(w->base, w->len);
		pack_mapped -= w->len;
		pack_open_windows--;
		p->windows = w->next;
		free(w);
	}
}

void unuse_pack(struct pack_window **w_cursor)
{
	struct pack_window *w = *w_cursor;
	if (w) {
		w->inuse_cnt--;
		*w_cursor = NULL;
	}
}

void close_pack_index(struct packed_git *p)
{
	if (p->index_data) {
		munmap((void *)p->index_data, p->index_size);
		p->index_data = NULL;
	}
}

/*
 * This is used by git-repack in case a newly created pack happens to
 * contain the same set of objects as an existing one.  In that case
 * the resulting file might be different even if its name would be the
 * same.  It is best to close any reference to the old pack before it is
 * replaced on disk.  Of course no index pointers nor windows for given pack
 * must subsist at this point.  If ever objects from this pack are requested
 * again, the new version of the pack will be reinitialized through
 * reprepare_packed_git().
 */
void free_pack_by_name(const char *pack_name)
{
	struct packed_git *p, **pp = &packed_git;

	while (*pp) {
		p = *pp;
		if (strcmp(pack_name, p->pack_name) == 0) {
			clear_delta_base_cache();
			close_pack_windows(p);
			if (p->pack_fd != -1) {
				close(p->pack_fd);
				pack_open_fds--;
			}
			close_pack_index(p);
			free(p->bad_object_sha1);
			*pp = p->next;
			free(p);
			return;
		}
		pp = &p->next;
	}
}

/*
 * Do not call this directly as this leaks p->pack_fd on error return;
 * call open_packed_git() instead.
 */
static int open_packed_git_1(struct packed_git *p)
{
	struct stat st;
	struct pack_header hdr;
	unsigned char sha1[20];
	unsigned char *idx_sha1;
	long fd_flag;

	if (!p->index_data && open_pack_index(p))
		return error("packfile %s index unavailable", p->pack_name);

	if (!pack_max_fds) {
		struct rlimit lim;
		unsigned int max_fds;

		if (getrlimit(RLIMIT_NOFILE, &lim))
			die_errno("cannot get RLIMIT_NOFILE");

		max_fds = lim.rlim_cur;

		/* Save 3 for stdin/stdout/stderr, 22 for work */
		if (25 < max_fds)
			pack_max_fds = max_fds - 25;
		else
			pack_max_fds = 1;
	}

	while (pack_max_fds <= pack_open_fds && unuse_one_window(NULL, -1))
		; /* nothing */

	p->pack_fd = git_open_noatime(p->pack_name);
	if (p->pack_fd < 0 || fstat(p->pack_fd, &st))
		return -1;
	pack_open_fds++;

	/* If we created the struct before we had the pack we lack size. */
	if (!p->pack_size) {
		if (!S_ISREG(st.st_mode))
			return error("packfile %s not a regular file", p->pack_name);
		p->pack_size = st.st_size;
	} else if (p->pack_size != st.st_size)
		return error("packfile %s size changed", p->pack_name);

	/* We leave these file descriptors open with sliding mmap;
	 * there is no point keeping them open across exec(), though.
	 */
	fd_flag = fcntl(p->pack_fd, F_GETFD, 0);
	if (fd_flag < 0)
		return error("cannot determine file descriptor flags");
	fd_flag |= FD_CLOEXEC;
	if (fcntl(p->pack_fd, F_SETFD, fd_flag) == -1)
		return error("cannot set FD_CLOEXEC");

	/* Verify we recognize this pack file format. */
	if (read_in_full(p->pack_fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		return error("file %s is far too short to be a packfile", p->pack_name);
	if (hdr.hdr_signature != htonl(PACK_SIGNATURE))
		return error("file %s is not a GIT packfile", p->pack_name);
	if (!pack_version_ok(hdr.hdr_version))
		return error("packfile %s is version %"PRIu32" and not"
			" supported (try upgrading GIT to a newer version)",
			p->pack_name, ntohl(hdr.hdr_version));

	/* Verify the pack matches its index. */
	if (p->num_objects != ntohl(hdr.hdr_entries))
		return error("packfile %s claims to have %"PRIu32" objects"
			     " while index indicates %"PRIu32" objects",
			     p->pack_name, ntohl(hdr.hdr_entries),
			     p->num_objects);
	if (lseek(p->pack_fd, p->pack_size - sizeof(sha1), SEEK_SET) == -1)
		return error("end of packfile %s is unavailable", p->pack_name);
	if (read_in_full(p->pack_fd, sha1, sizeof(sha1)) != sizeof(sha1))
		return error("packfile %s signature is unavailable", p->pack_name);
	idx_sha1 = ((unsigned char *)p->index_data) + p->index_size - 40;
	if (hashcmp(sha1, idx_sha1))
		return error("packfile %s does not match index", p->pack_name);
	return 0;
}

static int open_packed_git(struct packed_git *p)
{
	if (!open_packed_git_1(p))
		return 0;
	if (p->pack_fd != -1) {
		close(p->pack_fd);
		pack_open_fds--;
		p->pack_fd = -1;
	}
	return -1;
}

static int in_window(struct pack_window *win, off_t offset)
{
	/* We must promise at least 20 bytes (one hash) after the
	 * offset is available from this window, otherwise the offset
	 * is not actually in this window and a different window (which
	 * has that one hash excess) must be used.  This is to support
	 * the object header and delta base parsing routines below.
	 */
	off_t win_off = win->offset;
	return win_off <= offset
		&& (offset + 20) <= (win_off + win->len);
}

unsigned char *use_pack(struct packed_git *p,
		struct pack_window **w_cursor,
		off_t offset,
		unsigned long *left)
{
	struct pack_window *win = *w_cursor;

	/* Since packfiles end in a hash of their content and it's
	 * pointless to ask for an offset into the middle of that
	 * hash, and the in_window function above wouldn't match
	 * don't allow an offset too close to the end of the file.
	 */
	if (!p->pack_size && p->pack_fd == -1 && open_packed_git(p))
		die("packfile %s cannot be accessed", p->pack_name);
	if (offset > (p->pack_size - 20))
		die("offset beyond end of packfile (truncated pack?)");

	if (!win || !in_window(win, offset)) {
		if (win)
			win->inuse_cnt--;
		for (win = p->windows; win; win = win->next) {
			if (in_window(win, offset))
				break;
		}
		if (!win) {
			size_t window_align = packed_git_window_size / 2;
			off_t len;

			if (p->pack_fd == -1 && open_packed_git(p))
				die("packfile %s cannot be accessed", p->pack_name);

			win = xcalloc(1, sizeof(*win));
			win->offset = (offset / window_align) * window_align;
			len = p->pack_size - win->offset;
			if (len > packed_git_window_size)
				len = packed_git_window_size;
			win->len = (size_t)len;
			pack_mapped += win->len;
			while (packed_git_limit < pack_mapped
				&& unuse_one_window(p, p->pack_fd))
				; /* nothing */
			win->base = xmmap(NULL, win->len,
				PROT_READ, MAP_PRIVATE,
				p->pack_fd, win->offset);
			if (win->base == MAP_FAILED)
				die("packfile %s cannot be mapped: %s",
					p->pack_name,
					strerror(errno));
			if (!win->offset && win->len == p->pack_size
				&& !p->do_not_close) {
				close(p->pack_fd);
				pack_open_fds--;
				p->pack_fd = -1;
			}
			pack_mmap_calls++;
			pack_open_windows++;
			if (pack_mapped > peak_pack_mapped)
				peak_pack_mapped = pack_mapped;
			if (pack_open_windows > peak_pack_open_windows)
				peak_pack_open_windows = pack_open_windows;
			win->next = p->windows;
			p->windows = win;
		}
	}
	if (win != *w_cursor) {
		win->last_used = pack_used_ctr++;
		win->inuse_cnt++;
		*w_cursor = win;
	}
	offset -= win->offset;
	if (left)
		*left = win->len - xsize_t(offset);
	return win->base + offset;
}

static struct packed_git *alloc_packed_git(int extra)
{
	struct packed_git *p = xmalloc(sizeof(*p) + extra);
	memset(p, 0, sizeof(*p));
	p->pack_fd = -1;
	return p;
}

static void try_to_free_pack_memory(size_t size)
{
	release_pack_memory(size, -1);
}

struct packed_git *add_packed_git(const char *path, int path_len, int local)
{
	static int have_set_try_to_free_routine;
	struct stat st;
	struct packed_git *p = alloc_packed_git(path_len + 2);

	if (!have_set_try_to_free_routine) {
		have_set_try_to_free_routine = 1;
		set_try_to_free_routine(try_to_free_pack_memory);
	}

	/*
	 * Make sure a corresponding .pack file exists and that
	 * the index looks sane.
	 */
	path_len -= strlen(".idx");
	if (path_len < 1) {
		free(p);
		return NULL;
	}
	memcpy(p->pack_name, path, path_len);

	strcpy(p->pack_name + path_len, ".keep");
	if (!access(p->pack_name, F_OK))
		p->pack_keep = 1;

	strcpy(p->pack_name + path_len, ".pack");
	if (stat(p->pack_name, &st) || !S_ISREG(st.st_mode)) {
		free(p);
		return NULL;
	}

	/* ok, it looks sane as far as we can check without
	 * actually mapping the pack file.
	 */
	p->pack_size = st.st_size;
	p->pack_local = local;
	p->mtime = st.st_mtime;
	if (path_len < 40 || get_sha1_hex(path + path_len - 40, p->sha1))
		hashclr(p->sha1);
	return p;
}

struct packed_git *parse_pack_index(unsigned char *sha1, const char *idx_path)
{
	const char *path = sha1_pack_name(sha1);
	struct packed_git *p = alloc_packed_git(strlen(path) + 1);

	strcpy(p->pack_name, path);
	hashcpy(p->sha1, sha1);
	if (check_packed_git_idx(idx_path, p)) {
		free(p);
		return NULL;
	}

	return p;
}

void install_packed_git(struct packed_git *pack)
{
	if (pack->pack_fd != -1)
		pack_open_fds++;

	pack->next = packed_git;
	packed_git = pack;
}

static void prepare_packed_git_one(char *objdir, int local)
{
	/* Ensure that this buffer is large enough so that we can
	   append "/pack/" without clobbering the stack even if
	   strlen(objdir) were PATH_MAX.  */
	char path[PATH_MAX + 1 + 4 + 1 + 1];
	int len;
	DIR *dir;
	struct dirent *de;

	sprintf(path, "%s/pack", objdir);
	len = strlen(path);
	dir = opendir(path);
	if (!dir) {
		if (errno != ENOENT)
			error("unable to open object pack directory: %s: %s",
			      path, strerror(errno));
		return;
	}
	path[len++] = '/';
	while ((de = readdir(dir)) != NULL) {
		int namelen = strlen(de->d_name);
		struct packed_git *p;

		if (!has_extension(de->d_name, ".idx"))
			continue;

		if (len + namelen + 1 > sizeof(path))
			continue;

		/* Don't reopen a pack we already have. */
		strcpy(path + len, de->d_name);
		for (p = packed_git; p; p = p->next) {
			if (!memcmp(path, p->pack_name, len + namelen - 4))
				break;
		}
		if (p)
			continue;
		/* See if it really is a valid .idx file with corresponding
		 * .pack file that we can map.
		 */
		p = add_packed_git(path, len + namelen, local);
		if (!p)
			continue;
		install_packed_git(p);
	}
	closedir(dir);
}

static int sort_pack(const void *a_, const void *b_)
{
	struct packed_git *a = *((struct packed_git **)a_);
	struct packed_git *b = *((struct packed_git **)b_);
	int st;

	/*
	 * Local packs tend to contain objects specific to our
	 * variant of the project than remote ones.  In addition,
	 * remote ones could be on a network mounted filesystem.
	 * Favor local ones for these reasons.
	 */
	st = a->pack_local - b->pack_local;
	if (st)
		return -st;

	/*
	 * Younger packs tend to contain more recent objects,
	 * and more recent objects tend to get accessed more
	 * often.
	 */
	if (a->mtime < b->mtime)
		return 1;
	else if (a->mtime == b->mtime)
		return 0;
	return -1;
}

static void rearrange_packed_git(void)
{
	struct packed_git **ary, *p;
	int i, n;

	for (n = 0, p = packed_git; p; p = p->next)
		n++;
	if (n < 2)
		return;

	/* prepare an array of packed_git for easier sorting */
	ary = xcalloc(n, sizeof(struct packed_git *));
	for (n = 0, p = packed_git; p; p = p->next)
		ary[n++] = p;

	qsort(ary, n, sizeof(struct packed_git *), sort_pack);

	/* link them back again */
	for (i = 0; i < n - 1; i++)
		ary[i]->next = ary[i + 1];
	ary[n - 1]->next = NULL;
	packed_git = ary[0];

	free(ary);
}

static int prepare_packed_git_run_once = 0;
void prepare_packed_git(void)
{
	struct alternate_object_database *alt;

	if (prepare_packed_git_run_once)
		return;
	prepare_packed_git_one(get_object_directory(), 1);
	prepare_alt_odb();
	for (alt = alt_odb_list; alt; alt = alt->next) {
		alt->name[-1] = 0;
		prepare_packed_git_one(alt->base, 0);
		alt->name[-1] = '/';
	}
	rearrange_packed_git();
	prepare_packed_git_run_once = 1;
}

void reprepare_packed_git(void)
{
	discard_revindex();
	prepare_packed_git_run_once = 0;
	prepare_packed_git();
}

static void mark_bad_packed_object(struct packed_git *p,
				   const unsigned char *sha1)
{
	unsigned i;
	for (i = 0; i < p->num_bad_objects; i++)
		if (!hashcmp(sha1, p->bad_object_sha1 + 20 * i))
			return;
	p->bad_object_sha1 = xrealloc(p->bad_object_sha1, 20 * (p->num_bad_objects + 1));
	hashcpy(p->bad_object_sha1 + 20 * p->num_bad_objects, sha1);
	p->num_bad_objects++;
}

static const struct packed_git *has_packed_and_bad(const unsigned char *sha1)
{
	struct packed_git *p;
	unsigned i;

	for (p = packed_git; p; p = p->next)
		for (i = 0; i < p->num_bad_objects; i++)
			if (!hashcmp(sha1, p->bad_object_sha1 + 20 * i))
				return p;
	return NULL;
}

int check_sha1_signature(const unsigned char *sha1, void *map, unsigned long size, const char *type)
{
	unsigned char real_sha1[20];
	hash_sha1_file(map, size, type, real_sha1);
	return hashcmp(sha1, real_sha1) ? -1 : 0;
}

static int git_open_noatime(const char *name)
{
	static int sha1_file_open_flag = O_NOATIME;

	for (;;) {
		int fd = open(name, O_RDONLY | sha1_file_open_flag);
		if (fd >= 0)
			return fd;

		/* Might the failure be due to O_NOATIME? */
		if (errno != ENOENT && sha1_file_open_flag) {
			sha1_file_open_flag = 0;
			continue;
		}

		return -1;
	}
}

static int open_sha1_file(const unsigned char *sha1)
{
	int fd;
	char *name = sha1_file_name(sha1);
	struct alternate_object_database *alt;

	fd = git_open_noatime(name);
	if (fd >= 0)
		return fd;

	prepare_alt_odb();
	errno = ENOENT;
	for (alt = alt_odb_list; alt; alt = alt->next) {
		name = alt->name;
		fill_sha1_path(name, sha1);
		fd = git_open_noatime(alt->base);
		if (fd >= 0)
			return fd;
	}
	return -1;
}

void *map_sha1_file(const unsigned char *sha1, unsigned long *size)
{
	void *map;
	int fd;

	fd = open_sha1_file(sha1);
	map = NULL;
	if (fd >= 0) {
		struct stat st;

		if (!fstat(fd, &st)) {
			*size = xsize_t(st.st_size);
			map = xmmap(NULL, *size, PROT_READ, MAP_PRIVATE, fd, 0);
		}
		close(fd);
	}
	return map;
}

/*
 * There used to be a second loose object header format which
 * was meant to mimic the in-pack format, allowing for direct
 * copy of the object data.  This format turned up not to be
 * really worth it and we no longer write loose objects in that
 * format.
 */
static int experimental_loose_object(unsigned char *map)
{
	unsigned int word;

	/*
	 * We must determine if the buffer contains the standard
	 * zlib-deflated stream or the experimental format based
	 * on the in-pack object format. Compare the header byte
	 * for each format:
	 *
	 * RFC1950 zlib w/ deflate : 0www1000 : 0 <= www <= 7
	 * Experimental pack-based : Stttssss : ttt = 1,2,3,4
	 *
	 * If bit 7 is clear and bits 0-3 equal 8, the buffer MUST be
	 * in standard loose-object format, UNLESS it is a Git-pack
	 * format object *exactly* 8 bytes in size when inflated.
	 *
	 * However, RFC1950 also specifies that the 1st 16-bit word
	 * must be divisible by 31 - this checksum tells us our buffer
	 * is in the standard format, giving a false positive only if
	 * the 1st word of the Git-pack format object happens to be
	 * divisible by 31, ie:
	 *      ((byte0 * 256) + byte1) % 31 = 0
	 *   =>        0ttt10000www1000 % 31 = 0
	 *
	 * As it happens, this case can only arise for www=3 & ttt=1
	 * - ie, a Commit object, which would have to be 8 bytes in
	 * size. As no Commit can be that small, we find that the
	 * combination of these two criteria (bitmask & checksum)
	 * can always correctly determine the buffer format.
	 */
	word = (map[0] << 8) + map[1];
	if ((map[0] & 0x8F) == 0x08 && !(word % 31))
		return 0;
	else
		return 1;
}

unsigned long unpack_object_header_buffer(const unsigned char *buf,
		unsigned long len, enum object_type *type, unsigned long *sizep)
{
	unsigned shift;
	unsigned long size, c;
	unsigned long used = 0;

	c = buf[used++];
	*type = (c >> 4) & 7;
	size = c & 15;
	shift = 4;
	while (c & 0x80) {
		if (len <= used || bitsizeof(long) <= shift) {
			error("bad object header");
			return 0;
		}
		c = buf[used++];
		size += (c & 0x7f) << shift;
		shift += 7;
	}
	*sizep = size;
	return used;
}

int unpack_sha1_header(git_zstream *stream, unsigned char *map, unsigned long mapsize, void *buffer, unsigned long bufsiz)
{
	unsigned long size, used;
	static const char valid_loose_object_type[8] = {
		0, /* OBJ_EXT */
		1, 1, 1, 1, /* "commit", "tree", "blob", "tag" */
		0, /* "delta" and others are invalid in a loose object */
	};
	enum object_type type;

	/* Get the data stream */
	memset(stream, 0, sizeof(*stream));
	stream->next_in = map;
	stream->avail_in = mapsize;
	stream->next_out = buffer;
	stream->avail_out = bufsiz;

	if (experimental_loose_object(map)) {
		/*
		 * The old experimental format we no longer produce;
		 * we can still read it.
		 */
		used = unpack_object_header_buffer(map, mapsize, &type, &size);
		if (!used || !valid_loose_object_type[type])
			return -1;
		map += used;
		mapsize -= used;

		/* Set up the stream for the rest.. */
		stream->next_in = map;
		stream->avail_in = mapsize;
		git_inflate_init(stream);

		/* And generate the fake traditional header */
		stream->total_out = 1 + snprintf(buffer, bufsiz, "%s %lu",
						 typename(type), size);
		return 0;
	}
	git_inflate_init(stream);
	return git_inflate(stream, 0);
}

static void *unpack_sha1_rest(git_zstream *stream, void *buffer, unsigned long size, const unsigned char *sha1)
{
	int bytes = strlen(buffer) + 1;
	unsigned char *buf = xmallocz(size);
	unsigned long n;
	int status = Z_OK;

	n = stream->total_out - bytes;
	if (n > size)
		n = size;
	memcpy(buf, (char *) buffer + bytes, n);
	bytes = n;
	if (bytes <= size) {
		/*
		 * The above condition must be (bytes <= size), not
		 * (bytes < size).  In other words, even though we
		 * expect no more output and set avail_out to zero,
		 * the input zlib stream may have bytes that express
		 * "this concludes the stream", and we *do* want to
		 * eat that input.
		 *
		 * Otherwise we would not be able to test that we
		 * consumed all the input to reach the expected size;
		 * we also want to check that zlib tells us that all
		 * went well with status == Z_STREAM_END at the end.
		 */
		stream->next_out = buf + bytes;
		stream->avail_out = size - bytes;
		while (status == Z_OK)
			status = git_inflate(stream, Z_FINISH);
	}
	if (status == Z_STREAM_END && !stream->avail_in) {
		git_inflate_end(stream);
		return buf;
	}

	if (status < 0)
		error("corrupt loose object '%s'", sha1_to_hex(sha1));
	else if (stream->avail_in)
		error("garbage at end of loose object '%s'",
		      sha1_to_hex(sha1));
	free(buf);
	return NULL;
}

/*
 * We used to just use "sscanf()", but that's actually way
 * too permissive for what we want to check. So do an anal
 * object header parse by hand.
 */
int parse_sha1_header(const char *hdr, unsigned long *sizep)
{
	char type[10];
	int i;
	unsigned long size;

	/*
	 * The type can be at most ten bytes (including the
	 * terminating '\0' that we add), and is followed by
	 * a space.
	 */
	i = 0;
	for (;;) {
		char c = *hdr++;
		if (c == ' ')
			break;
		type[i++] = c;
		if (i >= sizeof(type))
			return -1;
	}
	type[i] = 0;

	/*
	 * The length must follow immediately, and be in canonical
	 * decimal format (ie "010" is not valid).
	 */
	size = *hdr++ - '0';
	if (size > 9)
		return -1;
	if (size) {
		for (;;) {
			unsigned long c = *hdr - '0';
			if (c > 9)
				break;
			hdr++;
			size = size * 10 + c;
		}
	}
	*sizep = size;

	/*
	 * The length must be followed by a zero byte
	 */
	return *hdr ? -1 : type_from_string(type);
}

static void *unpack_sha1_file(void *map, unsigned long mapsize, enum object_type *type, unsigned long *size, const unsigned char *sha1)
{
	int ret;
	git_zstream stream;
	char hdr[8192];

	ret = unpack_sha1_header(&stream, map, mapsize, hdr, sizeof(hdr));
	if (ret < Z_OK || (*type = parse_sha1_header(hdr, size)) < 0)
		return NULL;

	return unpack_sha1_rest(&stream, hdr, *size, sha1);
}

unsigned long get_size_from_delta(struct packed_git *p,
				  struct pack_window **w_curs,
			          off_t curpos)
{
	const unsigned char *data;
	unsigned char delta_head[20], *in;
	git_zstream stream;
	int st;

	memset(&stream, 0, sizeof(stream));
	stream.next_out = delta_head;
	stream.avail_out = sizeof(delta_head);

	git_inflate_init(&stream);
	do {
		in = use_pack(p, w_curs, curpos, &stream.avail_in);
		stream.next_in = in;
		st = git_inflate(&stream, Z_FINISH);
		curpos += stream.next_in - in;
	} while ((st == Z_OK || st == Z_BUF_ERROR) &&
		 stream.total_out < sizeof(delta_head));
	git_inflate_end(&stream);
	if ((st != Z_STREAM_END) && stream.total_out != sizeof(delta_head)) {
		error("delta data unpack-initial failed");
		return 0;
	}

	/* Examine the initial part of the delta to figure out
	 * the result size.
	 */
	data = delta_head;

	/* ignore base size */
	get_delta_hdr_size(&data, delta_head+sizeof(delta_head));

	/* Read the result size */
	return get_delta_hdr_size(&data, delta_head+sizeof(delta_head));
}

static off_t get_delta_base(struct packed_git *p,
				    struct pack_window **w_curs,
				    off_t *curpos,
				    enum object_type type,
				    off_t delta_obj_offset)
{
	unsigned char *base_info = use_pack(p, w_curs, *curpos, NULL);
	off_t base_offset;

	/* use_pack() assured us we have [base_info, base_info + 20)
	 * as a range that we can look at without walking off the
	 * end of the mapped window.  Its actually the hash size
	 * that is assured.  An OFS_DELTA longer than the hash size
	 * is stupid, as then a REF_DELTA would be smaller to store.
	 */
	if (type == OBJ_OFS_DELTA) {
		unsigned used = 0;
		unsigned char c = base_info[used++];
		base_offset = c & 127;
		while (c & 128) {
			base_offset += 1;
			if (!base_offset || MSB(base_offset, 7))
				return 0;  /* overflow */
			c = base_info[used++];
			base_offset = (base_offset << 7) + (c & 127);
		}
		base_offset = delta_obj_offset - base_offset;
		if (base_offset <= 0 || base_offset >= delta_obj_offset)
			return 0;  /* out of bound */
		*curpos += used;
	} else if (type == OBJ_REF_DELTA) {
		/* The base entry _must_ be in the same pack */
		base_offset = find_pack_entry_one(base_info, p);
		*curpos += 20;
	} else
		die("I am totally screwed");
	return base_offset;
}

/* forward declaration for a mutually recursive function */
static int packed_object_info(struct packed_git *p, off_t offset,
			      unsigned long *sizep, int *rtype);

static int packed_delta_info(struct packed_git *p,
			     struct pack_window **w_curs,
			     off_t curpos,
			     enum object_type type,
			     off_t obj_offset,
			     unsigned long *sizep)
{
	off_t base_offset;

	base_offset = get_delta_base(p, w_curs, &curpos, type, obj_offset);
	if (!base_offset)
		return OBJ_BAD;
	type = packed_object_info(p, base_offset, NULL, NULL);
	if (type <= OBJ_NONE) {
		struct revindex_entry *revidx;
		const unsigned char *base_sha1;
		revidx = find_pack_revindex(p, base_offset);
		if (!revidx)
			return OBJ_BAD;
		base_sha1 = nth_packed_object_sha1(p, revidx->nr);
		mark_bad_packed_object(p, base_sha1);
		type = sha1_object_info(base_sha1, NULL);
		if (type <= OBJ_NONE)
			return OBJ_BAD;
	}

	/* We choose to only get the type of the base object and
	 * ignore potentially corrupt pack file that expects the delta
	 * based on a base with a wrong size.  This saves tons of
	 * inflate() calls.
	 */
	if (sizep) {
		*sizep = get_size_from_delta(p, w_curs, curpos);
		if (*sizep == 0)
			type = OBJ_BAD;
	}

	return type;
}

int unpack_object_header(struct packed_git *p,
			 struct pack_window **w_curs,
			 off_t *curpos,
			 unsigned long *sizep)
{
	unsigned char *base;
	unsigned long left;
	unsigned long used;
	enum object_type type;

	/* use_pack() assures us we have [base, base + 20) available
	 * as a range that we can look at.  (Its actually the hash
	 * size that is assured.)  With our object header encoding
	 * the maximum deflated object size is 2^137, which is just
	 * insane, so we know won't exceed what we have been given.
	 */
	base = use_pack(p, w_curs, *curpos, &left);
	used = unpack_object_header_buffer(base, left, &type, sizep);
	if (!used) {
		type = OBJ_BAD;
	} else
		*curpos += used;

	return type;
}

static int packed_object_info(struct packed_git *p, off_t obj_offset,
			      unsigned long *sizep, int *rtype)
{
	struct pack_window *w_curs = NULL;
	unsigned long size;
	off_t curpos = obj_offset;
	enum object_type type;

	type = unpack_object_header(p, &w_curs, &curpos, &size);
	if (rtype)
		*rtype = type; /* representation type */

	switch (type) {
	case OBJ_OFS_DELTA:
	case OBJ_REF_DELTA:
		type = packed_delta_info(p, &w_curs, curpos,
					 type, obj_offset, sizep);
		break;
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		if (sizep)
			*sizep = size;
		break;
	default:
		error("unknown object type %i at offset %"PRIuMAX" in %s",
		      type, (uintmax_t)obj_offset, p->pack_name);
		type = OBJ_BAD;
	}
	unuse_pack(&w_curs);
	return type;
}

static void *unpack_compressed_entry(struct packed_git *p,
				    struct pack_window **w_curs,
				    off_t curpos,
				    unsigned long size)
{
	int st;
	git_zstream stream;
	unsigned char *buffer, *in;

	buffer = xmallocz(size);
	memset(&stream, 0, sizeof(stream));
	stream.next_out = buffer;
	stream.avail_out = size + 1;

	git_inflate_init(&stream);
	do {
		in = use_pack(p, w_curs, curpos, &stream.avail_in);
		stream.next_in = in;
		st = git_inflate(&stream, Z_FINISH);
		if (!stream.avail_out)
			break; /* the payload is larger than it should be */
		curpos += stream.next_in - in;
	} while (st == Z_OK || st == Z_BUF_ERROR);
	git_inflate_end(&stream);
	if ((st != Z_STREAM_END) || stream.total_out != size) {
		free(buffer);
		return NULL;
	}

	return buffer;
}

#define MAX_DELTA_CACHE (256)

static size_t delta_base_cached;

static struct delta_base_cache_lru_list {
	struct delta_base_cache_lru_list *prev;
	struct delta_base_cache_lru_list *next;
} delta_base_cache_lru = { &delta_base_cache_lru, &delta_base_cache_lru };

static struct delta_base_cache_entry {
	struct delta_base_cache_lru_list lru;
	void *data;
	struct packed_git *p;
	off_t base_offset;
	unsigned long size;
	enum object_type type;
} delta_base_cache[MAX_DELTA_CACHE];

static unsigned long pack_entry_hash(struct packed_git *p, off_t base_offset)
{
	unsigned long hash;

	hash = (unsigned long)p + (unsigned long)base_offset;
	hash += (hash >> 8) + (hash >> 16);
	return hash % MAX_DELTA_CACHE;
}

static int in_delta_base_cache(struct packed_git *p, off_t base_offset)
{
	unsigned long hash = pack_entry_hash(p, base_offset);
	struct delta_base_cache_entry *ent = delta_base_cache + hash;
	return (ent->data && ent->p == p && ent->base_offset == base_offset);
}

static void *cache_or_unpack_entry(struct packed_git *p, off_t base_offset,
	unsigned long *base_size, enum object_type *type, int keep_cache)
{
	void *ret;
	unsigned long hash = pack_entry_hash(p, base_offset);
	struct delta_base_cache_entry *ent = delta_base_cache + hash;

	ret = ent->data;
	if (!ret || ent->p != p || ent->base_offset != base_offset)
		return unpack_entry(p, base_offset, type, base_size);

	if (!keep_cache) {
		ent->data = NULL;
		ent->lru.next->prev = ent->lru.prev;
		ent->lru.prev->next = ent->lru.next;
		delta_base_cached -= ent->size;
	} else {
		ret = xmemdupz(ent->data, ent->size);
	}
	*type = ent->type;
	*base_size = ent->size;
	return ret;
}

static inline void release_delta_base_cache(struct delta_base_cache_entry *ent)
{
	if (ent->data) {
		free(ent->data);
		ent->data = NULL;
		ent->lru.next->prev = ent->lru.prev;
		ent->lru.prev->next = ent->lru.next;
		delta_base_cached -= ent->size;
	}
}

void clear_delta_base_cache(void)
{
	unsigned long p;
	for (p = 0; p < MAX_DELTA_CACHE; p++)
		release_delta_base_cache(&delta_base_cache[p]);
}

static void add_delta_base_cache(struct packed_git *p, off_t base_offset,
	void *base, unsigned long base_size, enum object_type type)
{
	unsigned long hash = pack_entry_hash(p, base_offset);
	struct delta_base_cache_entry *ent = delta_base_cache + hash;
	struct delta_base_cache_lru_list *lru;

	release_delta_base_cache(ent);
	delta_base_cached += base_size;

	for (lru = delta_base_cache_lru.next;
	     delta_base_cached > delta_base_cache_limit
	     && lru != &delta_base_cache_lru;
	     lru = lru->next) {
		struct delta_base_cache_entry *f = (void *)lru;
		if (f->type == OBJ_BLOB)
			release_delta_base_cache(f);
	}
	for (lru = delta_base_cache_lru.next;
	     delta_base_cached > delta_base_cache_limit
	     && lru != &delta_base_cache_lru;
	     lru = lru->next) {
		struct delta_base_cache_entry *f = (void *)lru;
		release_delta_base_cache(f);
	}

	ent->p = p;
	ent->base_offset = base_offset;
	ent->type = type;
	ent->data = base;
	ent->size = base_size;
	ent->lru.next = &delta_base_cache_lru;
	ent->lru.prev = delta_base_cache_lru.prev;
	delta_base_cache_lru.prev->next = &ent->lru;
	delta_base_cache_lru.prev = &ent->lru;
}

static void *read_object(const unsigned char *sha1, enum object_type *type,
			 unsigned long *size);

static void *unpack_delta_entry(struct packed_git *p,
				struct pack_window **w_curs,
				off_t curpos,
				unsigned long delta_size,
				off_t obj_offset,
				enum object_type *type,
				unsigned long *sizep)
{
	void *delta_data, *result, *base;
	unsigned long base_size;
	off_t base_offset;

	base_offset = get_delta_base(p, w_curs, &curpos, *type, obj_offset);
	if (!base_offset) {
		error("failed to validate delta base reference "
		      "at offset %"PRIuMAX" from %s",
		      (uintmax_t)curpos, p->pack_name);
		return NULL;
	}
	unuse_pack(w_curs);
	base = cache_or_unpack_entry(p, base_offset, &base_size, type, 0);
	if (!base) {
		/*
		 * We're probably in deep shit, but let's try to fetch
		 * the required base anyway from another pack or loose.
		 * This is costly but should happen only in the presence
		 * of a corrupted pack, and is better than failing outright.
		 */
		struct revindex_entry *revidx;
		const unsigned char *base_sha1;
		revidx = find_pack_revindex(p, base_offset);
		if (!revidx)
			return NULL;
		base_sha1 = nth_packed_object_sha1(p, revidx->nr);
		error("failed to read delta base object %s"
		      " at offset %"PRIuMAX" from %s",
		      sha1_to_hex(base_sha1), (uintmax_t)base_offset,
		      p->pack_name);
		mark_bad_packed_object(p, base_sha1);
		base = read_object(base_sha1, type, &base_size);
		if (!base)
			return NULL;
	}

	delta_data = unpack_compressed_entry(p, w_curs, curpos, delta_size);
	if (!delta_data) {
		error("failed to unpack compressed delta "
		      "at offset %"PRIuMAX" from %s",
		      (uintmax_t)curpos, p->pack_name);
		free(base);
		return NULL;
	}
	result = patch_delta(base, base_size,
			     delta_data, delta_size,
			     sizep);
	if (!result)
		die("failed to apply delta");
	free(delta_data);
	add_delta_base_cache(p, base_offset, base, base_size, *type);
	return result;
}

static void write_pack_access_log(struct packed_git *p, off_t obj_offset)
{
	static FILE *log_file;

	if (!log_file) {
		log_file = fopen(log_pack_access, "w");
		if (!log_file) {
			error("cannot open pack access log '%s' for writing: %s",
			      log_pack_access, strerror(errno));
			log_pack_access = NULL;
			return;
		}
	}
	fprintf(log_file, "%s %"PRIuMAX"\n",
		p->pack_name, (uintmax_t)obj_offset);
	fflush(log_file);
}

int do_check_packed_object_crc;

void *unpack_entry(struct packed_git *p, off_t obj_offset,
		   enum object_type *type, unsigned long *sizep)
{
	struct pack_window *w_curs = NULL;
	off_t curpos = obj_offset;
	void *data;

	if (log_pack_access)
		write_pack_access_log(p, obj_offset);

	if (do_check_packed_object_crc && p->index_version > 1) {
		struct revindex_entry *revidx = find_pack_revindex(p, obj_offset);
		unsigned long len = revidx[1].offset - obj_offset;
		if (check_pack_crc(p, &w_curs, obj_offset, len, revidx->nr)) {
			const unsigned char *sha1 =
				nth_packed_object_sha1(p, revidx->nr);
			error("bad packed object CRC for %s",
			      sha1_to_hex(sha1));
			mark_bad_packed_object(p, sha1);
			unuse_pack(&w_curs);
			return NULL;
		}
	}

	*type = unpack_object_header(p, &w_curs, &curpos, sizep);
	switch (*type) {
	case OBJ_OFS_DELTA:
	case OBJ_REF_DELTA:
		data = unpack_delta_entry(p, &w_curs, curpos, *sizep,
					  obj_offset, type, sizep);
		break;
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		data = unpack_compressed_entry(p, &w_curs, curpos, *sizep);
		break;
	default:
		data = NULL;
		error("unknown object type %i at offset %"PRIuMAX" in %s",
		      *type, (uintmax_t)obj_offset, p->pack_name);
	}
	unuse_pack(&w_curs);
	return data;
}

const unsigned char *nth_packed_object_sha1(struct packed_git *p,
					    uint32_t n)
{
	const unsigned char *index = p->index_data;
	if (!index) {
		if (open_pack_index(p))
			return NULL;
		index = p->index_data;
	}
	if (n >= p->num_objects)
		return NULL;
	index += 4 * 256;
	if (p->index_version == 1) {
		return index + 24 * n + 4;
	} else {
		index += 8;
		return index + 20 * n;
	}
}

off_t nth_packed_object_offset(const struct packed_git *p, uint32_t n)
{
	const unsigned char *index = p->index_data;
	index += 4 * 256;
	if (p->index_version == 1) {
		return ntohl(*((uint32_t *)(index + 24 * n)));
	} else {
		uint32_t off;
		index += 8 + p->num_objects * (20 + 4);
		off = ntohl(*((uint32_t *)(index + 4 * n)));
		if (!(off & 0x80000000))
			return off;
		index += p->num_objects * 4 + (off & 0x7fffffff) * 8;
		return (((uint64_t)ntohl(*((uint32_t *)(index + 0)))) << 32) |
				   ntohl(*((uint32_t *)(index + 4)));
	}
}

off_t find_pack_entry_one(const unsigned char *sha1,
				  struct packed_git *p)
{
	const uint32_t *level1_ofs = p->index_data;
	const unsigned char *index = p->index_data;
	unsigned hi, lo, stride;
	static int use_lookup = -1;
	static int debug_lookup = -1;

	if (debug_lookup < 0)
		debug_lookup = !!getenv("GIT_DEBUG_LOOKUP");

	if (!index) {
		if (open_pack_index(p))
			return 0;
		level1_ofs = p->index_data;
		index = p->index_data;
	}
	if (p->index_version > 1) {
		level1_ofs += 2;
		index += 8;
	}
	index += 4 * 256;
	hi = ntohl(level1_ofs[*sha1]);
	lo = ((*sha1 == 0x0) ? 0 : ntohl(level1_ofs[*sha1 - 1]));
	if (p->index_version > 1) {
		stride = 20;
	} else {
		stride = 24;
		index += 4;
	}

	if (debug_lookup)
		printf("%02x%02x%02x... lo %u hi %u nr %"PRIu32"\n",
		       sha1[0], sha1[1], sha1[2], lo, hi, p->num_objects);

	if (use_lookup < 0)
		use_lookup = !!getenv("GIT_USE_LOOKUP");
	if (use_lookup) {
		int pos = sha1_entry_pos(index, stride, 0,
					 lo, hi, p->num_objects, sha1);
		if (pos < 0)
			return 0;
		return nth_packed_object_offset(p, pos);
	}

	do {
		unsigned mi = (lo + hi) / 2;
		int cmp = hashcmp(index + mi * stride, sha1);

		if (debug_lookup)
			printf("lo %u hi %u rg %u mi %u\n",
			       lo, hi, hi - lo, mi);
		if (!cmp)
			return nth_packed_object_offset(p, mi);
		if (cmp > 0)
			hi = mi;
		else
			lo = mi+1;
	} while (lo < hi);
	return 0;
}

static int is_pack_valid(struct packed_git *p)
{
	/* An already open pack is known to be valid. */
	if (p->pack_fd != -1)
		return 1;

	/* If the pack has one window completely covering the
	 * file size, the pack is known to be valid even if
	 * the descriptor is not currently open.
	 */
	if (p->windows) {
		struct pack_window *w = p->windows;

		if (!w->offset && w->len == p->pack_size)
			return 1;
	}

	/* Force the pack to open to prove its valid. */
	return !open_packed_git(p);
}

static int find_pack_entry(const unsigned char *sha1, struct pack_entry *e)
{
	static struct packed_git *last_found = (void *)1;
	struct packed_git *p;
	off_t offset;

	prepare_packed_git();
	if (!packed_git)
		return 0;
	p = (last_found == (void *)1) ? packed_git : last_found;

	do {
		if (p->num_bad_objects) {
			unsigned i;
			for (i = 0; i < p->num_bad_objects; i++)
				if (!hashcmp(sha1, p->bad_object_sha1 + 20 * i))
					goto next;
		}

		offset = find_pack_entry_one(sha1, p);
		if (offset) {
			/*
			 * We are about to tell the caller where they can
			 * locate the requested object.  We better make
			 * sure the packfile is still here and can be
			 * accessed before supplying that answer, as
			 * it may have been deleted since the index
			 * was loaded!
			 */
			if (!is_pack_valid(p)) {
				error("packfile %s cannot be accessed", p->pack_name);
				goto next;
			}
			e->offset = offset;
			e->p = p;
			hashcpy(e->sha1, sha1);
			last_found = p;
			return 1;
		}

		next:
		if (p == last_found)
			p = packed_git;
		else
			p = p->next;
		if (p == last_found)
			p = p->next;
	} while (p);
	return 0;
}

struct packed_git *find_sha1_pack(const unsigned char *sha1,
				  struct packed_git *packs)
{
	struct packed_git *p;

	for (p = packs; p; p = p->next) {
		if (find_pack_entry_one(sha1, p))
			return p;
	}
	return NULL;

}

static int sha1_loose_object_info(const unsigned char *sha1, unsigned long *sizep)
{
	int status;
	unsigned long mapsize, size;
	void *map;
	git_zstream stream;
	char hdr[32];

	map = map_sha1_file(sha1, &mapsize);
	if (!map)
		return error("unable to find %s", sha1_to_hex(sha1));
	if (unpack_sha1_header(&stream, map, mapsize, hdr, sizeof(hdr)) < 0)
		status = error("unable to unpack %s header",
			       sha1_to_hex(sha1));
	else if ((status = parse_sha1_header(hdr, &size)) < 0)
		status = error("unable to parse %s header", sha1_to_hex(sha1));
	else if (sizep)
		*sizep = size;
	git_inflate_end(&stream);
	munmap(map, mapsize);
	return status;
}

/* returns enum object_type or negative */
int sha1_object_info_extended(const unsigned char *sha1, struct object_info *oi)
{
	struct cached_object *co;
	struct pack_entry e;
	int status, rtype;

	co = find_cached_object(sha1);
	if (co) {
		if (oi->sizep)
			*(oi->sizep) = co->size;
		oi->whence = OI_CACHED;
		return co->type;
	}

	if (!find_pack_entry(sha1, &e)) {
		/* Most likely it's a loose object. */
		status = sha1_loose_object_info(sha1, oi->sizep);
		if (status >= 0) {
			oi->whence = OI_LOOSE;
			return status;
		}

		/* Not a loose object; someone else may have just packed it. */
		reprepare_packed_git();
		if (!find_pack_entry(sha1, &e))
			return status;
	}

	status = packed_object_info(e.p, e.offset, oi->sizep, &rtype);
	if (status < 0) {
		mark_bad_packed_object(e.p, sha1);
		status = sha1_object_info_extended(sha1, oi);
	} else if (in_delta_base_cache(e.p, e.offset)) {
		oi->whence = OI_DBCACHED;
	} else {
		oi->whence = OI_PACKED;
		oi->u.packed.offset = e.offset;
		oi->u.packed.pack = e.p;
		oi->u.packed.is_delta = (rtype == OBJ_REF_DELTA ||
					 rtype == OBJ_OFS_DELTA);
	}

	return status;
}

int sha1_object_info(const unsigned char *sha1, unsigned long *sizep)
{
	struct object_info oi;

	oi.sizep = sizep;
	return sha1_object_info_extended(sha1, &oi);
}

static void *read_packed_sha1(const unsigned char *sha1,
			      enum object_type *type, unsigned long *size)
{
	struct pack_entry e;
	void *data;

	if (!find_pack_entry(sha1, &e))
		return NULL;
	data = cache_or_unpack_entry(e.p, e.offset, size, type, 1);
	if (!data) {
		/*
		 * We're probably in deep shit, but let's try to fetch
		 * the required object anyway from another pack or loose.
		 * This should happen only in the presence of a corrupted
		 * pack, and is better than failing outright.
		 */
		error("failed to read object %s at offset %"PRIuMAX" from %s",
		      sha1_to_hex(sha1), (uintmax_t)e.offset, e.p->pack_name);
		mark_bad_packed_object(e.p, sha1);
		data = read_object(sha1, type, size);
	}
	return data;
}

int pretend_sha1_file(void *buf, unsigned long len, enum object_type type,
		      unsigned char *sha1)
{
	struct cached_object *co;

	hash_sha1_file(buf, len, typename(type), sha1);
	if (has_sha1_file(sha1) || find_cached_object(sha1))
		return 0;
	if (cached_object_alloc <= cached_object_nr) {
		cached_object_alloc = alloc_nr(cached_object_alloc);
		cached_objects = xrealloc(cached_objects,
					  sizeof(*cached_objects) *
					  cached_object_alloc);
	}
	co = &cached_objects[cached_object_nr++];
	co->size = len;
	co->type = type;
	co->buf = xmalloc(len);
	memcpy(co->buf, buf, len);
	hashcpy(co->sha1, sha1);
	return 0;
}

static void *read_object(const unsigned char *sha1, enum object_type *type,
			 unsigned long *size)
{
	unsigned long mapsize;
	void *map, *buf;
	struct cached_object *co;

	co = find_cached_object(sha1);
	if (co) {
		*type = co->type;
		*size = co->size;
		return xmemdupz(co->buf, co->size);
	}

	buf = read_packed_sha1(sha1, type, size);
	if (buf)
		return buf;
	map = map_sha1_file(sha1, &mapsize);
	if (map) {
		buf = unpack_sha1_file(map, mapsize, type, size, sha1);
		munmap(map, mapsize);
		return buf;
	}
	reprepare_packed_git();
	return read_packed_sha1(sha1, type, size);
}

/*
 * This function dies on corrupt objects; the callers who want to
 * deal with them should arrange to call read_object() and give error
 * messages themselves.
 */
void *read_sha1_file_extended(const unsigned char *sha1,
			      enum object_type *type,
			      unsigned long *size,
			      unsigned flag)
{
	void *data;
	char *path;
	const struct packed_git *p;
	const unsigned char *repl = (flag & READ_SHA1_FILE_REPLACE)
		? lookup_replace_object(sha1) : sha1;

	errno = 0;
	data = read_object(repl, type, size);
	if (data)
		return data;

	if (errno && errno != ENOENT)
		die_errno("failed to read object %s", sha1_to_hex(sha1));

	/* die if we replaced an object with one that does not exist */
	if (repl != sha1)
		die("replacement %s not found for %s",
		    sha1_to_hex(repl), sha1_to_hex(sha1));

	if (has_loose_object(repl)) {
		path = sha1_file_name(sha1);
		die("loose object %s (stored in %s) is corrupt",
		    sha1_to_hex(repl), path);
	}

	if ((p = has_packed_and_bad(repl)) != NULL)
		die("packed object %s (stored in %s) is corrupt",
		    sha1_to_hex(repl), p->pack_name);

	return NULL;
}

void *read_object_with_reference(const unsigned char *sha1,
				 const char *required_type_name,
				 unsigned long *size,
				 unsigned char *actual_sha1_return)
{
	enum object_type type, required_type;
	void *buffer;
	unsigned long isize;
	unsigned char actual_sha1[20];

	required_type = type_from_string(required_type_name);
	hashcpy(actual_sha1, sha1);
	while (1) {
		int ref_length = -1;
		const char *ref_type = NULL;

		buffer = read_sha1_file(actual_sha1, &type, &isize);
		if (!buffer)
			return NULL;
		if (type == required_type) {
			*size = isize;
			if (actual_sha1_return)
				hashcpy(actual_sha1_return, actual_sha1);
			return buffer;
		}
		/* Handle references */
		else if (type == OBJ_COMMIT)
			ref_type = "tree ";
		else if (type == OBJ_TAG)
			ref_type = "object ";
		else {
			free(buffer);
			return NULL;
		}
		ref_length = strlen(ref_type);

		if (ref_length + 40 > isize ||
		    memcmp(buffer, ref_type, ref_length) ||
		    get_sha1_hex((char *) buffer + ref_length, actual_sha1)) {
			free(buffer);
			return NULL;
		}
		free(buffer);
		/* Now we have the ID of the referred-to object in
		 * actual_sha1.  Check again. */
	}
}

static void write_sha1_file_prepare(const void *buf, unsigned long len,
                                    const char *type, unsigned char *sha1,
                                    char *hdr, int *hdrlen)
{
	git_SHA_CTX c;

	/* Generate the header */
	*hdrlen = sprintf(hdr, "%s %lu", type, len)+1;

	/* Sha1.. */
	git_SHA1_Init(&c);
	git_SHA1_Update(&c, hdr, *hdrlen);
	git_SHA1_Update(&c, buf, len);
	git_SHA1_Final(sha1, &c);
}

/*
 * Move the just written object into its final resting place.
 * NEEDSWORK: this should be renamed to finalize_temp_file() as
 * "moving" is only a part of what it does, when no patch between
 * master to pu changes the call sites of this function.
 */
int move_temp_to_file(const char *tmpfile, const char *filename)
{
	int ret = 0;

	if (object_creation_mode == OBJECT_CREATION_USES_RENAMES)
		goto try_rename;
	else if (link(tmpfile, filename))
		ret = errno;

	/*
	 * Coda hack - coda doesn't like cross-directory links,
	 * so we fall back to a rename, which will mean that it
	 * won't be able to check collisions, but that's not a
	 * big deal.
	 *
	 * The same holds for FAT formatted media.
	 *
	 * When this succeeds, we just return.  We have nothing
	 * left to unlink.
	 */
	if (ret && ret != EEXIST) {
	try_rename:
		if (!rename(tmpfile, filename))
			goto out;
		ret = errno;
	}
	unlink_or_warn(tmpfile);
	if (ret) {
		if (ret != EEXIST) {
			return error("unable to write sha1 filename %s: %s\n", filename, strerror(ret));
		}
		/* FIXME!!! Collision check here ? */
	}

out:
	if (adjust_shared_perm(filename))
		return error("unable to set permission to '%s'", filename);
	return 0;
}

static int write_buffer(int fd, const void *buf, size_t len)
{
	if (write_in_full(fd, buf, len) < 0)
		return error("file write error (%s)", strerror(errno));
	return 0;
}

int hash_sha1_file(const void *buf, unsigned long len, const char *type,
                   unsigned char *sha1)
{
	char hdr[32];
	int hdrlen;
	write_sha1_file_prepare(buf, len, type, sha1, hdr, &hdrlen);
	return 0;
}

/* Finalize a file on disk, and close it. */
static void close_sha1_file(int fd)
{
	if (fsync_object_files)
		fsync_or_die(fd, "sha1 file");
	if (close(fd) != 0)
		die_errno("error when closing sha1 file");
}

/* Size of directory component, including the ending '/' */
static inline int directory_size(const char *filename)
{
	const char *s = strrchr(filename, '/');
	if (!s)
		return 0;
	return s - filename + 1;
}

/*
 * This creates a temporary file in the same directory as the final
 * 'filename'
 *
 * We want to avoid cross-directory filename renames, because those
 * can have problems on various filesystems (FAT, NFS, Coda).
 */
static int create_tmpfile(char *buffer, size_t bufsiz, const char *filename)
{
	int fd, dirlen = directory_size(filename);

	if (dirlen + 20 > bufsiz) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(buffer, filename, dirlen);
	strcpy(buffer + dirlen, "tmp_obj_XXXXXX");
	fd = git_mkstemp_mode(buffer, 0444);
	if (fd < 0 && dirlen && errno == ENOENT) {
		/* Make sure the directory exists */
		memcpy(buffer, filename, dirlen);
		buffer[dirlen-1] = 0;
		if (mkdir(buffer, 0777) || adjust_shared_perm(buffer))
			return -1;

		/* Try again */
		strcpy(buffer + dirlen - 1, "/tmp_obj_XXXXXX");
		fd = git_mkstemp_mode(buffer, 0444);
	}
	return fd;
}

static int write_loose_object(const unsigned char *sha1, char *hdr, int hdrlen,
			      const void *buf, unsigned long len, time_t mtime)
{
	int fd, ret;
	unsigned char compressed[4096];
	git_zstream stream;
	git_SHA_CTX c;
	unsigned char parano_sha1[20];
	char *filename;
	static char tmpfile[PATH_MAX];

	filename = sha1_file_name(sha1);
	fd = create_tmpfile(tmpfile, sizeof(tmpfile), filename);
	if (fd < 0) {
		if (errno == EACCES)
			return error("insufficient permission for adding an object to repository database %s\n", get_object_directory());
		else
			return error("unable to create temporary sha1 filename %s: %s\n", tmpfile, strerror(errno));
	}

	/* Set it up */
	memset(&stream, 0, sizeof(stream));
	git_deflate_init(&stream, zlib_compression_level);
	stream.next_out = compressed;
	stream.avail_out = sizeof(compressed);
	git_SHA1_Init(&c);

	/* First header.. */
	stream.next_in = (unsigned char *)hdr;
	stream.avail_in = hdrlen;
	while (git_deflate(&stream, 0) == Z_OK)
		; /* nothing */
	git_SHA1_Update(&c, hdr, hdrlen);

	/* Then the data itself.. */
	stream.next_in = (void *)buf;
	stream.avail_in = len;
	do {
		unsigned char *in0 = stream.next_in;
		ret = git_deflate(&stream, Z_FINISH);
		git_SHA1_Update(&c, in0, stream.next_in - in0);
		if (write_buffer(fd, compressed, stream.next_out - compressed) < 0)
			die("unable to write sha1 file");
		stream.next_out = compressed;
		stream.avail_out = sizeof(compressed);
	} while (ret == Z_OK);

	if (ret != Z_STREAM_END)
		die("unable to deflate new object %s (%d)", sha1_to_hex(sha1), ret);
	ret = git_deflate_end_gently(&stream);
	if (ret != Z_OK)
		die("deflateEnd on object %s failed (%d)", sha1_to_hex(sha1), ret);
	git_SHA1_Final(parano_sha1, &c);
	if (hashcmp(sha1, parano_sha1) != 0)
		die("confused by unstable object source data for %s", sha1_to_hex(sha1));

	close_sha1_file(fd);

	if (mtime) {
		struct utimbuf utb;
		utb.actime = mtime;
		utb.modtime = mtime;
		if (utime(tmpfile, &utb) < 0)
			warning("failed utime() on %s: %s",
				tmpfile, strerror(errno));
	}

	return move_temp_to_file(tmpfile, filename);
}

int write_sha1_file(const void *buf, unsigned long len, const char *type, unsigned char *returnsha1)
{
	unsigned char sha1[20];
	char hdr[32];
	int hdrlen;

	/* Normally if we have it in the pack then we do not bother writing
	 * it out into .git/objects/??/?{38} file.
	 */
	write_sha1_file_prepare(buf, len, type, sha1, hdr, &hdrlen);
	if (returnsha1)
		hashcpy(returnsha1, sha1);
	if (has_sha1_file(sha1))
		return 0;
	return write_loose_object(sha1, hdr, hdrlen, buf, len, 0);
}

int force_object_loose(const unsigned char *sha1, time_t mtime)
{
	void *buf;
	unsigned long len;
	enum object_type type;
	char hdr[32];
	int hdrlen;
	int ret;

	if (has_loose_object(sha1))
		return 0;
	buf = read_packed_sha1(sha1, &type, &len);
	if (!buf)
		return error("cannot read sha1_file for %s", sha1_to_hex(sha1));
	hdrlen = sprintf(hdr, "%s %lu", typename(type), len) + 1;
	ret = write_loose_object(sha1, hdr, hdrlen, buf, len, mtime);
	free(buf);

	return ret;
}

int has_pack_index(const unsigned char *sha1)
{
	struct stat st;
	if (stat(sha1_pack_index_name(sha1), &st))
		return 0;
	return 1;
}

int has_sha1_pack(const unsigned char *sha1)
{
	struct pack_entry e;
	return find_pack_entry(sha1, &e);
}

int has_sha1_file(const unsigned char *sha1)
{
	struct pack_entry e;

	if (find_pack_entry(sha1, &e))
		return 1;
	return has_loose_object(sha1);
}

static void check_tree(const void *buf, size_t size)
{
	struct tree_desc desc;
	struct name_entry entry;

	init_tree_desc(&desc, buf, size);
	while (tree_entry(&desc, &entry))
		/* do nothing
		 * tree_entry() will die() on malformed entries */
		;
}

static void check_commit(const void *buf, size_t size)
{
	struct commit c;
	memset(&c, 0, sizeof(c));
	if (parse_commit_buffer(&c, buf, size))
		die("corrupt commit");
}

static void check_tag(const void *buf, size_t size)
{
	struct tag t;
	memset(&t, 0, sizeof(t));
	if (parse_tag_buffer(&t, buf, size))
		die("corrupt tag");
}

static int index_mem(unsigned char *sha1, void *buf, size_t size,
		     enum object_type type,
		     const char *path, unsigned flags)
{
	int ret, re_allocated = 0;
	int write_object = flags & HASH_WRITE_OBJECT;

	if (!type)
		type = OBJ_BLOB;

	/*
	 * Convert blobs to git internal format
	 */
	if ((type == OBJ_BLOB) && path) {
		struct strbuf nbuf = STRBUF_INIT;
		if (convert_to_git(path, buf, size, &nbuf,
		                   write_object ? safe_crlf : 0)) {
			buf = strbuf_detach(&nbuf, &size);
			re_allocated = 1;
		}
	}
	if (flags & HASH_FORMAT_CHECK) {
		if (type == OBJ_TREE)
			check_tree(buf, size);
		if (type == OBJ_COMMIT)
			check_commit(buf, size);
		if (type == OBJ_TAG)
			check_tag(buf, size);
	}

	if (write_object)
		ret = write_sha1_file(buf, size, typename(type), sha1);
	else
		ret = hash_sha1_file(buf, size, typename(type), sha1);
	if (re_allocated)
		free(buf);
	return ret;
}

static int index_pipe(unsigned char *sha1, int fd, enum object_type type,
		      const char *path, unsigned flags)
{
	struct strbuf sbuf = STRBUF_INIT;
	int ret;

	if (strbuf_read(&sbuf, fd, 4096) >= 0)
		ret = index_mem(sha1, sbuf.buf, sbuf.len, type,	path, flags);
	else
		ret = -1;
	strbuf_release(&sbuf);
	return ret;
}

#define SMALL_FILE_SIZE (32*1024)

static int index_core(unsigned char *sha1, int fd, size_t size,
		      enum object_type type, const char *path,
		      unsigned flags)
{
	int ret;

	if (!size) {
		ret = index_mem(sha1, NULL, size, type, path, flags);
	} else if (size <= SMALL_FILE_SIZE) {
		char *buf = xmalloc(size);
		if (size == read_in_full(fd, buf, size))
			ret = index_mem(sha1, buf, size, type, path, flags);
		else
			ret = error("short read %s", strerror(errno));
		free(buf);
	} else {
		void *buf = xmmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
		ret = index_mem(sha1, buf, size, type, path, flags);
		munmap(buf, size);
	}
	return ret;
}

/*
 * This creates one packfile per large blob, because the caller
 * immediately wants the result sha1, and fast-import can report the
 * object name via marks mechanism only by closing the created
 * packfile.
 *
 * This also bypasses the usual "convert-to-git" dance, and that is on
 * purpose. We could write a streaming version of the converting
 * functions and insert that before feeding the data to fast-import
 * (or equivalent in-core API described above), but the primary
 * motivation for trying to stream from the working tree file and to
 * avoid mmaping it in core is to deal with large binary blobs, and
 * by definition they do _not_ want to get any conversion.
 */
static int index_stream(unsigned char *sha1, int fd, size_t size,
			enum object_type type, const char *path,
			unsigned flags)
{
	struct child_process fast_import;
	char export_marks[512];
	const char *argv[] = { "fast-import", "--quiet", export_marks, NULL };
	char tmpfile[512];
	char fast_import_cmd[512];
	char buf[512];
	int len, tmpfd;

	strcpy(tmpfile, git_path("hashstream_XXXXXX"));
	tmpfd = git_mkstemp_mode(tmpfile, 0600);
	if (tmpfd < 0)
		die_errno("cannot create tempfile: %s", tmpfile);
	if (close(tmpfd))
		die_errno("cannot close tempfile: %s", tmpfile);
	sprintf(export_marks, "--export-marks=%s", tmpfile);

	memset(&fast_import, 0, sizeof(fast_import));
	fast_import.in = -1;
	fast_import.argv = argv;
	fast_import.git_cmd = 1;
	if (start_command(&fast_import))
		die_errno("index-stream: git fast-import failed");

	len = sprintf(fast_import_cmd, "blob\nmark :1\ndata %lu\n",
		      (unsigned long) size);
	write_or_whine(fast_import.in, fast_import_cmd, len,
		       "index-stream: feeding fast-import");
	while (size) {
		char buf[10240];
		size_t sz = size < sizeof(buf) ? size : sizeof(buf);
		ssize_t actual;

		actual = read_in_full(fd, buf, sz);
		if (actual < 0)
			die_errno("index-stream: reading input");
		if (write_in_full(fast_import.in, buf, actual) != actual)
			die_errno("index-stream: feeding fast-import");
		size -= actual;
	}
	if (close(fast_import.in))
		die_errno("index-stream: closing fast-import");
	if (finish_command(&fast_import))
		die_errno("index-stream: finishing fast-import");

	tmpfd = open(tmpfile, O_RDONLY);
	if (tmpfd < 0)
		die_errno("index-stream: cannot open fast-import mark");
	len = read(tmpfd, buf, sizeof(buf));
	if (len < 0)
		die_errno("index-stream: reading fast-import mark");
	if (close(tmpfd) < 0)
		die_errno("index-stream: closing fast-import mark");
	if (unlink(tmpfile))
		die_errno("index-stream: unlinking fast-import mark");
	if (len != 44 ||
	    memcmp(":1 ", buf, 3) ||
	    get_sha1_hex(buf + 3, sha1))
		die_errno("index-stream: unexpected fast-import mark: <%s>", buf);
	return 0;
}

int index_fd(unsigned char *sha1, int fd, struct stat *st,
	     enum object_type type, const char *path, unsigned flags)
{
	int ret;
	size_t size = xsize_t(st->st_size);

	if (!S_ISREG(st->st_mode))
		ret = index_pipe(sha1, fd, type, path, flags);
	else if (size <= big_file_threshold || type != OBJ_BLOB)
		ret = index_core(sha1, fd, size, type, path, flags);
	else
		ret = index_stream(sha1, fd, size, type, path, flags);
	close(fd);
	return ret;
}

int index_path(unsigned char *sha1, const char *path, struct stat *st, unsigned flags)
{
	int fd;
	struct strbuf sb = STRBUF_INIT;

	switch (st->st_mode & S_IFMT) {
	case S_IFREG:
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return error("open(\"%s\"): %s", path,
				     strerror(errno));
		if (index_fd(sha1, fd, st, OBJ_BLOB, path, flags) < 0)
			return error("%s: failed to insert into database",
				     path);
		break;
	case S_IFLNK:
		if (strbuf_readlink(&sb, path, st->st_size)) {
			char *errstr = strerror(errno);
			return error("readlink(\"%s\"): %s", path,
			             errstr);
		}
		if (!(flags & HASH_WRITE_OBJECT))
			hash_sha1_file(sb.buf, sb.len, blob_type, sha1);
		else if (write_sha1_file(sb.buf, sb.len, blob_type, sha1))
			return error("%s: failed to insert into database",
				     path);
		strbuf_release(&sb);
		break;
	case S_IFDIR:
		return resolve_gitlink_ref(path, "HEAD", sha1);
	default:
		return error("%s: unsupported file type", path);
	}
	return 0;
}

int read_pack_header(int fd, struct pack_header *header)
{
	if (read_in_full(fd, header, sizeof(*header)) < sizeof(*header))
		/* "eof before pack header was fully read" */
		return PH_ERROR_EOF;

	if (header->hdr_signature != htonl(PACK_SIGNATURE))
		/* "protocol error (pack signature mismatch detected)" */
		return PH_ERROR_PACK_SIGNATURE;
	if (!pack_version_ok(header->hdr_version))
		/* "protocol error (pack version unsupported)" */
		return PH_ERROR_PROTOCOL;
	return 0;
}

void assert_sha1_type(const unsigned char *sha1, enum object_type expect)
{
	enum object_type type = sha1_object_info(sha1, NULL);
	if (type < 0)
		die("%s is not a valid object", sha1_to_hex(sha1));
	if (type != expect)
		die("%s is not a valid '%s' object", sha1_to_hex(sha1),
		    typename(expect));
}
