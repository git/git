#include "cache.h"
#include "mru.h"
#include "pack.h"
#include "dir.h"
#include "mergesort.h"
#include "packfile.h"
#include "delta.h"

char *odb_pack_name(struct strbuf *buf,
		    const unsigned char *sha1,
		    const char *ext)
{
	strbuf_reset(buf);
	strbuf_addf(buf, "%s/pack/pack-%s.%s", get_object_directory(),
		    sha1_to_hex(sha1), ext);
	return buf->buf;
}

char *sha1_pack_name(const unsigned char *sha1)
{
	static struct strbuf buf = STRBUF_INIT;
	return odb_pack_name(&buf, sha1, "pack");
}

char *sha1_pack_index_name(const unsigned char *sha1)
{
	static struct strbuf buf = STRBUF_INIT;
	return odb_pack_name(&buf, sha1, "idx");
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

static struct mru packed_git_mru_storage;
struct mru *packed_git_mru = &packed_git_mru_storage;

#define SZ_FMT PRIuMAX
static inline uintmax_t sz_fmt(size_t s) { return s; }

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

/*
 * Open and mmap the index file at path, perform a couple of
 * consistency checks, then record its information to p.  Return 0 on
 * success.
 */
static int check_packed_git_idx(const char *path, struct packed_git *p)
{
	void *idx_map;
	struct pack_idx_header *hdr;
	size_t idx_size;
	uint32_t version, nr, i, *index;
	int fd = git_open(path);
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
	size_t len;
	int ret;

	if (p->index_data)
		return 0;

	if (!strip_suffix(p->pack_name, ".pack", &len))
		die("BUG: pack_name does not end in .pack");
	idx_name = xstrfmt("%.*s.idx", (int)len, p->pack_name);
	ret = check_packed_git_idx(idx_name, p);
	free(idx_name);
	return ret;
}

static struct packed_git *alloc_packed_git(int extra)
{
	struct packed_git *p = xmalloc(st_add(sizeof(*p), extra));
	memset(p, 0, sizeof(*p));
	p->pack_fd = -1;
	return p;
}

struct packed_git *parse_pack_index(unsigned char *sha1, const char *idx_path)
{
	const char *path = sha1_pack_name(sha1);
	size_t alloc = st_add(strlen(path), 1);
	struct packed_git *p = alloc_packed_git(alloc);

	memcpy(p->pack_name, path, alloc); /* includes NUL */
	hashcpy(p->sha1, sha1);
	if (check_packed_git_idx(idx_path, p)) {
		free(p);
		return NULL;
	}

	return p;
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
		else
			lru_p->windows = lru_w->next;
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

static int close_pack_fd(struct packed_git *p)
{
	if (p->pack_fd < 0)
		return 0;

	close(p->pack_fd);
	pack_open_fds--;
	p->pack_fd = -1;

	return 1;
}

void close_pack_index(struct packed_git *p)
{
	if (p->index_data) {
		munmap((void *)p->index_data, p->index_size);
		p->index_data = NULL;
	}
}

static void close_pack(struct packed_git *p)
{
	close_pack_windows(p);
	close_pack_fd(p);
	close_pack_index(p);
}

void close_all_packs(void)
{
	struct packed_git *p;

	for (p = packed_git; p; p = p->next)
		if (p->do_not_close)
			die("BUG: want to close pack marked 'do-not-close'");
		else
			close_pack(p);
}

/*
 * The LRU pack is the one with the oldest MRU window, preferring packs
 * with no used windows, or the oldest mtime if it has no windows allocated.
 */
static void find_lru_pack(struct packed_git *p, struct packed_git **lru_p, struct pack_window **mru_w, int *accept_windows_inuse)
{
	struct pack_window *w, *this_mru_w;
	int has_windows_inuse = 0;

	/*
	 * Reject this pack if it has windows and the previously selected
	 * one does not.  If this pack does not have windows, reject
	 * it if the pack file is newer than the previously selected one.
	 */
	if (*lru_p && !*mru_w && (p->windows || p->mtime > (*lru_p)->mtime))
		return;

	for (w = this_mru_w = p->windows; w; w = w->next) {
		/*
		 * Reject this pack if any of its windows are in use,
		 * but the previously selected pack did not have any
		 * inuse windows.  Otherwise, record that this pack
		 * has windows in use.
		 */
		if (w->inuse_cnt) {
			if (*accept_windows_inuse)
				has_windows_inuse = 1;
			else
				return;
		}

		if (w->last_used > this_mru_w->last_used)
			this_mru_w = w;

		/*
		 * Reject this pack if it has windows that have been
		 * used more recently than the previously selected pack.
		 * If the previously selected pack had windows inuse and
		 * we have not encountered a window in this pack that is
		 * inuse, skip this check since we prefer a pack with no
		 * inuse windows to one that has inuse windows.
		 */
		if (*mru_w && *accept_windows_inuse == has_windows_inuse &&
		    this_mru_w->last_used > (*mru_w)->last_used)
			return;
	}

	/*
	 * Select this pack.
	 */
	*mru_w = this_mru_w;
	*lru_p = p;
	*accept_windows_inuse = has_windows_inuse;
}

static int close_one_pack(void)
{
	struct packed_git *p, *lru_p = NULL;
	struct pack_window *mru_w = NULL;
	int accept_windows_inuse = 1;

	for (p = packed_git; p; p = p->next) {
		if (p->pack_fd == -1)
			continue;
		find_lru_pack(p, &lru_p, &mru_w, &accept_windows_inuse);
	}

	if (lru_p)
		return close_pack_fd(lru_p);

	return 0;
}

static unsigned int get_max_fd_limit(void)
{
#ifdef RLIMIT_NOFILE
	{
		struct rlimit lim;

		if (!getrlimit(RLIMIT_NOFILE, &lim))
			return lim.rlim_cur;
	}
#endif

#ifdef _SC_OPEN_MAX
	{
		long open_max = sysconf(_SC_OPEN_MAX);
		if (0 < open_max)
			return open_max;
		/*
		 * Otherwise, we got -1 for one of the two
		 * reasons:
		 *
		 * (1) sysconf() did not understand _SC_OPEN_MAX
		 *     and signaled an error with -1; or
		 * (2) sysconf() said there is no limit.
		 *
		 * We _could_ clear errno before calling sysconf() to
		 * tell these two cases apart and return a huge number
		 * in the latter case to let the caller cap it to a
		 * value that is not so selfish, but letting the
		 * fallback OPEN_MAX codepath take care of these cases
		 * is a lot simpler.
		 */
	}
#endif

#ifdef OPEN_MAX
	return OPEN_MAX;
#else
	return 1; /* see the caller ;-) */
#endif
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
		unsigned int max_fds = get_max_fd_limit();

		/* Save 3 for stdin/stdout/stderr, 22 for work */
		if (25 < max_fds)
			pack_max_fds = max_fds - 25;
		else
			pack_max_fds = 1;
	}

	while (pack_max_fds <= pack_open_fds && close_one_pack())
		; /* nothing */

	p->pack_fd = git_open(p->pack_name);
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

int open_packed_git(struct packed_git *p)
{
	if (!open_packed_git_1(p))
		return 0;
	close_pack_fd(p);
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
	if (offset < 0)
		die(_("offset before end of packfile (broken .idx?)"));

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
				&& unuse_one_window(p))
				; /* nothing */
			win->base = xmmap(NULL, win->len,
				PROT_READ, MAP_PRIVATE,
				p->pack_fd, win->offset);
			if (win->base == MAP_FAILED)
				die_errno("packfile %s cannot be mapped",
					  p->pack_name);
			if (!win->offset && win->len == p->pack_size
				&& !p->do_not_close)
				close_pack_fd(p);
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

void unuse_pack(struct pack_window **w_cursor)
{
	struct pack_window *w = *w_cursor;
	if (w) {
		w->inuse_cnt--;
		*w_cursor = NULL;
	}
}

static void try_to_free_pack_memory(size_t size)
{
	release_pack_memory(size);
}

struct packed_git *add_packed_git(const char *path, size_t path_len, int local)
{
	static int have_set_try_to_free_routine;
	struct stat st;
	size_t alloc;
	struct packed_git *p;

	if (!have_set_try_to_free_routine) {
		have_set_try_to_free_routine = 1;
		set_try_to_free_routine(try_to_free_pack_memory);
	}

	/*
	 * Make sure a corresponding .pack file exists and that
	 * the index looks sane.
	 */
	if (!strip_suffix_mem(path, &path_len, ".idx"))
		return NULL;

	/*
	 * ".pack" is long enough to hold any suffix we're adding (and
	 * the use xsnprintf double-checks that)
	 */
	alloc = st_add3(path_len, strlen(".pack"), 1);
	p = alloc_packed_git(alloc);
	memcpy(p->pack_name, path, path_len);

	xsnprintf(p->pack_name + path_len, alloc - path_len, ".keep");
	if (!access(p->pack_name, F_OK))
		p->pack_keep = 1;

	xsnprintf(p->pack_name + path_len, alloc - path_len, ".pack");
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

void install_packed_git(struct packed_git *pack)
{
	if (pack->pack_fd != -1)
		pack_open_fds++;

	pack->next = packed_git;
	packed_git = pack;
}

void (*report_garbage)(unsigned seen_bits, const char *path);

static void report_helper(const struct string_list *list,
			  int seen_bits, int first, int last)
{
	if (seen_bits == (PACKDIR_FILE_PACK|PACKDIR_FILE_IDX))
		return;

	for (; first < last; first++)
		report_garbage(seen_bits, list->items[first].string);
}

static void report_pack_garbage(struct string_list *list)
{
	int i, baselen = -1, first = 0, seen_bits = 0;

	if (!report_garbage)
		return;

	string_list_sort(list);

	for (i = 0; i < list->nr; i++) {
		const char *path = list->items[i].string;
		if (baselen != -1 &&
		    strncmp(path, list->items[first].string, baselen)) {
			report_helper(list, seen_bits, first, i);
			baselen = -1;
			seen_bits = 0;
		}
		if (baselen == -1) {
			const char *dot = strrchr(path, '.');
			if (!dot) {
				report_garbage(PACKDIR_FILE_GARBAGE, path);
				continue;
			}
			baselen = dot - path + 1;
			first = i;
		}
		if (!strcmp(path + baselen, "pack"))
			seen_bits |= 1;
		else if (!strcmp(path + baselen, "idx"))
			seen_bits |= 2;
	}
	report_helper(list, seen_bits, first, list->nr);
}

static void prepare_packed_git_one(char *objdir, int local)
{
	struct strbuf path = STRBUF_INIT;
	size_t dirnamelen;
	DIR *dir;
	struct dirent *de;
	struct string_list garbage = STRING_LIST_INIT_DUP;

	strbuf_addstr(&path, objdir);
	strbuf_addstr(&path, "/pack");
	dir = opendir(path.buf);
	if (!dir) {
		if (errno != ENOENT)
			error_errno("unable to open object pack directory: %s",
				    path.buf);
		strbuf_release(&path);
		return;
	}
	strbuf_addch(&path, '/');
	dirnamelen = path.len;
	while ((de = readdir(dir)) != NULL) {
		struct packed_git *p;
		size_t base_len;

		if (is_dot_or_dotdot(de->d_name))
			continue;

		strbuf_setlen(&path, dirnamelen);
		strbuf_addstr(&path, de->d_name);

		base_len = path.len;
		if (strip_suffix_mem(path.buf, &base_len, ".idx")) {
			/* Don't reopen a pack we already have. */
			for (p = packed_git; p; p = p->next) {
				size_t len;
				if (strip_suffix(p->pack_name, ".pack", &len) &&
				    len == base_len &&
				    !memcmp(p->pack_name, path.buf, len))
					break;
			}
			if (p == NULL &&
			    /*
			     * See if it really is a valid .idx file with
			     * corresponding .pack file that we can map.
			     */
			    (p = add_packed_git(path.buf, path.len, local)) != NULL)
				install_packed_git(p);
		}

		if (!report_garbage)
			continue;

		if (ends_with(de->d_name, ".idx") ||
		    ends_with(de->d_name, ".pack") ||
		    ends_with(de->d_name, ".bitmap") ||
		    ends_with(de->d_name, ".keep"))
			string_list_append(&garbage, path.buf);
		else
			report_garbage(PACKDIR_FILE_GARBAGE, path.buf);
	}
	closedir(dir);
	report_pack_garbage(&garbage);
	string_list_clear(&garbage, 0);
	strbuf_release(&path);
}

static int approximate_object_count_valid;

/*
 * Give a fast, rough count of the number of objects in the repository. This
 * ignores loose objects completely. If you have a lot of them, then either
 * you should repack because your performance will be awful, or they are
 * all unreachable objects about to be pruned, in which case they're not really
 * interesting as a measure of repo size in the first place.
 */
unsigned long approximate_object_count(void)
{
	static unsigned long count;
	if (!approximate_object_count_valid) {
		struct packed_git *p;

		prepare_packed_git();
		count = 0;
		for (p = packed_git; p; p = p->next) {
			if (open_pack_index(p))
				continue;
			count += p->num_objects;
		}
	}
	return count;
}

static void *get_next_packed_git(const void *p)
{
	return ((const struct packed_git *)p)->next;
}

static void set_next_packed_git(void *p, void *next)
{
	((struct packed_git *)p)->next = next;
}

static int sort_pack(const void *a_, const void *b_)
{
	const struct packed_git *a = a_;
	const struct packed_git *b = b_;
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
	packed_git = llist_mergesort(packed_git, get_next_packed_git,
				     set_next_packed_git, sort_pack);
}

static void prepare_packed_git_mru(void)
{
	struct packed_git *p;

	mru_clear(packed_git_mru);
	for (p = packed_git; p; p = p->next)
		mru_append(packed_git_mru, p);
}

static int prepare_packed_git_run_once = 0;
void prepare_packed_git(void)
{
	struct alternate_object_database *alt;

	if (prepare_packed_git_run_once)
		return;
	prepare_packed_git_one(get_object_directory(), 1);
	prepare_alt_odb();
	for (alt = alt_odb_list; alt; alt = alt->next)
		prepare_packed_git_one(alt->path, 0);
	rearrange_packed_git();
	prepare_packed_git_mru();
	prepare_packed_git_run_once = 1;
}

void reprepare_packed_git(void)
{
	approximate_object_count_valid = 0;
	prepare_packed_git_run_once = 0;
	prepare_packed_git();
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
			size = used = 0;
			break;
		}
		c = buf[used++];
		size += (c & 0x7f) << shift;
		shift += 7;
	}
	*sizep = size;
	return used;
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
