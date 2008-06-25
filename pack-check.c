#include "cache.h"
#include "pack.h"
#include "pack-revindex.h"

struct idx_entry
{
	const unsigned char *sha1;
	off_t                offset;
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

static int verify_packfile(struct packed_git *p,
		struct pack_window **w_curs)
{
	off_t index_size = p->index_size;
	const unsigned char *index_base = p->index_data;
	SHA_CTX ctx;
	unsigned char sha1[20], *pack_sig;
	off_t offset = 0, pack_sig_ofs = p->pack_size - 20;
	uint32_t nr_objects, i;
	int err = 0;
	struct idx_entry *entries;

	/* Note that the pack header checks are actually performed by
	 * use_pack when it first opens the pack file.  If anything
	 * goes wrong during those checks then the call will die out
	 * immediately.
	 */

	SHA1_Init(&ctx);
	while (offset < pack_sig_ofs) {
		unsigned int remaining;
		unsigned char *in = use_pack(p, w_curs, offset, &remaining);
		offset += remaining;
		if (offset > pack_sig_ofs)
			remaining -= (unsigned int)(offset - pack_sig_ofs);
		SHA1_Update(&ctx, in, remaining);
	}
	SHA1_Final(sha1, &ctx);
	pack_sig = use_pack(p, w_curs, pack_sig_ofs, NULL);
	if (hashcmp(sha1, pack_sig))
		err = error("%s SHA1 checksum mismatch",
			    p->pack_name);
	if (hashcmp(index_base + index_size - 40, pack_sig))
		err = error("%s SHA1 does not match its inddex",
			    p->pack_name);
	unuse_pack(w_curs);

	/* Make sure everything reachable from idx is valid.  Since we
	 * have verified that nr_objects matches between idx and pack,
	 * we do not do scan-streaming check on the pack file.
	 */
	nr_objects = p->num_objects;
	entries = xmalloc(nr_objects * sizeof(*entries));
	/* first sort entries by pack offset, since unpacking them is more efficient that way */
	for (i = 0; i < nr_objects; i++) {
		entries[i].sha1 = nth_packed_object_sha1(p, i);
		if (!entries[i].sha1)
			die("internal error pack-check nth-packed-object");
		entries[i].offset = nth_packed_object_offset(p, i);
	}
	qsort(entries, nr_objects, sizeof(*entries), compare_entries);

	for (i = 0; i < nr_objects; i++) {
		void *data;
		enum object_type type;
		unsigned long size;

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

int verify_pack(struct packed_git *p)
{
	off_t index_size;
	const unsigned char *index_base;
	SHA_CTX ctx;
	unsigned char sha1[20];
	int err = 0;
	struct pack_window *w_curs = NULL;

	if (open_pack_index(p))
		return error("packfile %s index not opened", p->pack_name);
	index_size = p->index_size;
	index_base = p->index_data;

	/* Verify SHA1 sum of the index file */
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, index_base, (unsigned int)(index_size - 20));
	SHA1_Final(sha1, &ctx);
	if (hashcmp(sha1, index_base + index_size - 20))
		err = error("Packfile index for %s SHA1 mismatch",
			    p->pack_name);

	/* Verify pack file */
	err |= verify_packfile(p, &w_curs);
	unuse_pack(&w_curs);

	return err;
}
