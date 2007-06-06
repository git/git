#include "cache.h"
#include "pack.h"
#include "csum-file.h"

uint32_t pack_idx_default_version = 1;
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
const char *write_idx_file(const char *index_name, struct pack_idx_entry **objects, int nr_objects, unsigned char *sha1)
{
	struct sha1file *f;
	struct pack_idx_entry **sorted_by_sha, **list, **last;
	off_t last_obj_offset = 0;
	uint32_t array[256];
	int i, fd;
	SHA_CTX ctx;
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
		snprintf(tmpfile, sizeof(tmpfile),
			 "%s/tmp_idx_XXXXXX", get_object_directory());
		fd = mkstemp(tmpfile);
		index_name = xstrdup(tmpfile);
	} else {
		unlink(index_name);
		fd = open(index_name, O_CREAT|O_EXCL|O_WRONLY, 0600);
	}
	if (fd < 0)
		die("unable to create %s: %s", index_name, strerror(errno));
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
	SHA1_Init(&ctx);

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
		SHA1_Update(&ctx, obj->sha1, 20);
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
	sha1close(f, NULL, 1);
	SHA1_Final(sha1, &ctx);
	return index_name;
}

void fixup_pack_header_footer(int pack_fd,
			 unsigned char *pack_file_sha1,
			 const char *pack_name,
			 uint32_t object_count)
{
	static const int buf_sz = 128 * 1024;
	SHA_CTX c;
	struct pack_header hdr;
	char *buf;

	if (lseek(pack_fd, 0, SEEK_SET) != 0)
		die("Failed seeking to start: %s", strerror(errno));
	if (read_in_full(pack_fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		die("Unable to reread header of %s: %s", pack_name, strerror(errno));
	if (lseek(pack_fd, 0, SEEK_SET) != 0)
		die("Failed seeking to start: %s", strerror(errno));
	hdr.hdr_entries = htonl(object_count);
	write_or_die(pack_fd, &hdr, sizeof(hdr));

	SHA1_Init(&c);
	SHA1_Update(&c, &hdr, sizeof(hdr));

	buf = xmalloc(buf_sz);
	for (;;) {
		ssize_t n = xread(pack_fd, buf, buf_sz);
		if (!n)
			break;
		if (n < 0)
			die("Failed to checksum %s: %s", pack_name, strerror(errno));
		SHA1_Update(&c, buf, n);
	}
	free(buf);

	SHA1_Final(pack_file_sha1, &c);
	write_or_die(pack_fd, pack_file_sha1, 20);
}
