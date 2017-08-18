#include "cache.h"
#include "mru.h"
#include "pack.h"

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

unsigned int pack_used_ctr;
unsigned int pack_mmap_calls;
unsigned int peak_pack_open_windows;
unsigned int pack_open_windows;
unsigned int pack_open_fds;
unsigned int pack_max_fds;
size_t peak_pack_mapped;
size_t pack_mapped;
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
