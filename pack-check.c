#include "cache.h"
#include "pack.h"
#include "pack-revindex.h"

struct idx_entry {
	off_t                offset;
	const unsigned char *sha1;
	unsigned int nr;
};

static int compare_entries(const void *e1, const void *e2)
{
	const struct idx_entry *entry1 = e1;
	const struct idx_entry *entry2 = e2;
	if (entry1->offset < entry2->offset)
		return -1;
	if (entry1->offset > entry2->offset)
		return 1;
	return 0;
}

int check_pack_crc(struct packed_git *p, struct pack_window **w_curs,
		   off_t offset, off_t len, unsigned int nr)
{
	const uint32_t *index_crc;
	uint32_t data_crc = crc32(0, NULL, 0);

	do {
		unsigned long avail;
		void *data = use_pack(p, w_curs, offset, &avail);
		if (avail > len)
			avail = len;
		data_crc = crc32(data_crc, data, avail);
		offset += avail;
		len -= avail;
	} while (len);

	index_crc = p->index_data;
	index_crc += 2 + 256 + p->num_objects * (20/4) + nr;

	return data_crc != ntohl(*index_crc);
}

static int verify_packfile(struct packed_git *p,
		struct pack_window **w_curs)
{
	off_t index_size = p->index_size;
	const unsigned char *index_base = p->index_data;
	git_SHA_CTX ctx;
	unsigned char sha1[20], *pack_sig;
	off_t offset = 0, pack_sig_ofs = 0;
	uint32_t nr_objects, i;
	int err = 0;
	struct idx_entry *entries;

	/* Note that the pack header checks are actually performed by
	 * use_pack when it first opens the pack file.  If anything
	 * goes wrong during those checks then the call will die out
	 * immediately.
	 */

	git_SHA1_Init(&ctx);
	do {
		unsigned long remaining;
		unsigned char *in = use_pack(p, w_curs, offset, &remaining);
		offset += remaining;
		if (!pack_sig_ofs)
			pack_sig_ofs = p->pack_size - 20;
		if (offset > pack_sig_ofs)
			remaining -= (unsigned int)(offset - pack_sig_ofs);
		git_SHA1_Update(&ctx, in, remaining);
	} while (offset < pack_sig_ofs);
	git_SHA1_Final(sha1, &ctx);
	pack_sig = use_pack(p, w_curs, pack_sig_ofs, NULL);
	if (hashcmp(sha1, pack_sig))
		err = error("%s SHA1 checksum mismatch",
			    p->pack_name);
	if (hashcmp(index_base + index_size - 40, pack_sig))
		err = error("%s SHA1 does not match its index",
			    p->pack_name);
	unuse_pack(w_curs);

	/* Make sure everything reachable from idx is valid.  Since we
	 * have verified that nr_objects matches between idx and pack,
	 * we do not do scan-streaming check on the pack file.
	 */
	nr_objects = p->num_objects;
	entries = xmalloc((nr_objects + 1) * sizeof(*entries));
	entries[nr_objects].offset = pack_sig_ofs;
	/* first sort entries by pack offset, since unpacking them is more efficient that way */
	for (i = 0; i < nr_objects; i++) {
		entries[i].sha1 = nth_packed_object_sha1(p, i);
		if (!entries[i].sha1)
			die("internal error pack-check nth-packed-object");
		entries[i].offset = nth_packed_object_offset(p, i);
		entries[i].nr = i;
	}
	qsort(entries, nr_objects, sizeof(*entries), compare_entries);

	for (i = 0; i < nr_objects; i++) {
		void *data;
		enum object_type type;
		unsigned long size;

		if (p->index_version > 1) {
			off_t offset = entries[i].offset;
			off_t len = entries[i+1].offset - offset;
			unsigned int nr = entries[i].nr;
			if (check_pack_crc(p, w_curs, offset, len, nr))
				err = error("index CRC mismatch for object %s "
					    "from %s at offset %"PRIuMAX"",
					    sha1_to_hex(entries[i].sha1),
					    p->pack_name, (uintmax_t)offset);
		}
		data = unpack_entry(p, entries[i].offset, &type, &size);
		if (!data) {
			err = error("cannot unpack %s from %s at offset %"PRIuMAX"",
				    sha1_to_hex(entries[i].sha1), p->pack_name,
				    (uintmax_t)entries[i].offset);
			break;
		}
		if (check_sha1_signature(entries[i].sha1, data, size, typename(type))) {
			err = error("packed %s from %s is corrupt",
				    sha1_to_hex(entries[i].sha1), p->pack_name);
			free(data);
			break;
		}
		free(data);
	}
	free(entries);

	return err;
}

int verify_pack_index(struct packed_git *p)
{
	off_t index_size;
	const unsigned char *index_base;
	git_SHA_CTX ctx;
	unsigned char sha1[20];
	int err = 0;

	if (open_pack_index(p))
		return error("packfile %s index not opened", p->pack_name);
	index_size = p->index_size;
	index_base = p->index_data;

	/* Verify SHA1 sum of the index file */
	git_SHA1_Init(&ctx);
	git_SHA1_Update(&ctx, index_base, (unsigned int)(index_size - 20));
	git_SHA1_Final(sha1, &ctx);
	if (hashcmp(sha1, index_base + index_size - 20))
		err = error("Packfile index for %s SHA1 mismatch",
			    p->pack_name);
	return err;
}

int verify_pack(struct packed_git *p)
{
	int err = 0;
	struct pack_window *w_curs = NULL;

	err |= verify_pack_index(p);
	if (!p->index_data)
		return -1;

	err |= verify_packfile(p, &w_curs);
	unuse_pack(&w_curs);

	return err;
}
