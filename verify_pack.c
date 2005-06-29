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
	int nr_objects;

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
	return 0;
}


int verify_pack(struct packed_git *p)
{
	unsigned long index_size = p->index_size;
	void *index_base = p->index_base;
	SHA_CTX ctx;
	unsigned char sha1[20];
	int ret;

	/* Verify SHA1 sum of the index file */
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, index_base, index_size - 20);
	SHA1_Final(sha1, &ctx);
	if (memcmp(sha1, index_base + index_size - 20, 20))
		return error("Packfile index for %s SHA1 mismatch",
			     p->pack_name);

	/* Verify pack file */
	use_packed_git(p);
	ret = verify_packfile(p);
	unuse_packed_git(p);
	return ret;
}
