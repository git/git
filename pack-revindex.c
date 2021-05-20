#include "cache.h"
#include "pack-revindex.h"
#include "object-store.h"
#include "packfile.h"
#include "config.h"
#include "midx.h"

struct revindex_entry {
	off_t offset;
	unsigned int nr;
};

/*
 * Pack index for existing packs give us easy access to the offsets into
 * corresponding pack file where each object's data starts, but the entries
 * do not store the size of the compressed representation (uncompressed
 * size is easily available by examining the pack entry header).  It is
 * also rather expensive to find the sha1 for an object given its offset.
 *
 * The pack index file is sorted by object name mapping to offset;
 * this revindex array is a list of offset/index_nr pairs
 * ordered by offset, so if you know the offset of an object, next offset
 * is where its packed representation ends and the index_nr can be used to
 * get the object sha1 from the main index.
 */

/*
 * This is a least-significant-digit radix sort.
 *
 * It sorts each of the "n" items in "entries" by its offset field. The "max"
 * parameter must be at least as large as the largest offset in the array,
 * and lets us quit the sort early.
 */
static void sort_revindex(struct revindex_entry *entries, unsigned n, off_t max)
{
	/*
	 * We use a "digit" size of 16 bits. That keeps our memory
	 * usage reasonable, and we can generally (for a 4G or smaller
	 * packfile) quit after two rounds of radix-sorting.
	 */
#define DIGIT_SIZE (16)
#define BUCKETS (1 << DIGIT_SIZE)
	/*
	 * We want to know the bucket that a[i] will go into when we are using
	 * the digit that is N bits from the (least significant) end.
	 */
#define BUCKET_FOR(a, i, bits) (((a)[(i)].offset >> (bits)) & (BUCKETS-1))

	/*
	 * We need O(n) temporary storage. Rather than do an extra copy of the
	 * partial results into "entries", we sort back and forth between the
	 * real array and temporary storage. In each iteration of the loop, we
	 * keep track of them with alias pointers, always sorting from "from"
	 * to "to".
	 */
	struct revindex_entry *tmp, *from, *to;
	int bits;
	unsigned *pos;

	ALLOC_ARRAY(pos, BUCKETS);
	ALLOC_ARRAY(tmp, n);
	from = entries;
	to = tmp;

	/*
	 * If (max >> bits) is zero, then we know that the radix digit we are
	 * on (and any higher) will be zero for all entries, and our loop will
	 * be a no-op, as everybody lands in the same zero-th bucket.
	 */
	for (bits = 0; max >> bits; bits += DIGIT_SIZE) {
		unsigned i;

		memset(pos, 0, BUCKETS * sizeof(*pos));

		/*
		 * We want pos[i] to store the index of the last element that
		 * will go in bucket "i" (actually one past the last element).
		 * To do this, we first count the items that will go in each
		 * bucket, which gives us a relative offset from the last
		 * bucket. We can then cumulatively add the index from the
		 * previous bucket to get the true index.
		 */
		for (i = 0; i < n; i++)
			pos[BUCKET_FOR(from, i, bits)]++;
		for (i = 1; i < BUCKETS; i++)
			pos[i] += pos[i-1];

		/*
		 * Now we can drop the elements into their correct buckets (in
		 * our temporary array).  We iterate the pos counter backwards
		 * to avoid using an extra index to count up. And since we are
		 * going backwards there, we must also go backwards through the
		 * array itself, to keep the sort stable.
		 *
		 * Note that we use an unsigned iterator to make sure we can
		 * handle 2^32-1 objects, even on a 32-bit system. But this
		 * means we cannot use the more obvious "i >= 0" loop condition
		 * for counting backwards, and must instead check for
		 * wrap-around with UINT_MAX.
		 */
		for (i = n - 1; i != UINT_MAX; i--)
			to[--pos[BUCKET_FOR(from, i, bits)]] = from[i];

		/*
		 * Now "to" contains the most sorted list, so we swap "from" and
		 * "to" for the next iteration.
		 */
		SWAP(from, to);
	}

	/*
	 * If we ended with our data in the original array, great. If not,
	 * we have to move it back from the temporary storage.
	 */
	if (from != entries)
		COPY_ARRAY(entries, tmp, n);
	free(tmp);
	free(pos);

#undef BUCKET_FOR
#undef BUCKETS
#undef DIGIT_SIZE
}

/*
 * Ordered list of offsets of objects in the pack.
 */
static void create_pack_revindex(struct packed_git *p)
{
	const unsigned num_ent = p->num_objects;
	unsigned i;
	const char *index = p->index_data;
	const unsigned hashsz = the_hash_algo->rawsz;

	ALLOC_ARRAY(p->revindex, num_ent + 1);
	index += 4 * 256;

	if (p->index_version > 1) {
		const uint32_t *off_32 =
			(uint32_t *)(index + 8 + (size_t)p->num_objects * (hashsz + 4));
		const uint32_t *off_64 = off_32 + p->num_objects;
		for (i = 0; i < num_ent; i++) {
			const uint32_t off = ntohl(*off_32++);
			if (!(off & 0x80000000)) {
				p->revindex[i].offset = off;
			} else {
				p->revindex[i].offset = get_be64(off_64);
				off_64 += 2;
			}
			p->revindex[i].nr = i;
		}
	} else {
		for (i = 0; i < num_ent; i++) {
			const uint32_t hl = *((uint32_t *)(index + (hashsz + 4) * i));
			p->revindex[i].offset = ntohl(hl);
			p->revindex[i].nr = i;
		}
	}

	/*
	 * This knows the pack format -- the hash trailer
	 * follows immediately after the last object data.
	 */
	p->revindex[num_ent].offset = p->pack_size - hashsz;
	p->revindex[num_ent].nr = -1;
	sort_revindex(p->revindex, num_ent, p->pack_size);
}

static int create_pack_revindex_in_memory(struct packed_git *p)
{
	if (git_env_bool(GIT_TEST_REV_INDEX_DIE_IN_MEMORY, 0))
		die("dying as requested by '%s'",
		    GIT_TEST_REV_INDEX_DIE_IN_MEMORY);
	if (open_pack_index(p))
		return -1;
	create_pack_revindex(p);
	return 0;
}

static char *pack_revindex_filename(struct packed_git *p)
{
	size_t len;
	if (!strip_suffix(p->pack_name, ".pack", &len))
		BUG("pack_name does not end in .pack");
	return xstrfmt("%.*s.rev", (int)len, p->pack_name);
}

#define RIDX_HEADER_SIZE (12)
#define RIDX_MIN_SIZE (RIDX_HEADER_SIZE + (2 * the_hash_algo->rawsz))

struct revindex_header {
	uint32_t signature;
	uint32_t version;
	uint32_t hash_id;
};

static int load_revindex_from_disk(char *revindex_name,
				   uint32_t num_objects,
				   const uint32_t **data_p, size_t *len_p)
{
	int fd, ret = 0;
	struct stat st;
	void *data = NULL;
	size_t revindex_size;
	struct revindex_header *hdr;

	fd = git_open(revindex_name);

	if (fd < 0) {
		ret = -1;
		goto cleanup;
	}
	if (fstat(fd, &st)) {
		ret = error_errno(_("failed to read %s"), revindex_name);
		goto cleanup;
	}

	revindex_size = xsize_t(st.st_size);

	if (revindex_size < RIDX_MIN_SIZE) {
		ret = error(_("reverse-index file %s is too small"), revindex_name);
		goto cleanup;
	}

	if (revindex_size - RIDX_MIN_SIZE != st_mult(sizeof(uint32_t), num_objects)) {
		ret = error(_("reverse-index file %s is corrupt"), revindex_name);
		goto cleanup;
	}

	data = xmmap(NULL, revindex_size, PROT_READ, MAP_PRIVATE, fd, 0);
	hdr = data;

	if (ntohl(hdr->signature) != RIDX_SIGNATURE) {
		ret = error(_("reverse-index file %s has unknown signature"), revindex_name);
		goto cleanup;
	}
	if (ntohl(hdr->version) != 1) {
		ret = error(_("reverse-index file %s has unsupported version %"PRIu32),
			    revindex_name, ntohl(hdr->version));
		goto cleanup;
	}
	if (!(ntohl(hdr->hash_id) == 1 || ntohl(hdr->hash_id) == 2)) {
		ret = error(_("reverse-index file %s has unsupported hash id %"PRIu32),
			    revindex_name, ntohl(hdr->hash_id));
		goto cleanup;
	}

cleanup:
	if (ret) {
		if (data)
			munmap(data, revindex_size);
	} else {
		*len_p = revindex_size;
		*data_p = (const uint32_t *)data;
	}

	if (fd >= 0)
		close(fd);
	return ret;
}

static int load_pack_revindex_from_disk(struct packed_git *p)
{
	char *revindex_name;
	int ret;
	if (open_pack_index(p))
		return -1;

	revindex_name = pack_revindex_filename(p);

	ret = load_revindex_from_disk(revindex_name,
				      p->num_objects,
				      &p->revindex_map,
				      &p->revindex_size);
	if (ret)
		goto cleanup;

	p->revindex_data = (const uint32_t *)((const char *)p->revindex_map + RIDX_HEADER_SIZE);

cleanup:
	free(revindex_name);
	return ret;
}

int load_pack_revindex(struct packed_git *p)
{
	if (p->revindex || p->revindex_data)
		return 0;

	if (!load_pack_revindex_from_disk(p))
		return 0;
	else if (!create_pack_revindex_in_memory(p))
		return 0;
	return -1;
}

int load_midx_revindex(struct multi_pack_index *m)
{
	char *revindex_name;
	int ret;
	if (m->revindex_data)
		return 0;

	revindex_name = get_midx_rev_filename(m);

	ret = load_revindex_from_disk(revindex_name,
				      m->num_objects,
				      &m->revindex_map,
				      &m->revindex_len);
	if (ret)
		goto cleanup;

	m->revindex_data = (const uint32_t *)((const char *)m->revindex_map + RIDX_HEADER_SIZE);

cleanup:
	free(revindex_name);
	return ret;
}

int close_midx_revindex(struct multi_pack_index *m)
{
	if (!m || !m->revindex_map)
		return 0;

	munmap((void*)m->revindex_map, m->revindex_len);

	m->revindex_map = NULL;
	m->revindex_data = NULL;
	m->revindex_len = 0;

	return 0;
}

int offset_to_pack_pos(struct packed_git *p, off_t ofs, uint32_t *pos)
{
	unsigned lo, hi;

	if (load_pack_revindex(p) < 0)
		return -1;

	lo = 0;
	hi = p->num_objects + 1;

	do {
		const unsigned mi = lo + (hi - lo) / 2;
		off_t got = pack_pos_to_offset(p, mi);

		if (got == ofs) {
			*pos = mi;
			return 0;
		} else if (ofs < got)
			hi = mi;
		else
			lo = mi + 1;
	} while (lo < hi);

	error("bad offset for revindex");
	return -1;
}

uint32_t pack_pos_to_index(struct packed_git *p, uint32_t pos)
{
	if (!(p->revindex || p->revindex_data))
		BUG("pack_pos_to_index: reverse index not yet loaded");
	if (p->num_objects <= pos)
		BUG("pack_pos_to_index: out-of-bounds object at %"PRIu32, pos);

	if (p->revindex)
		return p->revindex[pos].nr;
	else
		return get_be32(p->revindex_data + pos);
}

off_t pack_pos_to_offset(struct packed_git *p, uint32_t pos)
{
	if (!(p->revindex || p->revindex_data))
		BUG("pack_pos_to_index: reverse index not yet loaded");
	if (p->num_objects < pos)
		BUG("pack_pos_to_offset: out-of-bounds object at %"PRIu32, pos);

	if (p->revindex)
		return p->revindex[pos].offset;
	else if (pos == p->num_objects)
		return p->pack_size - the_hash_algo->rawsz;
	else
		return nth_packed_object_offset(p, pack_pos_to_index(p, pos));
}

uint32_t pack_pos_to_midx(struct multi_pack_index *m, uint32_t pos)
{
	if (!m->revindex_data)
		BUG("pack_pos_to_midx: reverse index not yet loaded");
	if (m->num_objects <= pos)
		BUG("pack_pos_to_midx: out-of-bounds object at %"PRIu32, pos);
	return get_be32(m->revindex_data + pos);
}

struct midx_pack_key {
	uint32_t pack;
	off_t offset;

	uint32_t preferred_pack;
	struct multi_pack_index *midx;
};

static int midx_pack_order_cmp(const void *va, const void *vb)
{
	const struct midx_pack_key *key = va;
	struct multi_pack_index *midx = key->midx;

	uint32_t versus = pack_pos_to_midx(midx, (uint32_t*)vb - (const uint32_t *)midx->revindex_data);
	uint32_t versus_pack = nth_midxed_pack_int_id(midx, versus);
	off_t versus_offset;

	uint32_t key_preferred = key->pack == key->preferred_pack;
	uint32_t versus_preferred = versus_pack == key->preferred_pack;

	/*
	 * First, compare the preferred-ness, noting that the preferred pack
	 * comes first.
	 */
	if (key_preferred && !versus_preferred)
		return -1;
	else if (!key_preferred && versus_preferred)
		return 1;

	/* Then, break ties first by comparing the pack IDs. */
	if (key->pack < versus_pack)
		return -1;
	else if (key->pack > versus_pack)
		return 1;

	/* Finally, break ties by comparing offsets within a pack. */
	versus_offset = nth_midxed_offset(midx, versus);
	if (key->offset < versus_offset)
		return -1;
	else if (key->offset > versus_offset)
		return 1;

	return 0;
}

int midx_to_pack_pos(struct multi_pack_index *m, uint32_t at, uint32_t *pos)
{
	struct midx_pack_key key;
	uint32_t *found;

	if (!m->revindex_data)
		BUG("midx_to_pack_pos: reverse index not yet loaded");
	if (m->num_objects <= at)
		BUG("midx_to_pack_pos: out-of-bounds object at %"PRIu32, at);

	key.pack = nth_midxed_pack_int_id(m, at);
	key.offset = nth_midxed_offset(m, at);
	key.midx = m;
	/*
	 * The preferred pack sorts first, so determine its identifier by
	 * looking at the first object in pseudo-pack order.
	 *
	 * Note that if no --preferred-pack is explicitly given when writing a
	 * multi-pack index, then whichever pack has the lowest identifier
	 * implicitly is preferred (and includes all its objects, since ties are
	 * broken first by pack identifier).
	 */
	key.preferred_pack = nth_midxed_pack_int_id(m, pack_pos_to_midx(m, 0));

	found = bsearch(&key, m->revindex_data, m->num_objects,
			sizeof(*m->revindex_data), midx_pack_order_cmp);

	if (!found)
		return error("bad offset for revindex");

	*pos = found - m->revindex_data;
	return 0;
}
