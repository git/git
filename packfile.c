#include "cache.h"
#include "alloc.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "list.h"
#include "pack.h"
#include "repository.h"
#include "dir.h"
#include "mergesort.h"
#include "packfile.h"
#include "delta.h"
#include "streaming.h"
#include "hash-lookup.h"
#include "commit.h"
#include "object.h"
#include "tag.h"
#include "tree-walk.h"
#include "tree.h"
#include "object-store.h"
#include "midx.h"
#include "commit-graph.h"
#include "promisor-remote.h"
#include "wrapper.h"

char *odb_pack_name(struct strbuf *buf,
		    const unsigned char *hash,
		    const char *ext)
{
	strbuf_reset(buf);
	strbuf_addf(buf, "%s/pack/pack-%s.%s", get_object_directory(),
		    hash_to_hex(hash), ext);
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
	size_t idx_size;
	int fd = git_open(path), ret;
	struct stat st;
	const unsigned int hashsz = the_hash_algo->rawsz;

	if (fd < 0)
		return -1;
	if (fstat(fd, &st)) {
		close(fd);
		return -1;
	}
	idx_size = xsize_t(st.st_size);
	if (idx_size < 4 * 256 + hashsz + hashsz) {
		close(fd);
		return error("index file %s is too small", path);
	}
	idx_map = xmmap(NULL, idx_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	ret = load_idx(path, hashsz, idx_map, idx_size, p);

	if (ret)
		munmap(idx_map, idx_size);

	return ret;
}

int load_idx(const char *path, const unsigned int hashsz, void *idx_map,
	     size_t idx_size, struct packed_git *p)
{
	struct pack_idx_header *hdr = idx_map;
	uint32_t version, nr, i, *index;

	if (idx_size < 4 * 256 + hashsz + hashsz)
		return error("index file %s is too small", path);
	if (!idx_map)
		return error("empty data");

	if (hdr->idx_signature == htonl(PACK_IDX_SIGNATURE)) {
		version = ntohl(hdr->idx_version);
		if (version < 2 || version > 2)
			return error("index file %s is version %"PRIu32
				     " and is not supported by this binary"
				     " (try upgrading GIT to a newer version)",
				     path, version);
	} else
		version = 1;

	nr = 0;
	index = idx_map;
	if (version > 1)
		index += 2;  /* skip index header */
	for (i = 0; i < 256; i++) {
		uint32_t n = ntohl(index[i]);
		if (n < nr)
			return error("non-monotonic index %s", path);
		nr = n;
	}

	if (version == 1) {
		/*
		 * Total size:
		 *  - 256 index entries 4 bytes each
		 *  - 24-byte entries * nr (object ID + 4-byte offset)
		 *  - hash of the packfile
		 *  - file checksum
		 */
		if (idx_size != st_add(4 * 256 + hashsz + hashsz, st_mult(nr, hashsz + 4)))
			return error("wrong index v1 file size in %s", path);
	} else if (version == 2) {
		/*
		 * Minimum size:
		 *  - 8 bytes of header
		 *  - 256 index entries 4 bytes each
		 *  - object ID entry * nr
		 *  - 4-byte crc entry * nr
		 *  - 4-byte offset entry * nr
		 *  - hash of the packfile
		 *  - file checksum
		 * And after the 4-byte offset table might be a
		 * variable sized table containing 8-byte entries
		 * for offsets larger than 2^31.
		 */
		size_t min_size = st_add(8 + 4*256 + hashsz + hashsz, st_mult(nr, hashsz + 4 + 4));
		size_t max_size = min_size;
		if (nr)
			max_size = st_add(max_size, st_mult(nr - 1, 8));
		if (idx_size < min_size || idx_size > max_size)
			return error("wrong index v2 file size in %s", path);
		if (idx_size != min_size &&
		    /*
		     * make sure we can deal with large pack offsets.
		     * 31-bit signed offset won't be enough, neither
		     * 32-bit unsigned one will be.
		     */
		    (sizeof(off_t) <= 4))
			return error("pack too large for current definition of off_t in %s", path);
		p->crc_offset = 8 + 4 * 256 + nr * hashsz;
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
		BUG("pack_name does not end in .pack");
	idx_name = xstrfmt("%.*s.idx", (int)len, p->pack_name);
	ret = check_packed_git_idx(idx_name, p);
	free(idx_name);
	return ret;
}

uint32_t get_pack_fanout(struct packed_git *p, uint32_t value)
{
	const uint32_t *level1_ofs = p->index_data;

	if (!level1_ofs) {
		if (open_pack_index(p))
			return 0;
		level1_ofs = p->index_data;
	}

	if (p->index_version > 1) {
		level1_ofs += 2;
	}

	return ntohl(level1_ofs[value]);
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
	hashcpy(p->hash, sha1);
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
	for (p = the_repository->objects->packed_git; p; p = p->next)
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

int close_pack_fd(struct packed_git *p)
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

static void close_pack_revindex(struct packed_git *p)
{
	if (!p->revindex_map)
		return;

	munmap((void *)p->revindex_map, p->revindex_size);
	p->revindex_map = NULL;
	p->revindex_data = NULL;
}

static void close_pack_mtimes(struct packed_git *p)
{
	if (!p->mtimes_map)
		return;

	munmap((void *)p->mtimes_map, p->mtimes_size);
	p->mtimes_map = NULL;
}

void close_pack(struct packed_git *p)
{
	close_pack_windows(p);
	close_pack_fd(p);
	close_pack_index(p);
	close_pack_revindex(p);
	close_pack_mtimes(p);
	oidset_clear(&p->bad_objects);
}

void close_object_store(struct raw_object_store *o)
{
	struct packed_git *p;

	for (p = o->packed_git; p; p = p->next)
		if (p->do_not_close)
			BUG("want to close pack marked 'do-not-close'");
		else
			close_pack(p);

	if (o->multi_pack_index) {
		close_midx(o->multi_pack_index);
		o->multi_pack_index = NULL;
	}

	close_commit_graph(o);
}

void unlink_pack_path(const char *pack_name, int force_delete)
{
	static const char *exts[] = {".pack", ".idx", ".rev", ".keep", ".bitmap", ".promisor", ".mtimes"};
	int i;
	struct strbuf buf = STRBUF_INIT;
	size_t plen;

	strbuf_addstr(&buf, pack_name);
	strip_suffix_mem(buf.buf, &buf.len, ".pack");
	plen = buf.len;

	if (!force_delete) {
		strbuf_addstr(&buf, ".keep");
		if (!access(buf.buf, F_OK)) {
			strbuf_release(&buf);
			return;
		}
	}

	for (i = 0; i < ARRAY_SIZE(exts); i++) {
		strbuf_setlen(&buf, plen);
		strbuf_addstr(&buf, exts[i]);
		unlink(buf.buf);
	}

	strbuf_release(&buf);
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

	for (p = the_repository->objects->packed_git; p; p = p->next) {
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

const char *pack_basename(struct packed_git *p)
{
	const char *ret = strrchr(p->pack_name, '/');
	if (ret)
		ret = ret + 1; /* skip past slash */
	else
		ret = p->pack_name; /* we only have a base */
	return ret;
}

/*
 * Do not call this directly as this leaks p->pack_fd on error return;
 * call open_packed_git() instead.
 */
static int open_packed_git_1(struct packed_git *p)
{
	struct stat st;
	struct pack_header hdr;
	unsigned char hash[GIT_MAX_RAWSZ];
	unsigned char *idx_hash;
	ssize_t read_result;
	const unsigned hashsz = the_hash_algo->rawsz;

	if (open_pack_index(p))
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

	/* Verify we recognize this pack file format. */
	read_result = read_in_full(p->pack_fd, &hdr, sizeof(hdr));
	if (read_result < 0)
		return error_errno("error reading from %s", p->pack_name);
	if (read_result != sizeof(hdr))
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
	read_result = pread_in_full(p->pack_fd, hash, hashsz,
					p->pack_size - hashsz);
	if (read_result < 0)
		return error_errno("error reading from %s", p->pack_name);
	if (read_result != hashsz)
		return error("packfile %s signature is unavailable", p->pack_name);
	idx_hash = ((unsigned char *)p->index_data) + p->index_size - hashsz * 2;
	if (!hasheq(hash, idx_hash))
		return error("packfile %s does not match index", p->pack_name);
	return 0;
}

static int open_packed_git(struct packed_git *p)
{
	if (!open_packed_git_1(p))
		return 0;
	close_pack_fd(p);
	return -1;
}

static int in_window(struct pack_window *win, off_t offset)
{
	/* We must promise at least one full hash after the
	 * offset is available from this window, otherwise the offset
	 * is not actually in this window and a different window (which
	 * has that one hash excess) must be used.  This is to support
	 * the object header and delta base parsing routines below.
	 */
	off_t win_off = win->offset;
	return win_off <= offset
		&& (offset + the_hash_algo->rawsz) <= (win_off + win->len);
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
	if (offset > (p->pack_size - the_hash_algo->rawsz))
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

			CALLOC_ARRAY(win, 1);
			win->offset = (offset / window_align) * window_align;
			len = p->pack_size - win->offset;
			if (len > packed_git_window_size)
				len = packed_git_window_size;
			win->len = (size_t)len;
			pack_mapped += win->len;
			while (packed_git_limit < pack_mapped
				&& unuse_one_window(p))
				; /* nothing */
			win->base = xmmap_gently(NULL, win->len,
				PROT_READ, MAP_PRIVATE,
				p->pack_fd, win->offset);
			if (win->base == MAP_FAILED)
				die_errno(_("packfile %s cannot be mapped%s"),
					  p->pack_name, mmap_os_err());
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

struct packed_git *add_packed_git(const char *path, size_t path_len, int local)
{
	struct stat st;
	size_t alloc;
	struct packed_git *p;

	/*
	 * Make sure a corresponding .pack file exists and that
	 * the index looks sane.
	 */
	if (!strip_suffix_mem(path, &path_len, ".idx"))
		return NULL;

	/*
	 * ".promisor" is long enough to hold any suffix we're adding (and
	 * the use xsnprintf double-checks that)
	 */
	alloc = st_add3(path_len, strlen(".promisor"), 1);
	p = alloc_packed_git(alloc);
	memcpy(p->pack_name, path, path_len);

	xsnprintf(p->pack_name + path_len, alloc - path_len, ".keep");
	if (!access(p->pack_name, F_OK))
		p->pack_keep = 1;

	xsnprintf(p->pack_name + path_len, alloc - path_len, ".promisor");
	if (!access(p->pack_name, F_OK))
		p->pack_promisor = 1;

	xsnprintf(p->pack_name + path_len, alloc - path_len, ".mtimes");
	if (!access(p->pack_name, F_OK))
		p->is_cruft = 1;

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
	if (path_len < the_hash_algo->hexsz ||
	    get_sha1_hex(path + path_len - the_hash_algo->hexsz, p->hash))
		hashclr(p->hash);
	return p;
}

void install_packed_git(struct repository *r, struct packed_git *pack)
{
	if (pack->pack_fd != -1)
		pack_open_fds++;

	pack->next = r->objects->packed_git;
	r->objects->packed_git = pack;

	hashmap_entry_init(&pack->packmap_ent, strhash(pack->pack_name));
	hashmap_add(&r->objects->pack_map, &pack->packmap_ent);
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

void for_each_file_in_pack_dir(const char *objdir,
			       each_file_in_pack_dir_fn fn,
			       void *data)
{
	struct strbuf path = STRBUF_INIT;
	size_t dirnamelen;
	DIR *dir;
	struct dirent *de;

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
	while ((de = readdir_skip_dot_and_dotdot(dir)) != NULL) {
		strbuf_setlen(&path, dirnamelen);
		strbuf_addstr(&path, de->d_name);

		fn(path.buf, path.len, de->d_name, data);
	}

	closedir(dir);
	strbuf_release(&path);
}

struct prepare_pack_data {
	struct repository *r;
	struct string_list *garbage;
	int local;
	struct multi_pack_index *m;
};

static void prepare_pack(const char *full_name, size_t full_name_len,
			 const char *file_name, void *_data)
{
	struct prepare_pack_data *data = (struct prepare_pack_data *)_data;
	struct packed_git *p;
	size_t base_len = full_name_len;

	if (strip_suffix_mem(full_name, &base_len, ".idx") &&
	    !(data->m && midx_contains_pack(data->m, file_name))) {
		struct hashmap_entry hent;
		char *pack_name = xstrfmt("%.*s.pack", (int)base_len, full_name);
		unsigned int hash = strhash(pack_name);
		hashmap_entry_init(&hent, hash);

		/* Don't reopen a pack we already have. */
		if (!hashmap_get(&data->r->objects->pack_map, &hent, pack_name)) {
			p = add_packed_git(full_name, full_name_len, data->local);
			if (p)
				install_packed_git(data->r, p);
		}
		free(pack_name);
	}

	if (!report_garbage)
		return;

	if (!strcmp(file_name, "multi-pack-index"))
		return;
	if (starts_with(file_name, "multi-pack-index") &&
	    (ends_with(file_name, ".bitmap") || ends_with(file_name, ".rev")))
		return;
	if (ends_with(file_name, ".idx") ||
	    ends_with(file_name, ".rev") ||
	    ends_with(file_name, ".pack") ||
	    ends_with(file_name, ".bitmap") ||
	    ends_with(file_name, ".keep") ||
	    ends_with(file_name, ".promisor") ||
	    ends_with(file_name, ".mtimes"))
		string_list_append(data->garbage, full_name);
	else
		report_garbage(PACKDIR_FILE_GARBAGE, full_name);
}

static void prepare_packed_git_one(struct repository *r, char *objdir, int local)
{
	struct prepare_pack_data data;
	struct string_list garbage = STRING_LIST_INIT_DUP;

	data.m = r->objects->multi_pack_index;

	/* look for the multi-pack-index for this object directory */
	while (data.m && strcmp(data.m->object_dir, objdir))
		data.m = data.m->next;

	data.r = r;
	data.garbage = &garbage;
	data.local = local;

	for_each_file_in_pack_dir(objdir, prepare_pack, &data);

	report_pack_garbage(data.garbage);
	string_list_clear(data.garbage, 0);
}

static void prepare_packed_git(struct repository *r);
/*
 * Give a fast, rough count of the number of objects in the repository. This
 * ignores loose objects completely. If you have a lot of them, then either
 * you should repack because your performance will be awful, or they are
 * all unreachable objects about to be pruned, in which case they're not really
 * interesting as a measure of repo size in the first place.
 */
unsigned long repo_approximate_object_count(struct repository *r)
{
	if (!r->objects->approximate_object_count_valid) {
		unsigned long count;
		struct multi_pack_index *m;
		struct packed_git *p;

		prepare_packed_git(r);
		count = 0;
		for (m = get_multi_pack_index(r); m; m = m->next)
			count += m->num_objects;
		for (p = r->objects->packed_git; p; p = p->next) {
			if (open_pack_index(p))
				continue;
			count += p->num_objects;
		}
		r->objects->approximate_object_count = count;
		r->objects->approximate_object_count_valid = 1;
	}
	return r->objects->approximate_object_count;
}

DEFINE_LIST_SORT(static, sort_packs, struct packed_git, next);

static int sort_pack(const struct packed_git *a, const struct packed_git *b)
{
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

static void rearrange_packed_git(struct repository *r)
{
	sort_packs(&r->objects->packed_git, sort_pack);
}

static void prepare_packed_git_mru(struct repository *r)
{
	struct packed_git *p;

	INIT_LIST_HEAD(&r->objects->packed_git_mru);

	for (p = r->objects->packed_git; p; p = p->next)
		list_add_tail(&p->mru, &r->objects->packed_git_mru);
}

static void prepare_packed_git(struct repository *r)
{
	struct object_directory *odb;

	if (r->objects->packed_git_initialized)
		return;

	prepare_alt_odb(r);
	for (odb = r->objects->odb; odb; odb = odb->next) {
		int local = (odb == r->objects->odb);
		prepare_multi_pack_index_one(r, odb->path, local);
		prepare_packed_git_one(r, odb->path, local);
	}
	rearrange_packed_git(r);

	prepare_packed_git_mru(r);
	r->objects->packed_git_initialized = 1;
}

void reprepare_packed_git(struct repository *r)
{
	struct object_directory *odb;

	obj_read_lock();

	/*
	 * Reprepare alt odbs, in case the alternates file was modified
	 * during the course of this process. This only _adds_ odbs to
	 * the linked list, so existing odbs will continue to exist for
	 * the lifetime of the process.
	 */
	r->objects->loaded_alternates = 0;
	prepare_alt_odb(r);

	for (odb = r->objects->odb; odb; odb = odb->next)
		odb_clear_loose_cache(odb);

	r->objects->approximate_object_count_valid = 0;
	r->objects->packed_git_initialized = 0;
	prepare_packed_git(r);
	obj_read_unlock();
}

struct packed_git *get_packed_git(struct repository *r)
{
	prepare_packed_git(r);
	return r->objects->packed_git;
}

struct multi_pack_index *get_multi_pack_index(struct repository *r)
{
	prepare_packed_git(r);
	return r->objects->multi_pack_index;
}

struct multi_pack_index *get_local_multi_pack_index(struct repository *r)
{
	struct multi_pack_index *m = get_multi_pack_index(r);

	/* no need to iterate; we always put the local one first (if any) */
	if (m && m->local)
		return m;

	return NULL;
}

struct packed_git *get_all_packs(struct repository *r)
{
	struct multi_pack_index *m;

	prepare_packed_git(r);
	for (m = r->objects->multi_pack_index; m; m = m->next) {
		uint32_t i;
		for (i = 0; i < m->num_packs; i++)
			prepare_midx_pack(r, m, i);
	}

	return r->objects->packed_git;
}

struct list_head *get_packed_git_mru(struct repository *r)
{
	prepare_packed_git(r);
	return &r->objects->packed_git_mru;
}

unsigned long unpack_object_header_buffer(const unsigned char *buf,
		unsigned long len, enum object_type *type, unsigned long *sizep)
{
	unsigned shift;
	size_t size, c;
	unsigned long used = 0;

	c = buf[used++];
	*type = (c >> 4) & 7;
	size = c & 15;
	shift = 4;
	while (c & 0x80) {
		if (len <= used || (bitsizeof(long) - 7) < shift) {
			error("bad object header");
			size = used = 0;
			break;
		}
		c = buf[used++];
		size = st_add(size, st_left_shift(c & 0x7f, shift));
		shift += 7;
	}
	*sizep = cast_size_t_to_ulong(size);
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
		/*
		 * Note: the window section returned by use_pack() must be
		 * available throughout git_inflate()'s unlocked execution. To
		 * ensure no other thread will modify the window in the
		 * meantime, we rely on the packed_window.inuse_cnt. This
		 * counter is incremented before window reading and checked
		 * before window disposal.
		 *
		 * Other worrying sections could be the call to close_pack_fd(),
		 * which can close packs even with in-use windows, and to
		 * reprepare_packed_git(). Regarding the former, mmap doc says:
		 * "closing the file descriptor does not unmap the region". And
		 * for the latter, it won't re-open already available packs.
		 */
		obj_read_unlock();
		st = git_inflate(&stream, Z_FINISH);
		obj_read_lock();
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

void mark_bad_packed_object(struct packed_git *p, const struct object_id *oid)
{
	oidset_insert(&p->bad_objects, oid);
}

const struct packed_git *has_packed_and_bad(struct repository *r,
					    const struct object_id *oid)
{
	struct packed_git *p;

	for (p = r->objects->packed_git; p; p = p->next)
		if (oidset_contains(&p->bad_objects, oid))
			return p;
	return NULL;
}

off_t get_delta_base(struct packed_git *p,
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
		*curpos += the_hash_algo->rawsz;
	} else
		die("I am totally screwed");
	return base_offset;
}

/*
 * Like get_delta_base above, but we return the sha1 instead of the pack
 * offset. This means it is cheaper for REF deltas (we do not have to do
 * the final object lookup), but more expensive for OFS deltas (we
 * have to load the revidx to convert the offset back into a sha1).
 */
static int get_delta_base_oid(struct packed_git *p,
			      struct pack_window **w_curs,
			      off_t curpos,
			      struct object_id *oid,
			      enum object_type type,
			      off_t delta_obj_offset)
{
	if (type == OBJ_REF_DELTA) {
		unsigned char *base = use_pack(p, w_curs, curpos, NULL);
		oidread(oid, base);
		return 0;
	} else if (type == OBJ_OFS_DELTA) {
		uint32_t base_pos;
		off_t base_offset = get_delta_base(p, w_curs, &curpos,
						   type, delta_obj_offset);

		if (!base_offset)
			return -1;

		if (offset_to_pack_pos(p, base_offset, &base_pos) < 0)
			return -1;

		return nth_packed_object_id(oid, p,
					    pack_pos_to_index(p, base_pos));
	} else
		return -1;
}

static int retry_bad_packed_offset(struct repository *r,
				   struct packed_git *p,
				   off_t obj_offset)
{
	int type;
	uint32_t pos;
	struct object_id oid;
	if (offset_to_pack_pos(p, obj_offset, &pos) < 0)
		return OBJ_BAD;
	nth_packed_object_id(&oid, p, pack_pos_to_index(p, pos));
	mark_bad_packed_object(p, &oid);
	type = oid_object_info(r, &oid, NULL);
	if (type <= OBJ_NONE)
		return OBJ_BAD;
	return type;
}

#define POI_STACK_PREALLOC 64

static enum object_type packed_to_object_type(struct repository *r,
					      struct packed_git *p,
					      off_t obj_offset,
					      enum object_type type,
					      struct pack_window **w_curs,
					      off_t curpos)
{
	off_t small_poi_stack[POI_STACK_PREALLOC];
	off_t *poi_stack = small_poi_stack;
	int poi_stack_nr = 0, poi_stack_alloc = POI_STACK_PREALLOC;

	while (type == OBJ_OFS_DELTA || type == OBJ_REF_DELTA) {
		off_t base_offset;
		unsigned long size;
		/* Push the object we're going to leave behind */
		if (poi_stack_nr >= poi_stack_alloc && poi_stack == small_poi_stack) {
			poi_stack_alloc = alloc_nr(poi_stack_nr);
			ALLOC_ARRAY(poi_stack, poi_stack_alloc);
			COPY_ARRAY(poi_stack, small_poi_stack, poi_stack_nr);
		} else {
			ALLOC_GROW(poi_stack, poi_stack_nr+1, poi_stack_alloc);
		}
		poi_stack[poi_stack_nr++] = obj_offset;
		/* If parsing the base offset fails, just unwind */
		base_offset = get_delta_base(p, w_curs, &curpos, type, obj_offset);
		if (!base_offset)
			goto unwind;
		curpos = obj_offset = base_offset;
		type = unpack_object_header(p, w_curs, &curpos, &size);
		if (type <= OBJ_NONE) {
			/* If getting the base itself fails, we first
			 * retry the base, otherwise unwind */
			type = retry_bad_packed_offset(r, p, base_offset);
			if (type > OBJ_NONE)
				goto out;
			goto unwind;
		}
	}

	switch (type) {
	case OBJ_BAD:
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		break;
	default:
		error("unknown object type %i at offset %"PRIuMAX" in %s",
		      type, (uintmax_t)obj_offset, p->pack_name);
		type = OBJ_BAD;
	}

out:
	if (poi_stack != small_poi_stack)
		free(poi_stack);
	return type;

unwind:
	while (poi_stack_nr) {
		obj_offset = poi_stack[--poi_stack_nr];
		type = retry_bad_packed_offset(r, p, obj_offset);
		if (type > OBJ_NONE)
			goto out;
	}
	type = OBJ_BAD;
	goto out;
}

static struct hashmap delta_base_cache;
static size_t delta_base_cached;

static LIST_HEAD(delta_base_cache_lru);

struct delta_base_cache_key {
	struct packed_git *p;
	off_t base_offset;
};

struct delta_base_cache_entry {
	struct hashmap_entry ent;
	struct delta_base_cache_key key;
	struct list_head lru;
	void *data;
	unsigned long size;
	enum object_type type;
};

static unsigned int pack_entry_hash(struct packed_git *p, off_t base_offset)
{
	unsigned int hash;

	hash = (unsigned int)(intptr_t)p + (unsigned int)base_offset;
	hash += (hash >> 8) + (hash >> 16);
	return hash;
}

static struct delta_base_cache_entry *
get_delta_base_cache_entry(struct packed_git *p, off_t base_offset)
{
	struct hashmap_entry entry, *e;
	struct delta_base_cache_key key;

	if (!delta_base_cache.cmpfn)
		return NULL;

	hashmap_entry_init(&entry, pack_entry_hash(p, base_offset));
	key.p = p;
	key.base_offset = base_offset;
	e = hashmap_get(&delta_base_cache, &entry, &key);
	return e ? container_of(e, struct delta_base_cache_entry, ent) : NULL;
}

static int delta_base_cache_key_eq(const struct delta_base_cache_key *a,
				   const struct delta_base_cache_key *b)
{
	return a->p == b->p && a->base_offset == b->base_offset;
}

static int delta_base_cache_hash_cmp(const void *cmp_data UNUSED,
				     const struct hashmap_entry *va,
				     const struct hashmap_entry *vb,
				     const void *vkey)
{
	const struct delta_base_cache_entry *a, *b;
	const struct delta_base_cache_key *key = vkey;

	a = container_of(va, const struct delta_base_cache_entry, ent);
	b = container_of(vb, const struct delta_base_cache_entry, ent);

	if (key)
		return !delta_base_cache_key_eq(&a->key, key);
	else
		return !delta_base_cache_key_eq(&a->key, &b->key);
}

static int in_delta_base_cache(struct packed_git *p, off_t base_offset)
{
	return !!get_delta_base_cache_entry(p, base_offset);
}

/*
 * Remove the entry from the cache, but do _not_ free the associated
 * entry data. The caller takes ownership of the "data" buffer, and
 * should copy out any fields it wants before detaching.
 */
static void detach_delta_base_cache_entry(struct delta_base_cache_entry *ent)
{
	hashmap_remove(&delta_base_cache, &ent->ent, &ent->key);
	list_del(&ent->lru);
	delta_base_cached -= ent->size;
	free(ent);
}

static void *cache_or_unpack_entry(struct repository *r, struct packed_git *p,
				   off_t base_offset, unsigned long *base_size,
				   enum object_type *type)
{
	struct delta_base_cache_entry *ent;

	ent = get_delta_base_cache_entry(p, base_offset);
	if (!ent)
		return unpack_entry(r, p, base_offset, type, base_size);

	if (type)
		*type = ent->type;
	if (base_size)
		*base_size = ent->size;
	return xmemdupz(ent->data, ent->size);
}

static inline void release_delta_base_cache(struct delta_base_cache_entry *ent)
{
	free(ent->data);
	detach_delta_base_cache_entry(ent);
}

void clear_delta_base_cache(void)
{
	struct list_head *lru, *tmp;
	list_for_each_safe(lru, tmp, &delta_base_cache_lru) {
		struct delta_base_cache_entry *entry =
			list_entry(lru, struct delta_base_cache_entry, lru);
		release_delta_base_cache(entry);
	}
}

static void add_delta_base_cache(struct packed_git *p, off_t base_offset,
	void *base, unsigned long base_size, enum object_type type)
{
	struct delta_base_cache_entry *ent;
	struct list_head *lru, *tmp;

	/*
	 * Check required to avoid redundant entries when more than one thread
	 * is unpacking the same object, in unpack_entry() (since its phases I
	 * and III might run concurrently across multiple threads).
	 */
	if (in_delta_base_cache(p, base_offset)) {
		free(base);
		return;
	}

	delta_base_cached += base_size;

	list_for_each_safe(lru, tmp, &delta_base_cache_lru) {
		struct delta_base_cache_entry *f =
			list_entry(lru, struct delta_base_cache_entry, lru);
		if (delta_base_cached <= delta_base_cache_limit)
			break;
		release_delta_base_cache(f);
	}

	ent = xmalloc(sizeof(*ent));
	ent->key.p = p;
	ent->key.base_offset = base_offset;
	ent->type = type;
	ent->data = base;
	ent->size = base_size;
	list_add_tail(&ent->lru, &delta_base_cache_lru);

	if (!delta_base_cache.cmpfn)
		hashmap_init(&delta_base_cache, delta_base_cache_hash_cmp, NULL, 0);
	hashmap_entry_init(&ent->ent, pack_entry_hash(p, base_offset));
	hashmap_add(&delta_base_cache, &ent->ent);
}

int packed_object_info(struct repository *r, struct packed_git *p,
		       off_t obj_offset, struct object_info *oi)
{
	struct pack_window *w_curs = NULL;
	unsigned long size;
	off_t curpos = obj_offset;
	enum object_type type;

	/*
	 * We always get the representation type, but only convert it to
	 * a "real" type later if the caller is interested.
	 */
	if (oi->contentp) {
		*oi->contentp = cache_or_unpack_entry(r, p, obj_offset, oi->sizep,
						      &type);
		if (!*oi->contentp)
			type = OBJ_BAD;
	} else {
		type = unpack_object_header(p, &w_curs, &curpos, &size);
	}

	if (!oi->contentp && oi->sizep) {
		if (type == OBJ_OFS_DELTA || type == OBJ_REF_DELTA) {
			off_t tmp_pos = curpos;
			off_t base_offset = get_delta_base(p, &w_curs, &tmp_pos,
							   type, obj_offset);
			if (!base_offset) {
				type = OBJ_BAD;
				goto out;
			}
			*oi->sizep = get_size_from_delta(p, &w_curs, tmp_pos);
			if (*oi->sizep == 0) {
				type = OBJ_BAD;
				goto out;
			}
		} else {
			*oi->sizep = size;
		}
	}

	if (oi->disk_sizep) {
		uint32_t pos;
		if (offset_to_pack_pos(p, obj_offset, &pos) < 0) {
			error("could not find object at offset %"PRIuMAX" "
			      "in pack %s", (uintmax_t)obj_offset, p->pack_name);
			type = OBJ_BAD;
			goto out;
		}

		*oi->disk_sizep = pack_pos_to_offset(p, pos + 1) - obj_offset;
	}

	if (oi->typep || oi->type_name) {
		enum object_type ptot;
		ptot = packed_to_object_type(r, p, obj_offset,
					     type, &w_curs, curpos);
		if (oi->typep)
			*oi->typep = ptot;
		if (oi->type_name) {
			const char *tn = type_name(ptot);
			if (tn)
				strbuf_addstr(oi->type_name, tn);
		}
		if (ptot < 0) {
			type = OBJ_BAD;
			goto out;
		}
	}

	if (oi->delta_base_oid) {
		if (type == OBJ_OFS_DELTA || type == OBJ_REF_DELTA) {
			if (get_delta_base_oid(p, &w_curs, curpos,
					       oi->delta_base_oid,
					       type, obj_offset) < 0) {
				type = OBJ_BAD;
				goto out;
			}
		} else
			oidclr(oi->delta_base_oid);
	}

	oi->whence = in_delta_base_cache(p, obj_offset) ? OI_DBCACHED :
							  OI_PACKED;

out:
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

	buffer = xmallocz_gently(size);
	if (!buffer)
		return NULL;
	memset(&stream, 0, sizeof(stream));
	stream.next_out = buffer;
	stream.avail_out = size + 1;

	git_inflate_init(&stream);
	do {
		in = use_pack(p, w_curs, curpos, &stream.avail_in);
		stream.next_in = in;
		/*
		 * Note: we must ensure the window section returned by
		 * use_pack() will be available throughout git_inflate()'s
		 * unlocked execution. Please refer to the comment at
		 * get_size_from_delta() to see how this is done.
		 */
		obj_read_unlock();
		st = git_inflate(&stream, Z_FINISH);
		obj_read_lock();
		if (!stream.avail_out)
			break; /* the payload is larger than it should be */
		curpos += stream.next_in - in;
	} while (st == Z_OK || st == Z_BUF_ERROR);
	git_inflate_end(&stream);
	if ((st != Z_STREAM_END) || stream.total_out != size) {
		free(buffer);
		return NULL;
	}

	/* versions of zlib can clobber unconsumed portion of outbuf */
	buffer[size] = '\0';

	return buffer;
}

static void write_pack_access_log(struct packed_git *p, off_t obj_offset)
{
	static struct trace_key pack_access = TRACE_KEY_INIT(PACK_ACCESS);
	trace_printf_key(&pack_access, "%s %"PRIuMAX"\n",
			 p->pack_name, (uintmax_t)obj_offset);
}

int do_check_packed_object_crc;

#define UNPACK_ENTRY_STACK_PREALLOC 64
struct unpack_entry_stack_ent {
	off_t obj_offset;
	off_t curpos;
	unsigned long size;
};

void *unpack_entry(struct repository *r, struct packed_git *p, off_t obj_offset,
		   enum object_type *final_type, unsigned long *final_size)
{
	struct pack_window *w_curs = NULL;
	off_t curpos = obj_offset;
	void *data = NULL;
	unsigned long size;
	enum object_type type;
	struct unpack_entry_stack_ent small_delta_stack[UNPACK_ENTRY_STACK_PREALLOC];
	struct unpack_entry_stack_ent *delta_stack = small_delta_stack;
	int delta_stack_nr = 0, delta_stack_alloc = UNPACK_ENTRY_STACK_PREALLOC;
	int base_from_cache = 0;

	write_pack_access_log(p, obj_offset);

	/* PHASE 1: drill down to the innermost base object */
	for (;;) {
		off_t base_offset;
		int i;
		struct delta_base_cache_entry *ent;

		ent = get_delta_base_cache_entry(p, curpos);
		if (ent) {
			type = ent->type;
			data = ent->data;
			size = ent->size;
			detach_delta_base_cache_entry(ent);
			base_from_cache = 1;
			break;
		}

		if (do_check_packed_object_crc && p->index_version > 1) {
			uint32_t pack_pos, index_pos;
			off_t len;

			if (offset_to_pack_pos(p, obj_offset, &pack_pos) < 0) {
				error("could not find object at offset %"PRIuMAX" in pack %s",
				      (uintmax_t)obj_offset, p->pack_name);
				data = NULL;
				goto out;
			}

			len = pack_pos_to_offset(p, pack_pos + 1) - obj_offset;
			index_pos = pack_pos_to_index(p, pack_pos);
			if (check_pack_crc(p, &w_curs, obj_offset, len, index_pos)) {
				struct object_id oid;
				nth_packed_object_id(&oid, p, index_pos);
				error("bad packed object CRC for %s",
				      oid_to_hex(&oid));
				mark_bad_packed_object(p, &oid);
				data = NULL;
				goto out;
			}
		}

		type = unpack_object_header(p, &w_curs, &curpos, &size);
		if (type != OBJ_OFS_DELTA && type != OBJ_REF_DELTA)
			break;

		base_offset = get_delta_base(p, &w_curs, &curpos, type, obj_offset);
		if (!base_offset) {
			error("failed to validate delta base reference "
			      "at offset %"PRIuMAX" from %s",
			      (uintmax_t)curpos, p->pack_name);
			/* bail to phase 2, in hopes of recovery */
			data = NULL;
			break;
		}

		/* push object, proceed to base */
		if (delta_stack_nr >= delta_stack_alloc
		    && delta_stack == small_delta_stack) {
			delta_stack_alloc = alloc_nr(delta_stack_nr);
			ALLOC_ARRAY(delta_stack, delta_stack_alloc);
			COPY_ARRAY(delta_stack, small_delta_stack,
				   delta_stack_nr);
		} else {
			ALLOC_GROW(delta_stack, delta_stack_nr+1, delta_stack_alloc);
		}
		i = delta_stack_nr++;
		delta_stack[i].obj_offset = obj_offset;
		delta_stack[i].curpos = curpos;
		delta_stack[i].size = size;

		curpos = obj_offset = base_offset;
	}

	/* PHASE 2: handle the base */
	switch (type) {
	case OBJ_OFS_DELTA:
	case OBJ_REF_DELTA:
		if (data)
			BUG("unpack_entry: left loop at a valid delta");
		break;
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		if (!base_from_cache)
			data = unpack_compressed_entry(p, &w_curs, curpos, size);
		break;
	default:
		data = NULL;
		error("unknown object type %i at offset %"PRIuMAX" in %s",
		      type, (uintmax_t)obj_offset, p->pack_name);
	}

	/* PHASE 3: apply deltas in order */

	/* invariants:
	 *   'data' holds the base data, or NULL if there was corruption
	 */
	while (delta_stack_nr) {
		void *delta_data;
		void *base = data;
		void *external_base = NULL;
		unsigned long delta_size, base_size = size;
		int i;
		off_t base_obj_offset = obj_offset;

		data = NULL;

		if (!base) {
			/*
			 * We're probably in deep shit, but let's try to fetch
			 * the required base anyway from another pack or loose.
			 * This is costly but should happen only in the presence
			 * of a corrupted pack, and is better than failing outright.
			 */
			uint32_t pos;
			struct object_id base_oid;
			if (!(offset_to_pack_pos(p, obj_offset, &pos))) {
				struct object_info oi = OBJECT_INFO_INIT;

				nth_packed_object_id(&base_oid, p,
						     pack_pos_to_index(p, pos));
				error("failed to read delta base object %s"
				      " at offset %"PRIuMAX" from %s",
				      oid_to_hex(&base_oid), (uintmax_t)obj_offset,
				      p->pack_name);
				mark_bad_packed_object(p, &base_oid);

				oi.typep = &type;
				oi.sizep = &base_size;
				oi.contentp = &base;
				if (oid_object_info_extended(r, &base_oid, &oi, 0) < 0)
					base = NULL;

				external_base = base;
			}
		}

		i = --delta_stack_nr;
		obj_offset = delta_stack[i].obj_offset;
		curpos = delta_stack[i].curpos;
		delta_size = delta_stack[i].size;

		if (!base)
			continue;

		delta_data = unpack_compressed_entry(p, &w_curs, curpos, delta_size);

		if (!delta_data) {
			error("failed to unpack compressed delta "
			      "at offset %"PRIuMAX" from %s",
			      (uintmax_t)curpos, p->pack_name);
			data = NULL;
		} else {
			data = patch_delta(base, base_size, delta_data,
					   delta_size, &size);

			/*
			 * We could not apply the delta; warn the user, but
			 * keep going. Our failure will be noticed either in
			 * the next iteration of the loop, or if this is the
			 * final delta, in the caller when we return NULL.
			 * Those code paths will take care of making a more
			 * explicit warning and retrying with another copy of
			 * the object.
			 */
			if (!data)
				error("failed to apply delta");
		}

		/*
		 * We delay adding `base` to the cache until the end of the loop
		 * because unpack_compressed_entry() momentarily releases the
		 * obj_read_mutex, giving another thread the chance to access
		 * the cache. Therefore, if `base` was already there, this other
		 * thread could free() it (e.g. to make space for another entry)
		 * before we are done using it.
		 */
		if (!external_base)
			add_delta_base_cache(p, base_obj_offset, base, base_size, type);

		free(delta_data);
		free(external_base);
	}

	if (final_type)
		*final_type = type;
	if (final_size)
		*final_size = size;

out:
	unuse_pack(&w_curs);

	if (delta_stack != small_delta_stack)
		free(delta_stack);

	return data;
}

int bsearch_pack(const struct object_id *oid, const struct packed_git *p, uint32_t *result)
{
	const unsigned char *index_fanout = p->index_data;
	const unsigned char *index_lookup;
	const unsigned int hashsz = the_hash_algo->rawsz;
	int index_lookup_width;

	if (!index_fanout)
		BUG("bsearch_pack called without a valid pack-index");

	index_lookup = index_fanout + 4 * 256;
	if (p->index_version == 1) {
		index_lookup_width = hashsz + 4;
		index_lookup += 4;
	} else {
		index_lookup_width = hashsz;
		index_fanout += 8;
		index_lookup += 8;
	}

	return bsearch_hash(oid->hash, (const uint32_t*)index_fanout,
			    index_lookup, index_lookup_width, result);
}

int nth_packed_object_id(struct object_id *oid,
			 struct packed_git *p,
			 uint32_t n)
{
	const unsigned char *index = p->index_data;
	const unsigned int hashsz = the_hash_algo->rawsz;
	if (!index) {
		if (open_pack_index(p))
			return -1;
		index = p->index_data;
	}
	if (n >= p->num_objects)
		return -1;
	index += 4 * 256;
	if (p->index_version == 1) {
		oidread(oid, index + (hashsz + 4) * n + 4);
	} else {
		index += 8;
		oidread(oid, index + hashsz * n);
	}
	return 0;
}

void check_pack_index_ptr(const struct packed_git *p, const void *vptr)
{
	const unsigned char *ptr = vptr;
	const unsigned char *start = p->index_data;
	const unsigned char *end = start + p->index_size;
	if (ptr < start)
		die(_("offset before start of pack index for %s (corrupt index?)"),
		    p->pack_name);
	/* No need to check for underflow; .idx files must be at least 8 bytes */
	if (ptr >= end - 8)
		die(_("offset beyond end of pack index for %s (truncated index?)"),
		    p->pack_name);
}

off_t nth_packed_object_offset(const struct packed_git *p, uint32_t n)
{
	const unsigned char *index = p->index_data;
	const unsigned int hashsz = the_hash_algo->rawsz;
	index += 4 * 256;
	if (p->index_version == 1) {
		return ntohl(*((uint32_t *)(index + (hashsz + 4) * (size_t)n)));
	} else {
		uint32_t off;
		index += 8 + (size_t)p->num_objects * (hashsz + 4);
		off = ntohl(*((uint32_t *)(index + 4 * n)));
		if (!(off & 0x80000000))
			return off;
		index += (size_t)p->num_objects * 4 + (off & 0x7fffffff) * 8;
		check_pack_index_ptr(p, index);
		return get_be64(index);
	}
}

off_t find_pack_entry_one(const unsigned char *sha1,
				  struct packed_git *p)
{
	const unsigned char *index = p->index_data;
	struct object_id oid;
	uint32_t result;

	if (!index) {
		if (open_pack_index(p))
			return 0;
	}

	hashcpy(oid.hash, sha1);
	if (bsearch_pack(&oid, p, &result))
		return nth_packed_object_offset(p, result);
	return 0;
}

int is_pack_valid(struct packed_git *p)
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

static int fill_pack_entry(const struct object_id *oid,
			   struct pack_entry *e,
			   struct packed_git *p)
{
	off_t offset;

	if (oidset_size(&p->bad_objects) &&
	    oidset_contains(&p->bad_objects, oid))
		return 0;

	offset = find_pack_entry_one(oid->hash, p);
	if (!offset)
		return 0;

	/*
	 * We are about to tell the caller where they can locate the
	 * requested object.  We better make sure the packfile is
	 * still here and can be accessed before supplying that
	 * answer, as it may have been deleted since the index was
	 * loaded!
	 */
	if (!is_pack_valid(p))
		return 0;
	e->offset = offset;
	e->p = p;
	return 1;
}

int find_pack_entry(struct repository *r, const struct object_id *oid, struct pack_entry *e)
{
	struct list_head *pos;
	struct multi_pack_index *m;

	prepare_packed_git(r);
	if (!r->objects->packed_git && !r->objects->multi_pack_index)
		return 0;

	for (m = r->objects->multi_pack_index; m; m = m->next) {
		if (fill_midx_entry(r, oid, e, m))
			return 1;
	}

	list_for_each(pos, &r->objects->packed_git_mru) {
		struct packed_git *p = list_entry(pos, struct packed_git, mru);
		if (!p->multi_pack_index && fill_pack_entry(oid, e, p)) {
			list_move(&p->mru, &r->objects->packed_git_mru);
			return 1;
		}
	}
	return 0;
}

static void maybe_invalidate_kept_pack_cache(struct repository *r,
					     unsigned flags)
{
	if (!r->objects->kept_pack_cache.packs)
		return;
	if (r->objects->kept_pack_cache.flags == flags)
		return;
	FREE_AND_NULL(r->objects->kept_pack_cache.packs);
	r->objects->kept_pack_cache.flags = 0;
}

static struct packed_git **kept_pack_cache(struct repository *r, unsigned flags)
{
	maybe_invalidate_kept_pack_cache(r, flags);

	if (!r->objects->kept_pack_cache.packs) {
		struct packed_git **packs = NULL;
		size_t nr = 0, alloc = 0;
		struct packed_git *p;

		/*
		 * We want "all" packs here, because we need to cover ones that
		 * are used by a midx, as well. We need to look in every one of
		 * them (instead of the midx itself) to cover duplicates. It's
		 * possible that an object is found in two packs that the midx
		 * covers, one kept and one not kept, but the midx returns only
		 * the non-kept version.
		 */
		for (p = get_all_packs(r); p; p = p->next) {
			if ((p->pack_keep && (flags & ON_DISK_KEEP_PACKS)) ||
			    (p->pack_keep_in_core && (flags & IN_CORE_KEEP_PACKS))) {
				ALLOC_GROW(packs, nr + 1, alloc);
				packs[nr++] = p;
			}
		}
		ALLOC_GROW(packs, nr + 1, alloc);
		packs[nr] = NULL;

		r->objects->kept_pack_cache.packs = packs;
		r->objects->kept_pack_cache.flags = flags;
	}

	return r->objects->kept_pack_cache.packs;
}

int find_kept_pack_entry(struct repository *r,
			 const struct object_id *oid,
			 unsigned flags,
			 struct pack_entry *e)
{
	struct packed_git **cache;

	for (cache = kept_pack_cache(r, flags); *cache; cache++) {
		struct packed_git *p = *cache;
		if (fill_pack_entry(oid, e, p))
			return 1;
	}

	return 0;
}

int has_object_pack(const struct object_id *oid)
{
	struct pack_entry e;
	return find_pack_entry(the_repository, oid, &e);
}

int has_object_kept_pack(const struct object_id *oid, unsigned flags)
{
	struct pack_entry e;
	return find_kept_pack_entry(the_repository, oid, flags, &e);
}

int has_pack_index(const unsigned char *sha1)
{
	struct stat st;
	if (stat(sha1_pack_index_name(sha1), &st))
		return 0;
	return 1;
}

int for_each_object_in_pack(struct packed_git *p,
			    each_packed_object_fn cb, void *data,
			    enum for_each_object_flags flags)
{
	uint32_t i;
	int r = 0;

	if (flags & FOR_EACH_OBJECT_PACK_ORDER) {
		if (load_pack_revindex(p))
			return -1;
	}

	for (i = 0; i < p->num_objects; i++) {
		uint32_t index_pos;
		struct object_id oid;

		/*
		 * We are iterating "i" from 0 up to num_objects, but its
		 * meaning may be different, depending on the requested output
		 * order:
		 *
		 *   - in object-name order, it is the same as the index order
		 *     used by nth_packed_object_id(), so we can pass it
		 *     directly
		 *
		 *   - in pack-order, it is pack position, which we must
		 *     convert to an index position in order to get the oid.
		 */
		if (flags & FOR_EACH_OBJECT_PACK_ORDER)
			index_pos = pack_pos_to_index(p, i);
		else
			index_pos = i;

		if (nth_packed_object_id(&oid, p, index_pos) < 0)
			return error("unable to get sha1 of object %u in %s",
				     index_pos, p->pack_name);

		r = cb(&oid, p, index_pos, data);
		if (r)
			break;
	}
	return r;
}

int for_each_packed_object(each_packed_object_fn cb, void *data,
			   enum for_each_object_flags flags)
{
	struct packed_git *p;
	int r = 0;
	int pack_errors = 0;

	prepare_packed_git(the_repository);
	for (p = get_all_packs(the_repository); p; p = p->next) {
		if ((flags & FOR_EACH_OBJECT_LOCAL_ONLY) && !p->pack_local)
			continue;
		if ((flags & FOR_EACH_OBJECT_PROMISOR_ONLY) &&
		    !p->pack_promisor)
			continue;
		if ((flags & FOR_EACH_OBJECT_SKIP_IN_CORE_KEPT_PACKS) &&
		    p->pack_keep_in_core)
			continue;
		if ((flags & FOR_EACH_OBJECT_SKIP_ON_DISK_KEPT_PACKS) &&
		    p->pack_keep)
			continue;
		if (open_pack_index(p)) {
			pack_errors = 1;
			continue;
		}
		r = for_each_object_in_pack(p, cb, data, flags);
		if (r)
			break;
	}
	return r ? r : pack_errors;
}

static int add_promisor_object(const struct object_id *oid,
			       struct packed_git *pack UNUSED,
			       uint32_t pos UNUSED,
			       void *set_)
{
	struct oidset *set = set_;
	struct object *obj;
	int we_parsed_object;

	obj = lookup_object(the_repository, oid);
	if (obj && obj->parsed) {
		we_parsed_object = 0;
	} else {
		we_parsed_object = 1;
		obj = parse_object(the_repository, oid);
	}

	if (!obj)
		return 1;

	oidset_insert(set, oid);

	/*
	 * If this is a tree, commit, or tag, the objects it refers
	 * to are also promisor objects. (Blobs refer to no objects->)
	 */
	if (obj->type == OBJ_TREE) {
		struct tree *tree = (struct tree *)obj;
		struct tree_desc desc;
		struct name_entry entry;
		if (init_tree_desc_gently(&desc, tree->buffer, tree->size, 0))
			/*
			 * Error messages are given when packs are
			 * verified, so do not print any here.
			 */
			return 0;
		while (tree_entry_gently(&desc, &entry))
			oidset_insert(set, &entry.oid);
		if (we_parsed_object)
			free_tree_buffer(tree);
	} else if (obj->type == OBJ_COMMIT) {
		struct commit *commit = (struct commit *) obj;
		struct commit_list *parents = commit->parents;

		oidset_insert(set, get_commit_tree_oid(commit));
		for (; parents; parents = parents->next)
			oidset_insert(set, &parents->item->object.oid);
	} else if (obj->type == OBJ_TAG) {
		struct tag *tag = (struct tag *) obj;
		oidset_insert(set, get_tagged_oid(tag));
	}
	return 0;
}

int is_promisor_object(const struct object_id *oid)
{
	static struct oidset promisor_objects;
	static int promisor_objects_prepared;

	if (!promisor_objects_prepared) {
		if (repo_has_promisor_remote(the_repository)) {
			for_each_packed_object(add_promisor_object,
					       &promisor_objects,
					       FOR_EACH_OBJECT_PROMISOR_ONLY |
					       FOR_EACH_OBJECT_PACK_ORDER);
		}
		promisor_objects_prepared = 1;
	}
	return oidset_contains(&promisor_objects, oid);
}
