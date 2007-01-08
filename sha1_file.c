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
#include "tag.h"
#include "tree.h"

#ifndef O_NOATIME
#if defined(__linux__) && (defined(__i386__) || defined(__PPC__))
#define O_NOATIME 01000000
#else
#define O_NOATIME 0
#endif
#endif

const unsigned char null_sha1[20];

static unsigned int sha1_file_open_flag = O_NOATIME;

signed char hexval_table[256] = {
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 00-07 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 08-0f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 10-17 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 18-1f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 20-27 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 28-2f */
	  0,  1,  2,  3,  4,  5,  6,  7,		/* 30-37 */
	  8,  9, -1, -1, -1, -1, -1, -1,		/* 38-3f */
	 -1, 10, 11, 12, 13, 14, 15, -1,		/* 40-47 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 48-4f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 50-57 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 58-5f */
	 -1, 10, 11, 12, 13, 14, 15, -1,		/* 60-67 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 68-67 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 70-77 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 78-7f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 80-87 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 88-8f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 90-97 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 98-9f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* a0-a7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* a8-af */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* b0-b7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* b8-bf */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* c0-c7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* c8-cf */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* d0-d7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* d8-df */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* e0-e7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* e8-ef */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* f0-f7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* f8-ff */
};

int get_sha1_hex(const char *hex, unsigned char *sha1)
{
	int i;
	for (i = 0; i < 20; i++) {
		unsigned int val = (hexval(hex[0]) << 4) | hexval(hex[1]);
		if (val & ~0xff)
			return -1;
		*sha1++ = val;
		hex += 2;
	}
	return 0;
}

int safe_create_leading_directories(char *path)
{
	char *pos = path;
	struct stat st;

	if (*pos == '/')
		pos++;

	while (pos) {
		pos = strchr(pos, '/');
		if (!pos)
			break;
		*pos = 0;
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

char * sha1_to_hex(const unsigned char *sha1)
{
	static int bufno;
	static char hexbuffer[4][50];
	static const char hex[] = "0123456789abcdef";
	char *buffer = hexbuffer[3 & ++bufno], *buf = buffer;
	int i;

	for (i = 0; i < 20; i++) {
		unsigned int val = *sha1++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	*buf = '\0';

	return buffer;
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
 * careful about using it. Do a "xstrdup()" if you need to save the
 * filename.
 *
 * Also note that this returns the location for creating.  Reading
 * SHA1 file can happen from any alternate directory listed in the
 * DB_ENVIRONMENT environment variable if it is not found in
 * the primary object database.
 */
char *sha1_file_name(const unsigned char *sha1)
{
	static char *name, *base;

	if (!base) {
		const char *sha1_file_directory = get_object_directory();
		int len = strlen(sha1_file_directory);
		base = xmalloc(len + 60);
		memcpy(base, sha1_file_directory, len);
		memset(base+len, 0, 60);
		base[len] = '/';
		base[len+3] = '/';
		name = base + len + 1;
	}
	fill_sha1_path(name, sha1);
	return base;
}

char *sha1_pack_name(const unsigned char *sha1)
{
	static const char hex[] = "0123456789abcdef";
	static char *name, *base, *buf;
	int i;

	if (!base) {
		const char *sha1_file_directory = get_object_directory();
		int len = strlen(sha1_file_directory);
		base = xmalloc(len + 60);
		sprintf(base, "%s/pack/pack-1234567890123456789012345678901234567890.pack", sha1_file_directory);
		name = base + len + 11;
	}

	buf = name;

	for (i = 0; i < 20; i++) {
		unsigned int val = *sha1++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	
	return base;
}

char *sha1_pack_index_name(const unsigned char *sha1)
{
	static const char hex[] = "0123456789abcdef";
	static char *name, *base, *buf;
	int i;

	if (!base) {
		const char *sha1_file_directory = get_object_directory();
		int len = strlen(sha1_file_directory);
		base = xmalloc(len + 60);
		sprintf(base, "%s/pack/pack-1234567890123456789012345678901234567890.idx", sha1_file_directory);
		name = base + len + 11;
	}

	buf = name;

	for (i = 0; i < 20; i++) {
		unsigned int val = *sha1++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	
	return base;
}

struct alternate_object_database *alt_odb_list;
static struct alternate_object_database **alt_odb_tail;

static void read_info_alternates(const char * alternates, int depth);

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
	struct stat st;
	const char *objdir = get_object_directory();
	struct alternate_object_database *ent;
	struct alternate_object_database *alt;
	/* 43 = 40-byte + 2 '/' + terminating NUL */
	int pfxlen = len;
	int entlen = pfxlen + 43;
	int base_len = -1;

	if (*entry != '/' && relative_base) {
		/* Relative alt-odb */
		if (base_len < 0)
			base_len = strlen(relative_base) + 1;
		entlen += base_len;
		pfxlen += base_len;
	}
	ent = xmalloc(sizeof(*ent) + entlen);

	if (*entry != '/' && relative_base) {
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
	if (stat(ent->base, &st) || !S_ISDIR(st.st_mode)) {
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
			if ((*last != '/') && depth) {
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
	struct stat st;
	char path[PATH_MAX];
	int fd;

	sprintf(path, "%s/info/alternates", relative_base);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return;
	if (fstat(fd, &st) || (st.st_size == 0)) {
		close(fd);
		return;
	}
	map = xmmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	link_alt_odb_entries(map, map + st.st_size, '\n', relative_base, depth);

	munmap(map, st.st_size);
}

void prepare_alt_odb(void)
{
	const char *alt;

	alt = getenv(ALTERNATE_DB_ENVIRONMENT);
	if (!alt) alt = "";

	if (alt_odb_tail)
		return;
	alt_odb_tail = &alt_odb_list;
	link_alt_odb_entries(alt, alt + strlen(alt), ':', NULL, 0);

	read_info_alternates(get_object_directory(), 0);
}

static char *find_sha1_file(const unsigned char *sha1, struct stat *st)
{
	char *name = sha1_file_name(sha1);
	struct alternate_object_database *alt;

	if (!stat(name, st))
		return name;
	prepare_alt_odb();
	for (alt = alt_odb_list; alt; alt = alt->next) {
		name = alt->name;
		fill_sha1_path(name, sha1);
		if (!stat(alt->base, st))
			return alt->base;
	}
	return NULL;
}

static unsigned int pack_used_ctr;
static unsigned int pack_mmap_calls;
static unsigned int peak_pack_open_windows;
static unsigned int pack_open_windows;
static size_t peak_pack_mapped;
static size_t pack_mapped;
static size_t page_size;
struct packed_git *packed_git;

void pack_report()
{
	fprintf(stderr,
		"pack_report: getpagesize()            = %10lu\n"
		"pack_report: core.packedGitWindowSize = %10lu\n"
		"pack_report: core.packedGitLimit      = %10lu\n",
		page_size,
		packed_git_window_size,
		packed_git_limit);
	fprintf(stderr,
		"pack_report: pack_used_ctr            = %10u\n"
		"pack_report: pack_mmap_calls          = %10u\n"
		"pack_report: pack_open_windows        = %10u / %10u\n"
		"pack_report: pack_mapped              = %10lu / %10lu\n",
		pack_used_ctr,
		pack_mmap_calls,
		pack_open_windows, peak_pack_open_windows,
		pack_mapped, peak_pack_mapped);
}

static int check_packed_git_idx(const char *path, unsigned long *idx_size_,
				void **idx_map_)
{
	void *idx_map;
	unsigned int *index;
	unsigned long idx_size;
	int nr, i;
	int fd = open(path, O_RDONLY);
	struct stat st;
	if (fd < 0)
		return -1;
	if (fstat(fd, &st)) {
		close(fd);
		return -1;
	}
	idx_size = st.st_size;
	idx_map = xmmap(NULL, idx_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	index = idx_map;
	*idx_map_ = idx_map;
	*idx_size_ = idx_size;

	/* check index map */
	if (idx_size < 4*256 + 20 + 20)
		return error("index file too small");
	nr = 0;
	for (i = 0; i < 256; i++) {
		unsigned int n = ntohl(index[i]);
		if (n < nr)
			return error("non-monotonic index");
		nr = n;
	}

	/*
	 * Total size:
	 *  - 256 index entries 4 bytes each
	 *  - 24-byte entries * nr (20-byte sha1 + 4-byte offset)
	 *  - 20-byte SHA1 of the packfile
	 *  - 20-byte SHA1 file checksum
	 */
	if (idx_size != 4*256 + nr * 24 + 20 + 20)
		return error("wrong index file size");

	return 0;
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

static int unuse_one_window(struct packed_git *current)
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
			if (!lru_p->windows && lru_p != current) {
				close(lru_p->pack_fd);
				lru_p->pack_fd = -1;
			}
		}
		free(lru_w);
		pack_open_windows--;
		return 1;
	}
	return 0;
}

void release_pack_memory(size_t need)
{
	size_t cur = pack_mapped;
	while (need >= (cur - pack_mapped) && unuse_one_window(NULL))
		; /* nothing */
}

void unuse_pack(struct pack_window **w_cursor)
{
	struct pack_window *w = *w_cursor;
	if (w) {
		w->inuse_cnt--;
		*w_cursor = NULL;
	}
}

static void open_packed_git(struct packed_git *p)
{
	struct stat st;
	struct pack_header hdr;
	unsigned char sha1[20];
	unsigned char *idx_sha1;
	long fd_flag;

	p->pack_fd = open(p->pack_name, O_RDONLY);
	if (p->pack_fd < 0 || fstat(p->pack_fd, &st))
		die("packfile %s cannot be opened", p->pack_name);

	/* If we created the struct before we had the pack we lack size. */
	if (!p->pack_size) {
		if (!S_ISREG(st.st_mode))
			die("packfile %s not a regular file", p->pack_name);
		p->pack_size = st.st_size;
	} else if (p->pack_size != st.st_size)
		die("packfile %s size changed", p->pack_name);

	/* We leave these file descriptors open with sliding mmap;
	 * there is no point keeping them open across exec(), though.
	 */
	fd_flag = fcntl(p->pack_fd, F_GETFD, 0);
	if (fd_flag < 0)
		die("cannot determine file descriptor flags");
	fd_flag |= FD_CLOEXEC;
	if (fcntl(p->pack_fd, F_SETFD, fd_flag) == -1)
		die("cannot set FD_CLOEXEC");

	/* Verify we recognize this pack file format. */
	read_or_die(p->pack_fd, &hdr, sizeof(hdr));
	if (hdr.hdr_signature != htonl(PACK_SIGNATURE))
		die("file %s is not a GIT packfile", p->pack_name);
	if (!pack_version_ok(hdr.hdr_version))
		die("packfile %s is version %u and not supported"
			" (try upgrading GIT to a newer version)",
			p->pack_name, ntohl(hdr.hdr_version));

	/* Verify the pack matches its index. */
	if (num_packed_objects(p) != ntohl(hdr.hdr_entries))
		die("packfile %s claims to have %u objects"
			" while index size indicates %u objects",
			p->pack_name, ntohl(hdr.hdr_entries),
			num_packed_objects(p));
	if (lseek(p->pack_fd, p->pack_size - sizeof(sha1), SEEK_SET) == -1)
		die("end of packfile %s is unavailable", p->pack_name);
	read_or_die(p->pack_fd, sha1, sizeof(sha1));
	idx_sha1 = ((unsigned char *)p->index_base) + p->index_size - 40;
	if (hashcmp(sha1, idx_sha1))
		die("packfile %s does not match index", p->pack_name);
}

static int in_window(struct pack_window *win, unsigned long offset)
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

unsigned char* use_pack(struct packed_git *p,
		struct pack_window **w_cursor,
		unsigned long offset,
		unsigned int *left)
{
	struct pack_window *win = *w_cursor;

	if (p->pack_fd == -1)
		open_packed_git(p);

	/* Since packfiles end in a hash of their content and its
	 * pointless to ask for an offset into the middle of that
	 * hash, and the in_window function above wouldn't match
	 * don't allow an offset too close to the end of the file.
	 */
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
			if (!page_size)
				page_size = getpagesize();
			win = xcalloc(1, sizeof(*win));
			win->offset = (offset / page_size) * page_size;
			win->len = p->pack_size - win->offset;
			if (win->len > packed_git_window_size)
				win->len = packed_git_window_size;
			pack_mapped += win->len;
			while (packed_git_limit < pack_mapped
				&& unuse_one_window(p))
				; /* nothing */
			win->base = xmmap(NULL, win->len,
				PROT_READ, MAP_PRIVATE,
				p->pack_fd, win->offset);
			if (win->base == MAP_FAILED)
				die("packfile %s cannot be mapped: %s",
					p->pack_name,
					strerror(errno));
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
		*left = win->len - offset;
	return win->base + offset;
}

struct packed_git *add_packed_git(char *path, int path_len, int local)
{
	struct stat st;
	struct packed_git *p;
	unsigned long idx_size;
	void *idx_map;
	unsigned char sha1[20];

	if (check_packed_git_idx(path, &idx_size, &idx_map))
		return NULL;

	/* do we have a corresponding .pack file? */
	strcpy(path + path_len - 4, ".pack");
	if (stat(path, &st) || !S_ISREG(st.st_mode)) {
		munmap(idx_map, idx_size);
		return NULL;
	}
	/* ok, it looks sane as far as we can check without
	 * actually mapping the pack file.
	 */
	p = xmalloc(sizeof(*p) + path_len + 2);
	strcpy(p->pack_name, path);
	p->index_size = idx_size;
	p->pack_size = st.st_size;
	p->index_base = idx_map;
	p->next = NULL;
	p->windows = NULL;
	p->pack_fd = -1;
	p->pack_local = local;
	if ((path_len > 44) && !get_sha1_hex(path + path_len - 44, sha1))
		hashcpy(p->sha1, sha1);
	return p;
}

struct packed_git *parse_pack_index(unsigned char *sha1)
{
	char *path = sha1_pack_index_name(sha1);
	return parse_pack_index_file(sha1, path);
}

struct packed_git *parse_pack_index_file(const unsigned char *sha1, char *idx_path)
{
	struct packed_git *p;
	unsigned long idx_size;
	void *idx_map;
	char *path;

	if (check_packed_git_idx(idx_path, &idx_size, &idx_map))
		return NULL;

	path = sha1_pack_name(sha1);

	p = xmalloc(sizeof(*p) + strlen(path) + 2);
	strcpy(p->pack_name, path);
	p->index_size = idx_size;
	p->pack_size = 0;
	p->index_base = idx_map;
	p->next = NULL;
	p->windows = NULL;
	p->pack_fd = -1;
	hashcpy(p->sha1, sha1);
	return p;
}

void install_packed_git(struct packed_git *pack)
{
	pack->next = packed_git;
	packed_git = pack;
}

static void prepare_packed_git_one(char *objdir, int local)
{
	char path[PATH_MAX];
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

		/* we have .idx.  Is it a file we can map? */
		strcpy(path + len, de->d_name);
		for (p = packed_git; p; p = p->next) {
			if (!memcmp(path, p->pack_name, len + namelen - 4))
				break;
		}
		if (p)
			continue;
		p = add_packed_git(path, len + namelen, local);
		if (!p)
			continue;
		p->next = packed_git;
		packed_git = p;
	}
	closedir(dir);
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
	prepare_packed_git_run_once = 1;
}

void reprepare_packed_git(void)
{
	prepare_packed_git_run_once = 0;
	prepare_packed_git();
}

int check_sha1_signature(const unsigned char *sha1, void *map, unsigned long size, const char *type)
{
	unsigned char real_sha1[20];
	hash_sha1_file(map, size, type, real_sha1);
	return hashcmp(sha1, real_sha1) ? -1 : 0;
}

void *map_sha1_file(const unsigned char *sha1, unsigned long *size)
{
	struct stat st;
	void *map;
	int fd;
	char *filename = find_sha1_file(sha1, &st);

	if (!filename) {
		return NULL;
	}

	fd = open(filename, O_RDONLY | sha1_file_open_flag);
	if (fd < 0) {
		/* See if it works without O_NOATIME */
		switch (sha1_file_open_flag) {
		default:
			fd = open(filename, O_RDONLY);
			if (fd >= 0)
				break;
		/* Fallthrough */
		case 0:
			return NULL;
		}

		/* If it failed once, it will probably fail again.
		 * Stop using O_NOATIME
		 */
		sha1_file_open_flag = 0;
	}
	map = xmmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	*size = st.st_size;
	return map;
}

int legacy_loose_object(unsigned char *map)
{
	unsigned int word;

	/*
	 * Is it a zlib-compressed buffer? If so, the first byte
	 * must be 0x78 (15-bit window size, deflated), and the
	 * first 16-bit word is evenly divisible by 31
	 */
	word = (map[0] << 8) + map[1];
	if (map[0] == 0x78 && !(word % 31))
		return 1;
	else
		return 0;
}

unsigned long unpack_object_header_gently(const unsigned char *buf, unsigned long len, enum object_type *type, unsigned long *sizep)
{
	unsigned shift;
	unsigned char c;
	unsigned long size;
	unsigned long used = 0;

	c = buf[used++];
	*type = (c >> 4) & 7;
	size = c & 15;
	shift = 4;
	while (c & 0x80) {
		if (len <= used)
			return 0;
		if (sizeof(long) * 8 <= shift)
			return 0;
		c = buf[used++];
		size += (c & 0x7f) << shift;
		shift += 7;
	}
	*sizep = size;
	return used;
}

static int unpack_sha1_header(z_stream *stream, unsigned char *map, unsigned long mapsize, void *buffer, unsigned long bufsiz)
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

	if (legacy_loose_object(map)) {
		inflateInit(stream);
		return inflate(stream, 0);
	}

	used = unpack_object_header_gently(map, mapsize, &type, &size);
	if (!used || !valid_loose_object_type[type])
		return -1;
	map += used;
	mapsize -= used;

	/* Set up the stream for the rest.. */
	stream->next_in = map;
	stream->avail_in = mapsize;
	inflateInit(stream);

	/* And generate the fake traditional header */
	stream->total_out = 1 + snprintf(buffer, bufsiz, "%s %lu",
					 type_names[type], size);
	return 0;
}

static void *unpack_sha1_rest(z_stream *stream, void *buffer, unsigned long size)
{
	int bytes = strlen(buffer) + 1;
	unsigned char *buf = xmalloc(1+size);
	unsigned long n;

	n = stream->total_out - bytes;
	if (n > size)
		n = size;
	memcpy(buf, (char *) buffer + bytes, n);
	bytes = n;
	if (bytes < size) {
		stream->next_out = buf + bytes;
		stream->avail_out = size - bytes;
		while (inflate(stream, Z_FINISH) == Z_OK)
			/* nothing */;
	}
	buf[size] = 0;
	inflateEnd(stream);
	return buf;
}

/*
 * We used to just use "sscanf()", but that's actually way
 * too permissive for what we want to check. So do an anal
 * object header parse by hand.
 */
static int parse_sha1_header(char *hdr, char *type, unsigned long *sizep)
{
	int i;
	unsigned long size;

	/*
	 * The type can be at most ten bytes (including the 
	 * terminating '\0' that we add), and is followed by
	 * a space. 
	 */
	i = 10;
	for (;;) {
		char c = *hdr++;
		if (c == ' ')
			break;
		if (!--i)
			return -1;
		*type++ = c;
	}
	*type = 0;

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
	return *hdr ? -1 : 0;
}

void * unpack_sha1_file(void *map, unsigned long mapsize, char *type, unsigned long *size)
{
	int ret;
	z_stream stream;
	char hdr[8192];

	ret = unpack_sha1_header(&stream, map, mapsize, hdr, sizeof(hdr));
	if (ret < Z_OK || parse_sha1_header(hdr, type, size) < 0)
		return NULL;

	return unpack_sha1_rest(&stream, hdr, *size);
}

static unsigned long get_delta_base(struct packed_git *p,
				    struct pack_window **w_curs,
				    unsigned long offset,
				    enum object_type kind,
				    unsigned long delta_obj_offset,
				    unsigned long *base_obj_offset)
{
	unsigned char *base_info = use_pack(p, w_curs, offset, NULL);
	unsigned long base_offset;

	/* use_pack() assured us we have [base_info, base_info + 20)
	 * as a range that we can look at without walking off the
	 * end of the mapped window.  Its actually the hash size
	 * that is assured.  An OFS_DELTA longer than the hash size
	 * is stupid, as then a REF_DELTA would be smaller to store.
	 */
	if (kind == OBJ_OFS_DELTA) {
		unsigned used = 0;
		unsigned char c = base_info[used++];
		base_offset = c & 127;
		while (c & 128) {
			base_offset += 1;
			if (!base_offset || base_offset & ~(~0UL >> 7))
				die("offset value overflow for delta base object");
			c = base_info[used++];
			base_offset = (base_offset << 7) + (c & 127);
		}
		base_offset = delta_obj_offset - base_offset;
		if (base_offset >= delta_obj_offset)
			die("delta base offset out of bound");
		offset += used;
	} else if (kind == OBJ_REF_DELTA) {
		/* The base entry _must_ be in the same pack */
		base_offset = find_pack_entry_one(base_info, p);
		if (!base_offset)
			die("failed to find delta-pack base object %s",
				sha1_to_hex(base_info));
		offset += 20;
	} else
		die("I am totally screwed");
	*base_obj_offset = base_offset;
	return offset;
}

/* forward declaration for a mutually recursive function */
static int packed_object_info(struct packed_git *p, unsigned long offset,
			      char *type, unsigned long *sizep);

static int packed_delta_info(struct packed_git *p,
			     struct pack_window **w_curs,
			     unsigned long offset,
			     enum object_type kind,
			     unsigned long obj_offset,
			     char *type,
			     unsigned long *sizep)
{
	unsigned long base_offset;

	offset = get_delta_base(p, w_curs, offset, kind,
		obj_offset, &base_offset);

	/* We choose to only get the type of the base object and
	 * ignore potentially corrupt pack file that expects the delta
	 * based on a base with a wrong size.  This saves tons of
	 * inflate() calls.
	 */
	if (packed_object_info(p, base_offset, type, NULL))
		die("cannot get info for delta-pack base");

	if (sizep) {
		const unsigned char *data;
		unsigned char delta_head[20], *in;
		unsigned long result_size;
		z_stream stream;
		int st;

		memset(&stream, 0, sizeof(stream));
		stream.next_out = delta_head;
		stream.avail_out = sizeof(delta_head);

		inflateInit(&stream);
		do {
			in = use_pack(p, w_curs, offset, &stream.avail_in);
			stream.next_in = in;
			st = inflate(&stream, Z_FINISH);
			offset += stream.next_in - in;
		} while ((st == Z_OK || st == Z_BUF_ERROR)
			&& stream.total_out < sizeof(delta_head));
		inflateEnd(&stream);
		if ((st != Z_STREAM_END) &&
		    stream.total_out != sizeof(delta_head))
			die("delta data unpack-initial failed");

		/* Examine the initial part of the delta to figure out
		 * the result size.
		 */
		data = delta_head;

		/* ignore base size */
		get_delta_hdr_size(&data, delta_head+sizeof(delta_head));

		/* Read the result size */
		result_size = get_delta_hdr_size(&data, delta_head+sizeof(delta_head));
		*sizep = result_size;
	}
	return 0;
}

static unsigned long unpack_object_header(struct packed_git *p,
		struct pack_window **w_curs,
		unsigned long offset,
		enum object_type *type,
		unsigned long *sizep)
{
	unsigned char *base;
	unsigned int left;
	unsigned long used;

	/* use_pack() assures us we have [base, base + 20) available
	 * as a range that we can look at at.  (Its actually the hash
	 * size that is assurred.)  With our object header encoding
	 * the maximum deflated object size is 2^137, which is just
	 * insane, so we know won't exceed what we have been given.
	 */
	base = use_pack(p, w_curs, offset, &left);
	used = unpack_object_header_gently(base, left, type, sizep);
	if (!used)
		die("object offset outside of pack file");

	return offset + used;
}

void packed_object_info_detail(struct packed_git *p,
			       unsigned long offset,
			       char *type,
			       unsigned long *size,
			       unsigned long *store_size,
			       unsigned int *delta_chain_length,
			       unsigned char *base_sha1)
{
	struct pack_window *w_curs = NULL;
	unsigned long obj_offset, val;
	unsigned char *next_sha1;
	enum object_type kind;

	*delta_chain_length = 0;
	obj_offset = offset;
	offset = unpack_object_header(p, &w_curs, offset, &kind, size);

	for (;;) {
		switch (kind) {
		default:
			die("pack %s contains unknown object type %d",
			    p->pack_name, kind);
		case OBJ_COMMIT:
		case OBJ_TREE:
		case OBJ_BLOB:
		case OBJ_TAG:
			strcpy(type, type_names[kind]);
			*store_size = 0; /* notyet */
			unuse_pack(&w_curs);
			return;
		case OBJ_OFS_DELTA:
			get_delta_base(p, &w_curs, offset, kind,
				obj_offset, &offset);
			if (*delta_chain_length == 0) {
				/* TODO: find base_sha1 as pointed by offset */
			}
			break;
		case OBJ_REF_DELTA:
			next_sha1 = use_pack(p, &w_curs, offset, NULL);
			if (*delta_chain_length == 0)
				hashcpy(base_sha1, next_sha1);
			offset = find_pack_entry_one(next_sha1, p);
			break;
		}
		obj_offset = offset;
		offset = unpack_object_header(p, &w_curs, offset, &kind, &val);
		(*delta_chain_length)++;
	}
}

static int packed_object_info(struct packed_git *p, unsigned long offset,
			      char *type, unsigned long *sizep)
{
	struct pack_window *w_curs = NULL;
	unsigned long size, obj_offset = offset;
	enum object_type kind;
	int r;

	offset = unpack_object_header(p, &w_curs, offset, &kind, &size);

	switch (kind) {
	case OBJ_OFS_DELTA:
	case OBJ_REF_DELTA:
		r = packed_delta_info(p, &w_curs, offset, kind,
			obj_offset, type, sizep);
		unuse_pack(&w_curs);
		return r;
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		strcpy(type, type_names[kind]);
		unuse_pack(&w_curs);
		break;
	default:
		die("pack %s contains unknown object type %d",
		    p->pack_name, kind);
	}
	if (sizep)
		*sizep = size;
	return 0;
}

static void *unpack_compressed_entry(struct packed_git *p,
				    struct pack_window **w_curs,
				    unsigned long offset,
				    unsigned long size)
{
	int st;
	z_stream stream;
	unsigned char *buffer, *in;

	buffer = xmalloc(size + 1);
	buffer[size] = 0;
	memset(&stream, 0, sizeof(stream));
	stream.next_out = buffer;
	stream.avail_out = size;

	inflateInit(&stream);
	do {
		in = use_pack(p, w_curs, offset, &stream.avail_in);
		stream.next_in = in;
		st = inflate(&stream, Z_FINISH);
		offset += stream.next_in - in;
	} while (st == Z_OK || st == Z_BUF_ERROR);
	inflateEnd(&stream);
	if ((st != Z_STREAM_END) || stream.total_out != size) {
		free(buffer);
		return NULL;
	}

	return buffer;
}

static void *unpack_delta_entry(struct packed_git *p,
				struct pack_window **w_curs,
				unsigned long offset,
				unsigned long delta_size,
				enum object_type kind,
				unsigned long obj_offset,
				char *type,
				unsigned long *sizep)
{
	void *delta_data, *result, *base;
	unsigned long result_size, base_size, base_offset;

	offset = get_delta_base(p, w_curs, offset, kind,
		obj_offset, &base_offset);
	base = unpack_entry(p, base_offset, type, &base_size);
	if (!base)
		die("failed to read delta base object at %lu from %s",
		    base_offset, p->pack_name);

	delta_data = unpack_compressed_entry(p, w_curs, offset, delta_size);
	result = patch_delta(base, base_size,
			     delta_data, delta_size,
			     &result_size);
	if (!result)
		die("failed to apply delta");
	free(delta_data);
	free(base);
	*sizep = result_size;
	return result;
}

void *unpack_entry(struct packed_git *p, unsigned long offset,
			  char *type, unsigned long *sizep)
{
	struct pack_window *w_curs = NULL;
	unsigned long size, obj_offset = offset;
	enum object_type kind;
	void *retval;

	offset = unpack_object_header(p, &w_curs, offset, &kind, &size);
	switch (kind) {
	case OBJ_OFS_DELTA:
	case OBJ_REF_DELTA:
		retval = unpack_delta_entry(p, &w_curs, offset, size,
			kind, obj_offset, type, sizep);
		break;
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		strcpy(type, type_names[kind]);
		*sizep = size;
		retval = unpack_compressed_entry(p, &w_curs, offset, size);
		break;
	default:
		die("unknown object type %i in %s", kind, p->pack_name);
	}
	unuse_pack(&w_curs);
	return retval;
}

int num_packed_objects(const struct packed_git *p)
{
	/* See check_packed_git_idx() */
	return (p->index_size - 20 - 20 - 4*256) / 24;
}

int nth_packed_object_sha1(const struct packed_git *p, int n,
			   unsigned char* sha1)
{
	void *index = p->index_base + 256;
	if (n < 0 || num_packed_objects(p) <= n)
		return -1;
	hashcpy(sha1, (unsigned char *) index + (24 * n) + 4);
	return 0;
}

unsigned long find_pack_entry_one(const unsigned char *sha1,
				  struct packed_git *p)
{
	unsigned int *level1_ofs = p->index_base;
	int hi = ntohl(level1_ofs[*sha1]);
	int lo = ((*sha1 == 0x0) ? 0 : ntohl(level1_ofs[*sha1 - 1]));
	void *index = p->index_base + 256;

	do {
		int mi = (lo + hi) / 2;
		int cmp = hashcmp((unsigned char *)index + (24 * mi) + 4, sha1);
		if (!cmp)
			return ntohl(*((unsigned int *) ((char *) index + (24 * mi))));
		if (cmp > 0)
			hi = mi;
		else
			lo = mi+1;
	} while (lo < hi);
	return 0;
}

static int matches_pack_name(struct packed_git *p, const char *ig)
{
	const char *last_c, *c;

	if (!strcmp(p->pack_name, ig))
		return 0;

	for (c = p->pack_name, last_c = c; *c;)
		if (*c == '/')
			last_c = ++c;
		else
			++c;
	if (!strcmp(last_c, ig))
		return 0;

	return 1;
}

static int find_pack_entry(const unsigned char *sha1, struct pack_entry *e, const char **ignore_packed)
{
	struct packed_git *p;
	unsigned long offset;

	prepare_packed_git();

	for (p = packed_git; p; p = p->next) {
		if (ignore_packed) {
			const char **ig;
			for (ig = ignore_packed; *ig; ig++)
				if (!matches_pack_name(p, *ig))
					break;
			if (*ig)
				continue;
		}
		offset = find_pack_entry_one(sha1, p);
		if (offset) {
			e->offset = offset;
			e->p = p;
			hashcpy(e->sha1, sha1);
			return 1;
		}
	}
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

static int sha1_loose_object_info(const unsigned char *sha1, char *type, unsigned long *sizep)
{
	int status;
	unsigned long mapsize, size;
	void *map;
	z_stream stream;
	char hdr[128];

	map = map_sha1_file(sha1, &mapsize);
	if (!map)
		return error("unable to find %s", sha1_to_hex(sha1));
	if (unpack_sha1_header(&stream, map, mapsize, hdr, sizeof(hdr)) < 0)
		status = error("unable to unpack %s header",
			       sha1_to_hex(sha1));
	if (parse_sha1_header(hdr, type, &size) < 0)
		status = error("unable to parse %s header", sha1_to_hex(sha1));
	else {
		status = 0;
		if (sizep)
			*sizep = size;
	}
	inflateEnd(&stream);
	munmap(map, mapsize);
	return status;
}

int sha1_object_info(const unsigned char *sha1, char *type, unsigned long *sizep)
{
	struct pack_entry e;

	if (!find_pack_entry(sha1, &e, NULL)) {
		reprepare_packed_git();
		if (!find_pack_entry(sha1, &e, NULL))
			return sha1_loose_object_info(sha1, type, sizep);
	}
	return packed_object_info(e.p, e.offset, type, sizep);
}

static void *read_packed_sha1(const unsigned char *sha1, char *type, unsigned long *size)
{
	struct pack_entry e;

	if (!find_pack_entry(sha1, &e, NULL)) {
		error("cannot read sha1_file for %s", sha1_to_hex(sha1));
		return NULL;
	}
	return unpack_entry(e.p, e.offset, type, size);
}

void * read_sha1_file(const unsigned char *sha1, char *type, unsigned long *size)
{
	unsigned long mapsize;
	void *map, *buf;
	struct pack_entry e;

	if (find_pack_entry(sha1, &e, NULL))
		return read_packed_sha1(sha1, type, size);
	map = map_sha1_file(sha1, &mapsize);
	if (map) {
		buf = unpack_sha1_file(map, mapsize, type, size);
		munmap(map, mapsize);
		return buf;
	}
	reprepare_packed_git();
	if (find_pack_entry(sha1, &e, NULL))
		return read_packed_sha1(sha1, type, size);
	return NULL;
}

void *read_object_with_reference(const unsigned char *sha1,
				 const char *required_type,
				 unsigned long *size,
				 unsigned char *actual_sha1_return)
{
	char type[20];
	void *buffer;
	unsigned long isize;
	unsigned char actual_sha1[20];

	hashcpy(actual_sha1, sha1);
	while (1) {
		int ref_length = -1;
		const char *ref_type = NULL;

		buffer = read_sha1_file(actual_sha1, type, &isize);
		if (!buffer)
			return NULL;
		if (!strcmp(type, required_type)) {
			*size = isize;
			if (actual_sha1_return)
				hashcpy(actual_sha1_return, actual_sha1);
			return buffer;
		}
		/* Handle references */
		else if (!strcmp(type, commit_type))
			ref_type = "tree ";
		else if (!strcmp(type, tag_type))
			ref_type = "object ";
		else {
			free(buffer);
			return NULL;
		}
		ref_length = strlen(ref_type);

		if (memcmp(buffer, ref_type, ref_length) ||
		    get_sha1_hex((char *) buffer + ref_length, actual_sha1)) {
			free(buffer);
			return NULL;
		}
		free(buffer);
		/* Now we have the ID of the referred-to object in
		 * actual_sha1.  Check again. */
	}
}

static void write_sha1_file_prepare(void *buf, unsigned long len,
                                    const char *type, unsigned char *sha1,
                                    unsigned char *hdr, int *hdrlen)
{
	SHA_CTX c;

	/* Generate the header */
	*hdrlen = sprintf((char *)hdr, "%s %lu", type, len)+1;

	/* Sha1.. */
	SHA1_Init(&c);
	SHA1_Update(&c, hdr, *hdrlen);
	SHA1_Update(&c, buf, len);
	SHA1_Final(sha1, &c);
}

/*
 * Link the tempfile to the final place, possibly creating the
 * last directory level as you do so.
 *
 * Returns the errno on failure, 0 on success.
 */
static int link_temp_to_file(const char *tmpfile, const char *filename)
{
	int ret;
	char *dir;

	if (!link(tmpfile, filename))
		return 0;

	/*
	 * Try to mkdir the last path component if that failed.
	 *
	 * Re-try the "link()" regardless of whether the mkdir
	 * succeeds, since a race might mean that somebody
	 * else succeeded.
	 */
	ret = errno;
	dir = strrchr(filename, '/');
	if (dir) {
		*dir = 0;
		if (!mkdir(filename, 0777) && adjust_shared_perm(filename)) {
			*dir = '/';
			return -2;
		}
		*dir = '/';
		if (!link(tmpfile, filename))
			return 0;
		ret = errno;
	}
	return ret;
}

/*
 * Move the just written object into its final resting place
 */
int move_temp_to_file(const char *tmpfile, const char *filename)
{
	int ret = link_temp_to_file(tmpfile, filename);

	/*
	 * Coda hack - coda doesn't like cross-directory links,
	 * so we fall back to a rename, which will mean that it
	 * won't be able to check collisions, but that's not a
	 * big deal.
	 *
	 * The same holds for FAT formatted media.
	 *
	 * When this succeeds, we just return 0. We have nothing
	 * left to unlink.
	 */
	if (ret && ret != EEXIST) {
		if (!rename(tmpfile, filename))
			return 0;
		ret = errno;
	}
	unlink(tmpfile);
	if (ret) {
		if (ret != EEXIST) {
			return error("unable to write sha1 filename %s: %s\n", filename, strerror(ret));
		}
		/* FIXME!!! Collision check here ? */
	}

	return 0;
}

static int write_buffer(int fd, const void *buf, size_t len)
{
	while (len) {
		ssize_t size;

		size = write(fd, buf, len);
		if (!size)
			return error("file write: disk full");
		if (size < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return error("file write error (%s)", strerror(errno));
		}
		len -= size;
		buf = (char *) buf + size;
	}
	return 0;
}

static int write_binary_header(unsigned char *hdr, enum object_type type, unsigned long len)
{
	int hdr_len;
	unsigned char c;

	c = (type << 4) | (len & 15);
	len >>= 4;
	hdr_len = 1;
	while (len) {
		*hdr++ = c | 0x80;
		hdr_len++;
		c = (len & 0x7f);
		len >>= 7;
	}
	*hdr = c;
	return hdr_len;
}

static void setup_object_header(z_stream *stream, const char *type, unsigned long len)
{
	int obj_type, hdr;

	if (use_legacy_headers) {
		while (deflate(stream, 0) == Z_OK)
			/* nothing */;
		return;
	}
	if (!strcmp(type, blob_type))
		obj_type = OBJ_BLOB;
	else if (!strcmp(type, tree_type))
		obj_type = OBJ_TREE;
	else if (!strcmp(type, commit_type))
		obj_type = OBJ_COMMIT;
	else if (!strcmp(type, tag_type))
		obj_type = OBJ_TAG;
	else
		die("trying to generate bogus object of type '%s'", type);
	hdr = write_binary_header(stream->next_out, obj_type, len);
	stream->total_out = hdr;
	stream->next_out += hdr;
	stream->avail_out -= hdr;
}

int hash_sha1_file(void *buf, unsigned long len, const char *type,
                   unsigned char *sha1)
{
	unsigned char hdr[50];
	int hdrlen;
	write_sha1_file_prepare(buf, len, type, sha1, hdr, &hdrlen);
	return 0;
}

int write_sha1_file(void *buf, unsigned long len, const char *type, unsigned char *returnsha1)
{
	int size;
	unsigned char *compressed;
	z_stream stream;
	unsigned char sha1[20];
	char *filename;
	static char tmpfile[PATH_MAX];
	unsigned char hdr[50];
	int fd, hdrlen;

	/* Normally if we have it in the pack then we do not bother writing
	 * it out into .git/objects/??/?{38} file.
	 */
	write_sha1_file_prepare(buf, len, type, sha1, hdr, &hdrlen);
	filename = sha1_file_name(sha1);
	if (returnsha1)
		hashcpy(returnsha1, sha1);
	if (has_sha1_file(sha1))
		return 0;
	fd = open(filename, O_RDONLY);
	if (fd >= 0) {
		/*
		 * FIXME!!! We might do collision checking here, but we'd
		 * need to uncompress the old file and check it. Later.
		 */
		close(fd);
		return 0;
	}

	if (errno != ENOENT) {
		return error("sha1 file %s: %s\n", filename, strerror(errno));
	}

	snprintf(tmpfile, sizeof(tmpfile), "%s/obj_XXXXXX", get_object_directory());

	fd = mkstemp(tmpfile);
	if (fd < 0) {
		if (errno == EPERM)
			return error("insufficient permission for adding an object to repository database %s\n", get_object_directory());
		else
			return error("unable to create temporary sha1 filename %s: %s\n", tmpfile, strerror(errno));
	}

	/* Set it up */
	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, zlib_compression_level);
	size = 8 + deflateBound(&stream, len+hdrlen);
	compressed = xmalloc(size);

	/* Compress it */
	stream.next_out = compressed;
	stream.avail_out = size;

	/* First header.. */
	stream.next_in = hdr;
	stream.avail_in = hdrlen;
	setup_object_header(&stream, type, len);

	/* Then the data itself.. */
	stream.next_in = buf;
	stream.avail_in = len;
	while (deflate(&stream, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&stream);
	size = stream.total_out;

	if (write_buffer(fd, compressed, size) < 0)
		die("unable to write sha1 file");
	fchmod(fd, 0444);
	close(fd);
	free(compressed);

	return move_temp_to_file(tmpfile, filename);
}

/*
 * We need to unpack and recompress the object for writing
 * it out to a different file.
 */
static void *repack_object(const unsigned char *sha1, unsigned long *objsize)
{
	size_t size;
	z_stream stream;
	unsigned char *unpacked;
	unsigned long len;
	char type[20];
	char hdr[50];
	int hdrlen;
	void *buf;

	/* need to unpack and recompress it by itself */
	unpacked = read_packed_sha1(sha1, type, &len);

	hdrlen = sprintf(hdr, "%s %lu", type, len) + 1;

	/* Set it up */
	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, zlib_compression_level);
	size = deflateBound(&stream, len + hdrlen);
	buf = xmalloc(size);

	/* Compress it */
	stream.next_out = buf;
	stream.avail_out = size;

	/* First header.. */
	stream.next_in = (void *)hdr;
	stream.avail_in = hdrlen;
	while (deflate(&stream, 0) == Z_OK)
		/* nothing */;

	/* Then the data itself.. */
	stream.next_in = unpacked;
	stream.avail_in = len;
	while (deflate(&stream, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&stream);
	free(unpacked);

	*objsize = stream.total_out;
	return buf;
}

int write_sha1_to_fd(int fd, const unsigned char *sha1)
{
	int retval;
	unsigned long objsize;
	void *buf = map_sha1_file(sha1, &objsize);

	if (buf) {
		retval = write_buffer(fd, buf, objsize);
		munmap(buf, objsize);
		return retval;
	}

	buf = repack_object(sha1, &objsize);
	retval = write_buffer(fd, buf, objsize);
	free(buf);
	return retval;
}

int write_sha1_from_fd(const unsigned char *sha1, int fd, char *buffer,
		       size_t bufsize, size_t *bufposn)
{
	char tmpfile[PATH_MAX];
	int local;
	z_stream stream;
	unsigned char real_sha1[20];
	unsigned char discard[4096];
	int ret;
	SHA_CTX c;

	snprintf(tmpfile, sizeof(tmpfile), "%s/obj_XXXXXX", get_object_directory());

	local = mkstemp(tmpfile);
	if (local < 0) {
		if (errno == EPERM)
			return error("insufficient permission for adding an object to repository database %s\n", get_object_directory());
		else
			return error("unable to create temporary sha1 filename %s: %s\n", tmpfile, strerror(errno));
	}

	memset(&stream, 0, sizeof(stream));

	inflateInit(&stream);

	SHA1_Init(&c);

	do {
		ssize_t size;
		if (*bufposn) {
			stream.avail_in = *bufposn;
			stream.next_in = (unsigned char *) buffer;
			do {
				stream.next_out = discard;
				stream.avail_out = sizeof(discard);
				ret = inflate(&stream, Z_SYNC_FLUSH);
				SHA1_Update(&c, discard, sizeof(discard) -
					    stream.avail_out);
			} while (stream.avail_in && ret == Z_OK);
			if (write_buffer(local, buffer, *bufposn - stream.avail_in) < 0)
				die("unable to write sha1 file");
			memmove(buffer, buffer + *bufposn - stream.avail_in,
				stream.avail_in);
			*bufposn = stream.avail_in;
			if (ret != Z_OK)
				break;
		}
		size = xread(fd, buffer + *bufposn, bufsize - *bufposn);
		if (size <= 0) {
			close(local);
			unlink(tmpfile);
			if (!size)
				return error("Connection closed?");
			perror("Reading from connection");
			return -1;
		}
		*bufposn += size;
	} while (1);
	inflateEnd(&stream);

	close(local);
	SHA1_Final(real_sha1, &c);
	if (ret != Z_STREAM_END) {
		unlink(tmpfile);
		return error("File %s corrupted", sha1_to_hex(sha1));
	}
	if (hashcmp(sha1, real_sha1)) {
		unlink(tmpfile);
		return error("File %s has bad hash", sha1_to_hex(sha1));
	}

	return move_temp_to_file(tmpfile, sha1_file_name(sha1));
}

int has_pack_index(const unsigned char *sha1)
{
	struct stat st;
	if (stat(sha1_pack_index_name(sha1), &st))
		return 0;
	return 1;
}

int has_pack_file(const unsigned char *sha1)
{
	struct stat st;
	if (stat(sha1_pack_name(sha1), &st))
		return 0;
	return 1;
}

int has_sha1_pack(const unsigned char *sha1, const char **ignore_packed)
{
	struct pack_entry e;
	return find_pack_entry(sha1, &e, ignore_packed);
}

int has_sha1_file(const unsigned char *sha1)
{
	struct stat st;
	struct pack_entry e;

	if (find_pack_entry(sha1, &e, NULL))
		return 1;
	return find_sha1_file(sha1, &st) ? 1 : 0;
}

/*
 * reads from fd as long as possible into a supplied buffer of size bytes.
 * If necessary the buffer's size is increased using realloc()
 *
 * returns 0 if anything went fine and -1 otherwise
 *
 * NOTE: both buf and size may change, but even when -1 is returned
 * you still have to free() it yourself.
 */
int read_pipe(int fd, char** return_buf, unsigned long* return_size)
{
	char* buf = *return_buf;
	unsigned long size = *return_size;
	int iret;
	unsigned long off = 0;

	do {
		iret = xread(fd, buf + off, size - off);
		if (iret > 0) {
			off += iret;
			if (off == size) {
				size *= 2;
				buf = xrealloc(buf, size);
			}
		}
	} while (iret > 0);

	*return_buf = buf;
	*return_size = off;

	if (iret < 0)
		return -1;
	return 0;
}

int index_pipe(unsigned char *sha1, int fd, const char *type, int write_object)
{
	unsigned long size = 4096;
	char *buf = xmalloc(size);
	int ret;

	if (read_pipe(fd, &buf, &size)) {
		free(buf);
		return -1;
	}

	if (!type)
		type = blob_type;
	if (write_object)
		ret = write_sha1_file(buf, size, type, sha1);
	else
		ret = hash_sha1_file(buf, size, type, sha1);
	free(buf);
	return ret;
}

int index_fd(unsigned char *sha1, int fd, struct stat *st, int write_object, const char *type)
{
	unsigned long size = st->st_size;
	void *buf;
	int ret;

	buf = "";
	if (size)
		buf = xmmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (!type)
		type = blob_type;
	if (write_object)
		ret = write_sha1_file(buf, size, type, sha1);
	else
		ret = hash_sha1_file(buf, size, type, sha1);
	if (size)
		munmap(buf, size);
	return ret;
}

int index_path(unsigned char *sha1, const char *path, struct stat *st, int write_object)
{
	int fd;
	char *target;

	switch (st->st_mode & S_IFMT) {
	case S_IFREG:
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return error("open(\"%s\"): %s", path,
				     strerror(errno));
		if (index_fd(sha1, fd, st, write_object, NULL) < 0)
			return error("%s: failed to insert into database",
				     path);
		break;
	case S_IFLNK:
		target = xmalloc(st->st_size+1);
		if (readlink(path, target, st->st_size+1) != st->st_size) {
			char *errstr = strerror(errno);
			free(target);
			return error("readlink(\"%s\"): %s", path,
			             errstr);
		}
		if (!write_object)
			hash_sha1_file(target, st->st_size, blob_type, sha1);
		else if (write_sha1_file(target, st->st_size, blob_type, sha1))
			return error("%s: failed to insert into database",
				     path);
		free(target);
		break;
	default:
		return error("%s: unsupported file type", path);
	}
	return 0;
}
