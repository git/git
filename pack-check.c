#include "cache.h"
#include "pack.h"

static int verify_packfile(struct packed_git *p,
		struct pack_window **w_curs)
{
	unsigned long index_size = p->index_size;
	void *index_base = p->index_base;
	SHA_CTX ctx;
	unsigned char sha1[20];
	unsigned long offset = 0, pack_sig = p->pack_size - 20;
	int nr_objects, err, i;

	/* Note that the pack header checks are actually performed by
	 * use_pack when it first opens the pack file.  If anything
	 * goes wrong during those checks then the call will die out
	 * immediately.
	 */

	SHA1_Init(&ctx);
	while (offset < pack_sig) {
		unsigned int remaining;
		unsigned char *in = use_pack(p, w_curs, offset, &remaining);
		offset += remaining;
		if (offset > pack_sig)
			remaining -= offset - pack_sig;
		SHA1_Update(&ctx, in, remaining);
	}
	SHA1_Final(sha1, &ctx);
	if (hashcmp(sha1, use_pack(p, w_curs, pack_sig, NULL)))
		return error("Packfile %s SHA1 mismatch with itself",
			     p->pack_name);
	if (hashcmp(sha1, (unsigned char *)index_base + index_size - 40))
		return error("Packfile %s SHA1 mismatch with idx",
			     p->pack_name);
	unuse_pack(w_curs);

	/* Make sure everything reachable from idx is valid.  Since we
	 * have verified that nr_objects matches between idx and pack,
	 * we do not do scan-streaming check on the pack file.
	 */
	nr_objects = num_packed_objects(p);
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

static void show_pack_info(struct packed_git *p)
{
	int nr_objects, i;
	unsigned int chain_histogram[MAX_CHAIN];

	nr_objects = num_packed_objects(p);
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
			show_pack_info(p);
			printf("%s: ok\n", p->pack_name);
		}
	}

	return ret;
}
