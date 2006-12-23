#include "cache.h"
#include "pack.h"

static int verify_packfile(struct packed_git *p,
		struct pack_window **w_curs)
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
	pack_base = use_pack(p, w_curs, 0, NULL);
	hdr = (struct pack_header*)pack_base;
	if (hdr->hdr_signature != htonl(PACK_SIGNATURE))
		return error("Packfile %s signature mismatch", p->pack_name);
	if (!pack_version_ok(hdr->hdr_version))
		return error("Packfile version %d unsupported",
			     ntohl(hdr->hdr_version));
	nr_objects = ntohl(hdr->hdr_entries);
	if (num_packed_objects(p) != nr_objects)
		return error("Packfile claims to have %d objects, "
			     "while idx size expects %d", nr_objects,
			     num_packed_objects(p));

	SHA1_Init(&ctx);
	SHA1_Update(&ctx, pack_base, pack_size - 20);
	SHA1_Final(sha1, &ctx);
	if (hashcmp(sha1, (unsigned char *)pack_base + pack_size - 20))
		return error("Packfile %s SHA1 mismatch with itself",
			     p->pack_name);
	if (hashcmp(sha1, (unsigned char *)index_base + index_size - 40))
		return error("Packfile %s SHA1 mismatch with idx",
			     p->pack_name);

	/* Make sure everything reachable from idx is valid.  Since we
	 * have verified that nr_objects matches between idx and pack,
	 * we do not do scan-streaming check on the pack file.
	 */
	for (i = err = 0; i < nr_objects; i++) {
		unsigned char sha1[20];
		void *data;
		char type[20];
		unsigned long size, offset;

		if (nth_packed_object_sha1(p, i, sha1))
			die("internal error pack-check nth-packed-object");
		offset = find_pack_entry_one(sha1, p);
		if (!offset)
			die("internal error pack-check find-pack-entry-one");
		data = unpack_entry(p, offset, type, &size);
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


#define MAX_CHAIN 40

static void show_pack_info(struct packed_git *p,
		struct pack_window **w_curs)
{
	struct pack_header *hdr;
	int nr_objects, i;
	unsigned int chain_histogram[MAX_CHAIN];

	hdr = (struct pack_header*)use_pack(p, w_curs, 0, NULL);
	nr_objects = ntohl(hdr->hdr_entries);
	memset(chain_histogram, 0, sizeof(chain_histogram));

	for (i = 0; i < nr_objects; i++) {
		unsigned char sha1[20], base_sha1[20];
		char type[20];
		unsigned long size;
		unsigned long store_size;
		unsigned long offset;
		unsigned int delta_chain_length;

		if (nth_packed_object_sha1(p, i, sha1))
			die("internal error pack-check nth-packed-object");
		offset = find_pack_entry_one(sha1, p);
		if (!offset)
			die("internal error pack-check find-pack-entry-one");

		packed_object_info_detail(p, offset, type, &size, &store_size,
					  &delta_chain_length,
					  base_sha1);
		printf("%s ", sha1_to_hex(sha1));
		if (!delta_chain_length)
			printf("%-6s %lu %lu\n", type, size, offset);
		else {
			printf("%-6s %lu %lu %u %s\n", type, size, offset,
			       delta_chain_length, sha1_to_hex(base_sha1));
			if (delta_chain_length < MAX_CHAIN)
				chain_histogram[delta_chain_length]++;
			else
				chain_histogram[0]++;
		}
	}

	for (i = 0; i < MAX_CHAIN; i++) {
		if (!chain_histogram[i])
			continue;
		printf("chain length %s %d: %d object%s\n",
		       i ? "=" : ">=",
		       i ? i : MAX_CHAIN,
		       chain_histogram[i],
		       1 < chain_histogram[i] ? "s" : "");
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
	if (hashcmp(sha1, (unsigned char *)index_base + index_size - 20))
		ret = error("Packfile index for %s SHA1 mismatch",
			    p->pack_name);

	if (!ret) {
		/* Verify pack file */
		struct pack_window *w_curs = NULL;
		ret = verify_packfile(p, &w_curs);
		unuse_pack(&w_curs);
	}

	if (verbose) {
		if (ret)
			printf("%s: bad\n", p->pack_name);
		else {
			struct pack_window *w_curs = NULL;
			show_pack_info(p, &w_curs);
			unuse_pack(&w_curs);
			printf("%s: ok\n", p->pack_name);
		}
	}

	return ret;
}
