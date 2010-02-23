#include "cache.h"
#include "pack.h"
#include "csum-file.h"

uint32_t pack_idx_default_version = 2;
uint32_t pack_idx_off32_limit = 0x7fffffff;

static int sha1_compare(const void *_a, const void *_b)
{
	struct pack_idx_entry *a = *(struct pack_idx_entry **)_a;
	struct pack_idx_entry *b = *(struct pack_idx_entry **)_b;
	return hashcmp(a->sha1, b->sha1);
}

/*
 * On entry *sha1 contains the pack content SHA1 hash, on exit it is
 * the SHA1 hash of sorted object names. The objects array passed in
 * will be sorted by SHA1 on exit.
 */
const char *write_idx_file(const char *index_name, struct pack_idx_entry **objects,
			   int nr_objects, unsigned char *sha1)
{
	struct sha1file *f;
	struct pack_idx_entry **sorted_by_sha, **list, **last;
	off_t last_obj_offset = 0;
	uint32_t array[256];
	int i, fd;
	git_SHA_CTX ctx;
	uint32_t index_version;

	if (nr_objects) {
		sorted_by_sha = objects;
		list = sorted_by_sha;
		last = sorted_by_sha + nr_objects;
		for (i = 0; i < nr_objects; ++i) {
			if (objects[i]->offset > last_obj_offset)
				last_obj_offset = objects[i]->offset;
		}
		qsort(sorted_by_sha, nr_objects, sizeof(sorted_by_sha[0]),
		      sha1_compare);
	}
	else
		sorted_by_sha = list = last = NULL;

	if (!index_name) {
		static char tmpfile[PATH_MAX];
		fd = odb_mkstemp(tmpfile, sizeof(tmpfile), "pack/tmp_idx_XXXXXX");
		index_name = xstrdup(tmpfile);
	} else {
		unlink(index_name);
		fd = open(index_name, O_CREAT|O_EXCL|O_WRONLY, 0600);
	}
	if (fd < 0)
		die_errno("unable to create '%s'", index_name);
	f = sha1fd(fd, index_name);

	/* if last object's offset is >= 2^31 we should use index V2 */
	index_version = (last_obj_offset >> 31) ? 2 : pack_idx_default_version;

	/* index versions 2 and above need a header */
	if (index_version >= 2) {
		struct pack_idx_header hdr;
		hdr.idx_signature = htonl(PACK_IDX_SIGNATURE);
		hdr.idx_version = htonl(index_version);
		sha1write(f, &hdr, sizeof(hdr));
	}

	/*
	 * Write the first-level table (the list is sorted,
	 * but we use a 256-entry lookup to be able to avoid
	 * having to do eight extra binary search iterations).
	 */
	for (i = 0; i < 256; i++) {
		struct pack_idx_entry **next = list;
		while (next < last) {
			struct pack_idx_entry *obj = *next;
			if (obj->sha1[0] != i)
				break;
			next++;
		}
		array[i] = htonl(next - sorted_by_sha);
		list = next;
	}
	sha1write(f, array, 256 * 4);

	/* compute the SHA1 hash of sorted object names. */
	git_SHA1_Init(&ctx);

	/*
	 * Write the actual SHA1 entries..
	 */
	list = sorted_by_sha;
	for (i = 0; i < nr_objects; i++) {
		struct pack_idx_entry *obj = *list++;
		if (index_version < 2) {
			uint32_t offset = htonl(obj->offset);
			sha1write(f, &offset, 4);
		}
		sha1write(f, obj->sha1, 20);
		git_SHA1_Update(&ctx, obj->sha1, 20);
	}

	if (index_version >= 2) {
		unsigned int nr_large_offset = 0;

		/* write the crc32 table */
		list = sorted_by_sha;
		for (i = 0; i < nr_objects; i++) {
			struct pack_idx_entry *obj = *list++;
			uint32_t crc32_val = htonl(obj->crc32);
			sha1write(f, &crc32_val, 4);
		}

		/* write the 32-bit offset table */
		list = sorted_by_sha;
		for (i = 0; i < nr_objects; i++) {
			struct pack_idx_entry *obj = *list++;
			uint32_t offset = (obj->offset <= pack_idx_off32_limit) ?
				obj->offset : (0x80000000 | nr_large_offset++);
			offset = htonl(offset);
			sha1write(f, &offset, 4);
		}

		/* write the large offset table */
		list = sorted_by_sha;
		while (nr_large_offset) {
			struct pack_idx_entry *obj = *list++;
			uint64_t offset = obj->offset;
			if (offset > pack_idx_off32_limit) {
				uint32_t split[2];
				split[0] = htonl(offset >> 32);
				split[1] = htonl(offset & 0xffffffff);
				sha1write(f, split, 8);
				nr_large_offset--;
			}
		}
	}

	sha1write(f, sha1, 20);
	sha1close(f, NULL, CSUM_FSYNC);
	git_SHA1_Final(sha1, &ctx);
	return index_name;
}

/*
 * Update pack header with object_count and compute new SHA1 for pack data
 * associated to pack_fd, and write that SHA1 at the end.  That new SHA1
 * is also returned in new_pack_sha1.
 *
 * If partial_pack_sha1 is non null, then the SHA1 of the existing pack
 * (without the header update) is computed and validated against the
 * one provided in partial_pack_sha1.  The validation is performed at
 * partial_pack_offset bytes in the pack file.  The SHA1 of the remaining
 * data (i.e. from partial_pack_offset to the end) is then computed and
 * returned in partial_pack_sha1.
 *
 * Note that new_pack_sha1 is updated last, so both new_pack_sha1 and
 * partial_pack_sha1 can refer to the same buffer if the caller is not
 * interested in the resulting SHA1 of pack data above partial_pack_offset.
 */
void fixup_pack_header_footer(int pack_fd,
			 unsigned char *new_pack_sha1,
			 const char *pack_name,
			 uint32_t object_count,
			 unsigned char *partial_pack_sha1,
			 off_t partial_pack_offset)
{
	int aligned_sz, buf_sz = 8 * 1024;
	git_SHA_CTX old_sha1_ctx, new_sha1_ctx;
	struct pack_header hdr;
	char *buf;

	git_SHA1_Init(&old_sha1_ctx);
	git_SHA1_Init(&new_sha1_ctx);

	if (lseek(pack_fd, 0, SEEK_SET) != 0)
		die_errno("Failed seeking to start of '%s'", pack_name);
	if (read_in_full(pack_fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		die_errno("Unable to reread header of '%s'", pack_name);
	if (lseek(pack_fd, 0, SEEK_SET) != 0)
		die_errno("Failed seeking to start of '%s'", pack_name);
	git_SHA1_Update(&old_sha1_ctx, &hdr, sizeof(hdr));
	hdr.hdr_entries = htonl(object_count);
	git_SHA1_Update(&new_sha1_ctx, &hdr, sizeof(hdr));
	write_or_die(pack_fd, &hdr, sizeof(hdr));
	partial_pack_offset -= sizeof(hdr);

	buf = xmalloc(buf_sz);
	aligned_sz = buf_sz - sizeof(hdr);
	for (;;) {
		ssize_t m, n;
		m = (partial_pack_sha1 && partial_pack_offset < aligned_sz) ?
			partial_pack_offset : aligned_sz;
		n = xread(pack_fd, buf, m);
		if (!n)
			break;
		if (n < 0)
			die_errno("Failed to checksum '%s'", pack_name);
		git_SHA1_Update(&new_sha1_ctx, buf, n);

		aligned_sz -= n;
		if (!aligned_sz)
			aligned_sz = buf_sz;

		if (!partial_pack_sha1)
			continue;

		git_SHA1_Update(&old_sha1_ctx, buf, n);
		partial_pack_offset -= n;
		if (partial_pack_offset == 0) {
			unsigned char sha1[20];
			git_SHA1_Final(sha1, &old_sha1_ctx);
			if (hashcmp(sha1, partial_pack_sha1) != 0)
				die("Unexpected checksum for %s "
				    "(disk corruption?)", pack_name);
			/*
			 * Now let's compute the SHA1 of the remainder of the
			 * pack, which also means making partial_pack_offset
			 * big enough not to matter anymore.
			 */
			git_SHA1_Init(&old_sha1_ctx);
			partial_pack_offset = ~partial_pack_offset;
			partial_pack_offset -= MSB(partial_pack_offset, 1);
		}
	}
	free(buf);

	if (partial_pack_sha1)
		git_SHA1_Final(partial_pack_sha1, &old_sha1_ctx);
	git_SHA1_Final(new_pack_sha1, &new_sha1_ctx);
	write_or_die(pack_fd, new_pack_sha1, 20);
	fsync_or_die(pack_fd, pack_name);
}

char *index_pack_lockfile(int ip_out)
{
	char packname[46];

	/*
	 * The first thing we expect from index-pack's output
	 * is "pack\t%40s\n" or "keep\t%40s\n" (46 bytes) where
	 * %40s is the newly created pack SHA1 name.  In the "keep"
	 * case, we need it to remove the corresponding .keep file
	 * later on.  If we don't get that then tough luck with it.
	 */
	if (read_in_full(ip_out, packname, 46) == 46 && packname[45] == '\n' &&
	    memcmp(packname, "keep\t", 5) == 0) {
		char path[PATH_MAX];
		packname[45] = 0;
		snprintf(path, sizeof(path), "%s/pack/pack-%s.keep",
			 get_object_directory(), packname + 5);
		return xstrdup(path);
	}
	return NULL;
}

/*
 * The per-object header is a pretty dense thing, which is
 *  - first byte: low four bits are "size", then three bits of "type",
 *    and the high bit is "size continues".
 *  - each byte afterwards: low seven bits are size continuation,
 *    with the high bit being "size continues"
 */
int encode_in_pack_object_header(enum object_type type, uintmax_t size, unsigned char *hdr)
{
	int n = 1;
	unsigned char c;

	if (type < OBJ_COMMIT || type > OBJ_REF_DELTA)
		die("bad type %d", type);

	c = (type << 4) | (size & 15);
	size >>= 4;
	while (size) {
		*hdr++ = c | 0x80;
		c = size & 0x7f;
		size >>= 7;
		n++;
	}
	*hdr = c;
	return n;
}
