#include "cache.h"
#include "pack.h"

static int verify_packfile(struct packed_git *p)
{
	unsigned long index_size = p->index_size;
	void *index_base = p->index_base;
	SHA_CTX ctx;
	unsigned char sha1[20];
	unsigned long pack_size = p->pack_size;
	void *pack_base;
	struct pack_header *hdr;
	int nr_objects, err, i;

	/* Header consistency check */
	hdr = p->pack_base;
	if (hdr->hdr_signature != htonl(PACK_SIGNATURE))
		return error("Packfile signature mismatch", p->pack_name);
	if (hdr->hdr_version != htonl(PACK_VERSION))
		return error("Packfile version %d different from ours %d",
			     ntohl(hdr->hdr_version), PACK_VERSION);
	nr_objects = ntohl(hdr->hdr_entries);
	if (num_packed_objects(p) != nr_objects)
		return error("Packfile claims to have %d objects, "
			     "while idx size expects %d", nr_objects,
			     num_packed_objects(p));

	SHA1_Init(&ctx);
	pack_base = p->pack_base;
	SHA1_Update(&ctx, pack_base, pack_size - 20);
	SHA1_Final(sha1, &ctx);
	if (memcmp(sha1, index_base + index_size - 40, 20))
		return error("Packfile %s SHA1 mismatch with idx",
			     p->pack_name);
	if (memcmp(sha1, pack_base + pack_size - 20, 20))
		return error("Packfile %s SHA1 mismatch with itself",
			     p->pack_name);

	/* Make sure everything reachable from idx is valid.  Since we
	 * have verified that nr_objects matches between idx and pack,
	 * we do not do scan-streaming check on the pack file.
	 */
	for (i = err = 0; i < nr_objects; i++) {
		unsigned char sha1[20];
		struct pack_entry e;
		void *data;
		char type[20];
		unsigned long size;

		if (nth_packed_object_sha1(p, i, sha1))
			die("internal error pack-check nth-packed-object");
		if (!find_pack_entry_one(sha1, &e, p))
			die("internal error pack-check find-pack-entry-one");
		data = unpack_entry_gently(&e, type, &size);
		if (!data) {
			err = error("cannot unpack %s from %s",
				    sha1_to_hex(sha1), p->pack_name);
			continue;
		}
		if (check_sha1_signature(sha1, data, size, type)) {
			err = error("packed %s from %s is corrupt",
				    sha1_to_hex(sha1), p->pack_name);
			free(data);
			continue;
		}
		free(data);
	}

	return err;
}


static void show_pack_info(struct packed_git *p)
{
	struct pack_header *hdr;
	int nr_objects, i;

	hdr = p->pack_base;
	nr_objects = ntohl(hdr->hdr_entries);

	for (i = 0; i < nr_objects; i++) {
		unsigned char sha1[20], base_sha1[20];
		struct pack_entry e;
		char type[20];
		unsigned long size;
		unsigned long store_size;
		int delta_chain_length;

		if (nth_packed_object_sha1(p, i, sha1))
			die("internal error pack-check nth-packed-object");
		if (!find_pack_entry_one(sha1, &e, p))
			die("internal error pack-check find-pack-entry-one");

		packed_object_info_detail(&e, type, &size, &store_size,
					  &delta_chain_length,
					  base_sha1);
		printf("%s ", sha1_to_hex(sha1));
		if (!delta_chain_length)
			printf("%-6s %lu %u\n", type, size, e.offset);
		else
			printf("%-6s %lu %u %d %s\n", type, size, e.offset,
			       delta_chain_length, sha1_to_hex(base_sha1));
	}

}

int verify_pack(struct packed_git *p, int verbose)
{
	unsigned long index_size = p->index_size;
	void *index_base = p->index_base;
	SHA_CTX ctx;
	unsigned char sha1[20];
	int ret;

	ret = 0;
	/* Verify SHA1 sum of the index file */
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, index_base, index_size - 20);
	SHA1_Final(sha1, &ctx);
	if (memcmp(sha1, index_base + index_size - 20, 20))
		ret = error("Packfile index for %s SHA1 mismatch",
			    p->pack_name);

	if (!ret) {
		/* Verify pack file */
		use_packed_git(p);
		ret = verify_packfile(p);
		unuse_packed_git(p);
	}

	if (verbose) {
		if (ret)
			printf("%s: bad\n", p->pack_name);
		else {
			use_packed_git(p);
			show_pack_info(p);
			unuse_packed_git(p);
			printf("%s: ok\n", p->pack_name);
		}
	}

	return ret;
}
