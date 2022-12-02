#include "cache.h"
#include "commit.h"
#include "strbuf.h"
#include "tag.h"
#include "diff.h"
#include "revision.h"
#include "progress.h"
#include "list-objects.h"
#include "pack.h"
#include "pack-bitmap.h"
#include "pack-revindex.h"
#include "pack-objects.h"
#include "packfile.h"
#include "repository.h"
#include "object-store.h"
#include "list-objects-filter-options.h"
#include "midx.h"
#include "config.h"

/*
 * An entry on the bitmap index, representing the bitmap for a given
 * commit.
 */
struct stored_bitmap {
	struct object_id oid;
	struct ewah_bitmap *root;
	struct stored_bitmap *xor;
	int flags;
};

/*
 * The active bitmap index for a repository. By design, repositories only have
 * a single bitmap index available (the index for the biggest packfile in
 * the repository), since bitmap indexes need full closure.
 *
 * If there is more than one bitmap index available (e.g. because of alternates),
 * the active bitmap index is the largest one.
 */
struct bitmap_index {
	/*
	 * The pack or multi-pack index (MIDX) that this bitmap index belongs
	 * to.
	 *
	 * Exactly one of these must be non-NULL; this specifies the object
	 * order used to interpret this bitmap.
	 */
	struct packed_git *pack;
	struct multi_pack_index *midx;

	/*
	 * Mark the first `reuse_objects` in the packfile as reused:
	 * they will be sent as-is without using them for repacking
	 * calculations
	 */
	uint32_t reuse_objects;

	/* mmapped buffer of the whole bitmap index */
	unsigned char *map;
	size_t map_size; /* size of the mmaped buffer */
	size_t map_pos; /* current position when loading the index */

	/*
	 * Type indexes.
	 *
	 * Each bitmap marks which objects in the packfile  are of the given
	 * type. This provides type information when yielding the objects from
	 * the packfile during a walk, which allows for better delta bases.
	 */
	struct ewah_bitmap *commits;
	struct ewah_bitmap *trees;
	struct ewah_bitmap *blobs;
	struct ewah_bitmap *tags;

	/* Map from object ID -> `stored_bitmap` for all the bitmapped commits */
	kh_oid_map_t *bitmaps;

	/* Number of bitmapped commits */
	uint32_t entry_count;

	/* If not NULL, this is a name-hash cache pointing into map. */
	uint32_t *hashes;

	/* The checksum of the packfile or MIDX; points into map. */
	const unsigned char *checksum;

	/*
	 * If not NULL, this point into the commit table extension
	 * (within the memory mapped region `map`).
	 */
	unsigned char *table_lookup;

	/*
	 * Extended index.
	 *
	 * When trying to perform bitmap operations with objects that are not
	 * packed in `pack`, these objects are added to this "fake index" and
	 * are assumed to appear at the end of the packfile for all operations
	 */
	struct eindex {
		struct object **objects;
		uint32_t *hashes;
		uint32_t count, alloc;
		kh_oid_pos_t *positions;
	} ext_index;

	/* Bitmap result of the last performed walk */
	struct bitmap *result;

	/* "have" bitmap from the last performed walk */
	struct bitmap *haves;

	/* Version of the bitmap index */
	unsigned int version;
};

static struct ewah_bitmap *lookup_stored_bitmap(struct stored_bitmap *st)
{
	struct ewah_bitmap *parent;
	struct ewah_bitmap *composed;

	if (!st->xor)
		return st->root;

	composed = ewah_pool_new();
	parent = lookup_stored_bitmap(st->xor);
	ewah_xor(st->root, parent, composed);

	ewah_pool_free(st->root);
	st->root = composed;
	st->xor = NULL;

	return composed;
}

/*
 * Read a bitmap from the current read position on the mmaped
 * index, and increase the read position accordingly
 */
static struct ewah_bitmap *read_bitmap_1(struct bitmap_index *index)
{
	struct ewah_bitmap *b = ewah_pool_new();

	ssize_t bitmap_size = ewah_read_mmap(b,
		index->map + index->map_pos,
		index->map_size - index->map_pos);

	if (bitmap_size < 0) {
		error(_("failed to load bitmap index (corrupted?)"));
		ewah_pool_free(b);
		return NULL;
	}

	index->map_pos += bitmap_size;
	return b;
}

static uint32_t bitmap_num_objects(struct bitmap_index *index)
{
	if (index->midx)
		return index->midx->num_objects;
	return index->pack->num_objects;
}

static int load_bitmap_header(struct bitmap_index *index)
{
	struct bitmap_disk_header *header = (void *)index->map;
	size_t header_size = sizeof(*header) - GIT_MAX_RAWSZ + the_hash_algo->rawsz;

	if (index->map_size < header_size + the_hash_algo->rawsz)
		return error(_("corrupted bitmap index (too small)"));

	if (memcmp(header->magic, BITMAP_IDX_SIGNATURE, sizeof(BITMAP_IDX_SIGNATURE)) != 0)
		return error(_("corrupted bitmap index file (wrong header)"));

	index->version = ntohs(header->version);
	if (index->version != 1)
		return error(_("unsupported version '%d' for bitmap index file"), index->version);

	/* Parse known bitmap format options */
	{
		uint32_t flags = ntohs(header->options);
		size_t cache_size = st_mult(bitmap_num_objects(index), sizeof(uint32_t));
		unsigned char *index_end = index->map + index->map_size - the_hash_algo->rawsz;

		if ((flags & BITMAP_OPT_FULL_DAG) == 0)
			BUG("unsupported options for bitmap index file "
				"(Git requires BITMAP_OPT_FULL_DAG)");

		if (flags & BITMAP_OPT_HASH_CACHE) {
			if (cache_size > index_end - index->map - header_size)
				return error(_("corrupted bitmap index file (too short to fit hash cache)"));
			index->hashes = (void *)(index_end - cache_size);
			index_end -= cache_size;
		}

		if (flags & BITMAP_OPT_LOOKUP_TABLE) {
			size_t table_size = st_mult(ntohl(header->entry_count),
						    BITMAP_LOOKUP_TABLE_TRIPLET_WIDTH);
			if (table_size > index_end - index->map - header_size)
				return error(_("corrupted bitmap index file (too short to fit lookup table)"));
			if (git_env_bool("GIT_TEST_READ_COMMIT_TABLE", 1))
				index->table_lookup = (void *)(index_end - table_size);
			index_end -= table_size;
		}
	}

	index->entry_count = ntohl(header->entry_count);
	index->checksum = header->checksum;
	index->map_pos += header_size;
	return 0;
}

static struct stored_bitmap *store_bitmap(struct bitmap_index *index,
					  struct ewah_bitmap *root,
					  const struct object_id *oid,
					  struct stored_bitmap *xor_with,
					  int flags)
{
	struct stored_bitmap *stored;
	khiter_t hash_pos;
	int ret;

	stored = xmalloc(sizeof(struct stored_bitmap));
	stored->root = root;
	stored->xor = xor_with;
	stored->flags = flags;
	oidcpy(&stored->oid, oid);

	hash_pos = kh_put_oid_map(index->bitmaps, stored->oid, &ret);

	/*
	 * A 0 return code means the insertion succeeded with no changes,
	 * because the SHA1 already existed on the map. This is bad, there
	 * shouldn't be duplicated commits in the index.
	 */
	if (ret == 0) {
		error(_("duplicate entry in bitmap index: '%s'"), oid_to_hex(oid));
		return NULL;
	}

	kh_value(index->bitmaps, hash_pos) = stored;
	return stored;
}

static inline uint32_t read_be32(const unsigned char *buffer, size_t *pos)
{
	uint32_t result = get_be32(buffer + *pos);
	(*pos) += sizeof(result);
	return result;
}

static inline uint8_t read_u8(const unsigned char *buffer, size_t *pos)
{
	return buffer[(*pos)++];
}

#define MAX_XOR_OFFSET 160

static int nth_bitmap_object_oid(struct bitmap_index *index,
				 struct object_id *oid,
				 uint32_t n)
{
	if (index->midx)
		return nth_midxed_object_oid(oid, index->midx, n) ? 0 : -1;
	return nth_packed_object_id(oid, index->pack, n);
}

static int load_bitmap_entries_v1(struct bitmap_index *index)
{
	uint32_t i;
	struct stored_bitmap *recent_bitmaps[MAX_XOR_OFFSET] = { NULL };

	for (i = 0; i < index->entry_count; ++i) {
		int xor_offset, flags;
		struct ewah_bitmap *bitmap = NULL;
		struct stored_bitmap *xor_bitmap = NULL;
		uint32_t commit_idx_pos;
		struct object_id oid;

		if (index->map_size - index->map_pos < 6)
			return error(_("corrupt ewah bitmap: truncated header for entry %d"), i);

		commit_idx_pos = read_be32(index->map, &index->map_pos);
		xor_offset = read_u8(index->map, &index->map_pos);
		flags = read_u8(index->map, &index->map_pos);

		if (nth_bitmap_object_oid(index, &oid, commit_idx_pos) < 0)
			return error(_("corrupt ewah bitmap: commit index %u out of range"),
				     (unsigned)commit_idx_pos);

		bitmap = read_bitmap_1(index);
		if (!bitmap)
			return -1;

		if (xor_offset > MAX_XOR_OFFSET || xor_offset > i)
			return error(_("corrupted bitmap pack index"));

		if (xor_offset > 0) {
			xor_bitmap = recent_bitmaps[(i - xor_offset) % MAX_XOR_OFFSET];

			if (!xor_bitmap)
				return error(_("invalid XOR offset in bitmap pack index"));
		}

		recent_bitmaps[i % MAX_XOR_OFFSET] = store_bitmap(
			index, bitmap, &oid, xor_bitmap, flags);
	}

	return 0;
}

char *midx_bitmap_filename(struct multi_pack_index *midx)
{
	struct strbuf buf = STRBUF_INIT;

	get_midx_filename(&buf, midx->object_dir);
	strbuf_addf(&buf, "-%s.bitmap", hash_to_hex(get_midx_checksum(midx)));

	return strbuf_detach(&buf, NULL);
}

char *pack_bitmap_filename(struct packed_git *p)
{
	size_t len;

	if (!strip_suffix(p->pack_name, ".pack", &len))
		BUG("pack_name does not end in .pack");
	return xstrfmt("%.*s.bitmap", (int)len, p->pack_name);
}

static int open_midx_bitmap_1(struct bitmap_index *bitmap_git,
			      struct multi_pack_index *midx)
{
	struct stat st;
	char *bitmap_name = midx_bitmap_filename(midx);
	int fd = git_open(bitmap_name);
	uint32_t i;
	struct packed_git *preferred;

	if (fd < 0) {
		if (errno != ENOENT)
			warning_errno("cannot open '%s'", bitmap_name);
		free(bitmap_name);
		return -1;
	}
	free(bitmap_name);

	if (fstat(fd, &st)) {
		error_errno(_("cannot fstat bitmap file"));
		close(fd);
		return -1;
	}

	if (bitmap_git->pack || bitmap_git->midx) {
		struct strbuf buf = STRBUF_INIT;
		get_midx_filename(&buf, midx->object_dir);
		trace2_data_string("bitmap", the_repository,
				   "ignoring extra midx bitmap file", buf.buf);
		close(fd);
		strbuf_release(&buf);
		return -1;
	}

	bitmap_git->midx = midx;
	bitmap_git->map_size = xsize_t(st.st_size);
	bitmap_git->map_pos = 0;
	bitmap_git->map = xmmap(NULL, bitmap_git->map_size, PROT_READ,
				MAP_PRIVATE, fd, 0);
	close(fd);

	if (load_bitmap_header(bitmap_git) < 0)
		goto cleanup;

	if (!hasheq(get_midx_checksum(bitmap_git->midx), bitmap_git->checksum)) {
		error(_("checksum doesn't match in MIDX and bitmap"));
		goto cleanup;
	}

	if (load_midx_revindex(bitmap_git->midx) < 0) {
		warning(_("multi-pack bitmap is missing required reverse index"));
		goto cleanup;
	}

	for (i = 0; i < bitmap_git->midx->num_packs; i++) {
		if (prepare_midx_pack(the_repository, bitmap_git->midx, i))
			die(_("could not open pack %s"),
			    bitmap_git->midx->pack_names[i]);
	}

	preferred = bitmap_git->midx->packs[midx_preferred_pack(bitmap_git)];
	if (!is_pack_valid(preferred)) {
		warning(_("preferred pack (%s) is invalid"),
			preferred->pack_name);
		goto cleanup;
	}

	return 0;

cleanup:
	munmap(bitmap_git->map, bitmap_git->map_size);
	bitmap_git->map_size = 0;
	bitmap_git->map_pos = 0;
	bitmap_git->map = NULL;
	bitmap_git->midx = NULL;
	return -1;
}

static int open_pack_bitmap_1(struct bitmap_index *bitmap_git, struct packed_git *packfile)
{
	int fd;
	struct stat st;
	char *bitmap_name;

	bitmap_name = pack_bitmap_filename(packfile);
	fd = git_open(bitmap_name);

	if (fd < 0) {
		if (errno != ENOENT)
			warning_errno("cannot open '%s'", bitmap_name);
		free(bitmap_name);
		return -1;
	}
	free(bitmap_name);

	if (fstat(fd, &st)) {
		error_errno(_("cannot fstat bitmap file"));
		close(fd);
		return -1;
	}

	if (bitmap_git->pack || bitmap_git->midx) {
		trace2_data_string("bitmap", the_repository,
				   "ignoring extra bitmap file", packfile->pack_name);
		close(fd);
		return -1;
	}

	if (!is_pack_valid(packfile)) {
		close(fd);
		return -1;
	}

	bitmap_git->pack = packfile;
	bitmap_git->map_size = xsize_t(st.st_size);
	bitmap_git->map = xmmap(NULL, bitmap_git->map_size, PROT_READ, MAP_PRIVATE, fd, 0);
	bitmap_git->map_pos = 0;
	close(fd);

	if (load_bitmap_header(bitmap_git) < 0) {
		munmap(bitmap_git->map, bitmap_git->map_size);
		bitmap_git->map = NULL;
		bitmap_git->map_size = 0;
		bitmap_git->map_pos = 0;
		bitmap_git->pack = NULL;
		return -1;
	}

	trace2_data_string("bitmap", the_repository, "opened bitmap file",
			   packfile->pack_name);
	return 0;
}

static int load_reverse_index(struct bitmap_index *bitmap_git)
{
	if (bitmap_is_midx(bitmap_git)) {
		uint32_t i;
		int ret;

		/*
		 * The multi-pack-index's .rev file is already loaded via
		 * open_pack_bitmap_1().
		 *
		 * But we still need to open the individual pack .rev files,
		 * since we will need to make use of them in pack-objects.
		 */
		for (i = 0; i < bitmap_git->midx->num_packs; i++) {
			ret = load_pack_revindex(bitmap_git->midx->packs[i]);
			if (ret)
				return ret;
		}
		return 0;
	}
	return load_pack_revindex(bitmap_git->pack);
}

static int load_bitmap(struct bitmap_index *bitmap_git)
{
	assert(bitmap_git->map);

	bitmap_git->bitmaps = kh_init_oid_map();
	bitmap_git->ext_index.positions = kh_init_oid_pos();

	if (load_reverse_index(bitmap_git))
		goto failed;

	if (!(bitmap_git->commits = read_bitmap_1(bitmap_git)) ||
		!(bitmap_git->trees = read_bitmap_1(bitmap_git)) ||
		!(bitmap_git->blobs = read_bitmap_1(bitmap_git)) ||
		!(bitmap_git->tags = read_bitmap_1(bitmap_git)))
		goto failed;

	if (!bitmap_git->table_lookup && load_bitmap_entries_v1(bitmap_git) < 0)
		goto failed;

	return 0;

failed:
	munmap(bitmap_git->map, bitmap_git->map_size);
	bitmap_git->map = NULL;
	bitmap_git->map_size = 0;

	kh_destroy_oid_map(bitmap_git->bitmaps);
	bitmap_git->bitmaps = NULL;

	kh_destroy_oid_pos(bitmap_git->ext_index.positions);
	bitmap_git->ext_index.positions = NULL;

	return -1;
}

static int open_pack_bitmap(struct repository *r,
			    struct bitmap_index *bitmap_git)
{
	struct packed_git *p;
	int ret = -1;

	for (p = get_all_packs(r); p; p = p->next) {
		if (open_pack_bitmap_1(bitmap_git, p) == 0) {
			ret = 0;
			/*
			 * The only reason to keep looking is to report
			 * duplicates.
			 */
			if (!trace2_is_enabled())
				break;
		}
	}

	return ret;
}

static int open_midx_bitmap(struct repository *r,
			    struct bitmap_index *bitmap_git)
{
	int ret = -1;
	struct multi_pack_index *midx;

	assert(!bitmap_git->map);

	for (midx = get_multi_pack_index(r); midx; midx = midx->next) {
		if (!open_midx_bitmap_1(bitmap_git, midx))
			ret = 0;
	}
	return ret;
}

static int open_bitmap(struct repository *r,
		       struct bitmap_index *bitmap_git)
{
	int found;

	assert(!bitmap_git->map);

	found = !open_midx_bitmap(r, bitmap_git);

	/*
	 * these will all be skipped if we opened a midx bitmap; but run it
	 * anyway if tracing is enabled to report the duplicates
	 */
	if (!found || trace2_is_enabled())
		found |= !open_pack_bitmap(r, bitmap_git);

	return found ? 0 : -1;
}

struct bitmap_index *prepare_bitmap_git(struct repository *r)
{
	struct bitmap_index *bitmap_git = xcalloc(1, sizeof(*bitmap_git));

	if (!open_bitmap(r, bitmap_git) && !load_bitmap(bitmap_git))
		return bitmap_git;

	free_bitmap_index(bitmap_git);
	return NULL;
}

struct bitmap_index *prepare_midx_bitmap_git(struct multi_pack_index *midx)
{
	struct bitmap_index *bitmap_git = xcalloc(1, sizeof(*bitmap_git));

	if (!open_midx_bitmap_1(bitmap_git, midx) && !load_bitmap(bitmap_git))
		return bitmap_git;

	free_bitmap_index(bitmap_git);
	return NULL;
}

struct include_data {
	struct bitmap_index *bitmap_git;
	struct bitmap *base;
	struct bitmap *seen;
};

struct bitmap_lookup_table_triplet {
	uint32_t commit_pos;
	uint64_t offset;
	uint32_t xor_row;
};

struct bitmap_lookup_table_xor_item {
	struct object_id oid;
	uint64_t offset;
};

/*
 * Given a `triplet` struct pointer and pointer `p`, this
 * function reads the triplet beginning at `p` into the struct.
 * Note that this function assumes that there is enough memory
 * left for filling the `triplet` struct from `p`.
 */
static int bitmap_lookup_table_get_triplet_by_pointer(struct bitmap_lookup_table_triplet *triplet,
						      const unsigned char *p)
{
	if (!triplet)
		return -1;

	triplet->commit_pos = get_be32(p);
	p += sizeof(uint32_t);
	triplet->offset = get_be64(p);
	p += sizeof(uint64_t);
	triplet->xor_row = get_be32(p);
	return 0;
}

/*
 * This function gets the raw triplet from `row`'th row in the
 * lookup table and fills that data to the `triplet`.
 */
static int bitmap_lookup_table_get_triplet(struct bitmap_index *bitmap_git,
					   uint32_t pos,
					   struct bitmap_lookup_table_triplet *triplet)
{
	unsigned char *p = NULL;
	if (pos >= bitmap_git->entry_count)
		return error(_("corrupt bitmap lookup table: triplet position out of index"));

	p = bitmap_git->table_lookup + st_mult(pos, BITMAP_LOOKUP_TABLE_TRIPLET_WIDTH);

	return bitmap_lookup_table_get_triplet_by_pointer(triplet, p);
}

/*
 * Searches for a matching triplet. `commit_pos` is a pointer
 * to the wanted commit position value. `table_entry` points to
 * a triplet in lookup table. The first 4 bytes of each
 * triplet (pointed by `table_entry`) are compared with `*commit_pos`.
 */
static int triplet_cmp(const void *commit_pos, const void *table_entry)
{

	uint32_t a = *(uint32_t *)commit_pos;
	uint32_t b = get_be32(table_entry);
	if (a > b)
		return 1;
	else if (a < b)
		return -1;

	return 0;
}

static uint32_t bitmap_bsearch_pos(struct bitmap_index *bitmap_git,
			    struct object_id *oid,
			    uint32_t *result)
{
	int found;

	if (bitmap_is_midx(bitmap_git))
		found = bsearch_midx(oid, bitmap_git->midx, result);
	else
		found = bsearch_pack(oid, bitmap_git->pack, result);

	return found;
}

/*
 * `bsearch_triplet_by_pos` function searches for the raw triplet
 * having commit position same as `commit_pos` and fills `triplet`
 * object from the raw triplet. Returns 1 on success and 0 on
 * failure.
 */
static int bitmap_bsearch_triplet_by_pos(uint32_t commit_pos,
				  struct bitmap_index *bitmap_git,
				  struct bitmap_lookup_table_triplet *triplet)
{
	unsigned char *p = bsearch(&commit_pos, bitmap_git->table_lookup, bitmap_git->entry_count,
				   BITMAP_LOOKUP_TABLE_TRIPLET_WIDTH, triplet_cmp);

	if (!p)
		return -1;

	return bitmap_lookup_table_get_triplet_by_pointer(triplet, p);
}

static struct stored_bitmap *lazy_bitmap_for_commit(struct bitmap_index *bitmap_git,
						    struct commit *commit)
{
	uint32_t commit_pos, xor_row;
	uint64_t offset;
	int flags;
	struct bitmap_lookup_table_triplet triplet;
	struct object_id *oid = &commit->object.oid;
	struct ewah_bitmap *bitmap;
	struct stored_bitmap *xor_bitmap = NULL;
	const int bitmap_header_size = 6;
	static struct bitmap_lookup_table_xor_item *xor_items = NULL;
	static size_t xor_items_nr = 0, xor_items_alloc = 0;
	static int is_corrupt = 0;
	int xor_flags;
	khiter_t hash_pos;
	struct bitmap_lookup_table_xor_item *xor_item;

	if (is_corrupt)
		return NULL;

	if (!bitmap_bsearch_pos(bitmap_git, oid, &commit_pos))
		return NULL;

	if (bitmap_bsearch_triplet_by_pos(commit_pos, bitmap_git, &triplet) < 0)
		return NULL;

	xor_items_nr = 0;
	offset = triplet.offset;
	xor_row = triplet.xor_row;

	while (xor_row != 0xffffffff) {
		ALLOC_GROW(xor_items, xor_items_nr + 1, xor_items_alloc);

		if (xor_items_nr + 1 >= bitmap_git->entry_count) {
			error(_("corrupt bitmap lookup table: xor chain exceeds entry count"));
			goto corrupt;
		}

		if (bitmap_lookup_table_get_triplet(bitmap_git, xor_row, &triplet) < 0)
			goto corrupt;

		xor_item = &xor_items[xor_items_nr];
		xor_item->offset = triplet.offset;

		if (nth_bitmap_object_oid(bitmap_git, &xor_item->oid, triplet.commit_pos) < 0) {
			error(_("corrupt bitmap lookup table: commit index %u out of range"),
				triplet.commit_pos);
			goto corrupt;
		}

		hash_pos = kh_get_oid_map(bitmap_git->bitmaps, xor_item->oid);

		/*
		 * If desired bitmap is already stored, we don't need
		 * to iterate further. Because we know that bitmaps
		 * that are needed to be parsed to parse this bitmap
		 * has already been stored. So, assign this stored bitmap
		 * to the xor_bitmap.
		 */
		if (hash_pos < kh_end(bitmap_git->bitmaps) &&
			(xor_bitmap = kh_value(bitmap_git->bitmaps, hash_pos)))
			break;
		xor_items_nr++;
		xor_row = triplet.xor_row;
	}

	while (xor_items_nr) {
		xor_item = &xor_items[xor_items_nr - 1];
		bitmap_git->map_pos = xor_item->offset;
		if (bitmap_git->map_size - bitmap_git->map_pos < bitmap_header_size) {
			error(_("corrupt ewah bitmap: truncated header for bitmap of commit \"%s\""),
				oid_to_hex(&xor_item->oid));
			goto corrupt;
		}

		bitmap_git->map_pos += sizeof(uint32_t) + sizeof(uint8_t);
		xor_flags = read_u8(bitmap_git->map, &bitmap_git->map_pos);
		bitmap = read_bitmap_1(bitmap_git);

		if (!bitmap)
			goto corrupt;

		xor_bitmap = store_bitmap(bitmap_git, bitmap, &xor_item->oid, xor_bitmap, xor_flags);
		xor_items_nr--;
	}

	bitmap_git->map_pos = offset;
	if (bitmap_git->map_size - bitmap_git->map_pos < bitmap_header_size) {
		error(_("corrupt ewah bitmap: truncated header for bitmap of commit \"%s\""),
			oid_to_hex(oid));
		goto corrupt;
	}

	/*
	 * Don't bother reading the commit's index position or its xor
	 * offset:
	 *
	 *   - The commit's index position is irrelevant to us, since
	 *     load_bitmap_entries_v1 only uses it to learn the object
	 *     id which is used to compute the hashmap's key. We already
	 *     have an object id, so no need to look it up again.
	 *
	 *   - The xor_offset is unusable for us, since it specifies how
	 *     many entries previous to ours we should look at. This
	 *     makes sense when reading the bitmaps sequentially (as in
	 *     load_bitmap_entries_v1()), since we can keep track of
	 *     each bitmap as we read them.
	 *
	 *     But it can't work for us, since the bitmap's don't have a
	 *     fixed size. So we learn the position of the xor'd bitmap
	 *     from the commit table (and resolve it to a bitmap in the
	 *     above if-statement).
	 *
	 * Instead, we can skip ahead and immediately read the flags and
	 * ewah bitmap.
	 */
	bitmap_git->map_pos += sizeof(uint32_t) + sizeof(uint8_t);
	flags = read_u8(bitmap_git->map, &bitmap_git->map_pos);
	bitmap = read_bitmap_1(bitmap_git);

	if (!bitmap)
		goto corrupt;

	return store_bitmap(bitmap_git, bitmap, oid, xor_bitmap, flags);

corrupt:
	free(xor_items);
	is_corrupt = 1;
	return NULL;
}

struct ewah_bitmap *bitmap_for_commit(struct bitmap_index *bitmap_git,
				      struct commit *commit)
{
	khiter_t hash_pos = kh_get_oid_map(bitmap_git->bitmaps,
					   commit->object.oid);
	if (hash_pos >= kh_end(bitmap_git->bitmaps)) {
		struct stored_bitmap *bitmap = NULL;
		if (!bitmap_git->table_lookup)
			return NULL;

		/* this is a fairly hot codepath - no trace2_region please */
		/* NEEDSWORK: cache misses aren't recorded */
		bitmap = lazy_bitmap_for_commit(bitmap_git, commit);
		if (!bitmap)
			return NULL;
		return lookup_stored_bitmap(bitmap);
	}
	return lookup_stored_bitmap(kh_value(bitmap_git->bitmaps, hash_pos));
}

static inline int bitmap_position_extended(struct bitmap_index *bitmap_git,
					   const struct object_id *oid)
{
	kh_oid_pos_t *positions = bitmap_git->ext_index.positions;
	khiter_t pos = kh_get_oid_pos(positions, *oid);

	if (pos < kh_end(positions)) {
		int bitmap_pos = kh_value(positions, pos);
		return bitmap_pos + bitmap_num_objects(bitmap_git);
	}

	return -1;
}

static inline int bitmap_position_packfile(struct bitmap_index *bitmap_git,
					   const struct object_id *oid)
{
	uint32_t pos;
	off_t offset = find_pack_entry_one(oid->hash, bitmap_git->pack);
	if (!offset)
		return -1;

	if (offset_to_pack_pos(bitmap_git->pack, offset, &pos) < 0)
		return -1;
	return pos;
}

static int bitmap_position_midx(struct bitmap_index *bitmap_git,
				const struct object_id *oid)
{
	uint32_t want, got;
	if (!bsearch_midx(oid, bitmap_git->midx, &want))
		return -1;

	if (midx_to_pack_pos(bitmap_git->midx, want, &got) < 0)
		return -1;
	return got;
}

static int bitmap_position(struct bitmap_index *bitmap_git,
			   const struct object_id *oid)
{
	int pos;
	if (bitmap_is_midx(bitmap_git))
		pos = bitmap_position_midx(bitmap_git, oid);
	else
		pos = bitmap_position_packfile(bitmap_git, oid);
	return (pos >= 0) ? pos : bitmap_position_extended(bitmap_git, oid);
}

static int ext_index_add_object(struct bitmap_index *bitmap_git,
				struct object *object, const char *name)
{
	struct eindex *eindex = &bitmap_git->ext_index;

	khiter_t hash_pos;
	int hash_ret;
	int bitmap_pos;

	hash_pos = kh_put_oid_pos(eindex->positions, object->oid, &hash_ret);
	if (hash_ret > 0) {
		if (eindex->count >= eindex->alloc) {
			eindex->alloc = (eindex->alloc + 16) * 3 / 2;
			REALLOC_ARRAY(eindex->objects, eindex->alloc);
			REALLOC_ARRAY(eindex->hashes, eindex->alloc);
		}

		bitmap_pos = eindex->count;
		eindex->objects[eindex->count] = object;
		eindex->hashes[eindex->count] = pack_name_hash(name);
		kh_value(eindex->positions, hash_pos) = bitmap_pos;
		eindex->count++;
	} else {
		bitmap_pos = kh_value(eindex->positions, hash_pos);
	}

	return bitmap_pos + bitmap_num_objects(bitmap_git);
}

struct bitmap_show_data {
	struct bitmap_index *bitmap_git;
	struct bitmap *base;
};

static void show_object(struct object *object, const char *name, void *data_)
{
	struct bitmap_show_data *data = data_;
	int bitmap_pos;

	bitmap_pos = bitmap_position(data->bitmap_git, &object->oid);

	if (bitmap_pos < 0)
		bitmap_pos = ext_index_add_object(data->bitmap_git, object,
						  name);

	bitmap_set(data->base, bitmap_pos);
}

static void show_commit(struct commit *commit, void *data)
{
}

static int add_to_include_set(struct bitmap_index *bitmap_git,
			      struct include_data *data,
			      struct commit *commit,
			      int bitmap_pos)
{
	struct ewah_bitmap *partial;

	if (data->seen && bitmap_get(data->seen, bitmap_pos))
		return 0;

	if (bitmap_get(data->base, bitmap_pos))
		return 0;

	partial = bitmap_for_commit(bitmap_git, commit);
	if (partial) {
		bitmap_or_ewah(data->base, partial);
		return 0;
	}

	bitmap_set(data->base, bitmap_pos);
	return 1;
}

static int should_include(struct commit *commit, void *_data)
{
	struct include_data *data = _data;
	int bitmap_pos;

	bitmap_pos = bitmap_position(data->bitmap_git, &commit->object.oid);
	if (bitmap_pos < 0)
		bitmap_pos = ext_index_add_object(data->bitmap_git,
						  (struct object *)commit,
						  NULL);

	if (!add_to_include_set(data->bitmap_git, data, commit, bitmap_pos)) {
		struct commit_list *parent = commit->parents;

		while (parent) {
			parent->item->object.flags |= SEEN;
			parent = parent->next;
		}

		return 0;
	}

	return 1;
}

static int should_include_obj(struct object *obj, void *_data)
{
	struct include_data *data = _data;
	int bitmap_pos;

	bitmap_pos = bitmap_position(data->bitmap_git, &obj->oid);
	if (bitmap_pos < 0)
		return 1;
	if ((data->seen && bitmap_get(data->seen, bitmap_pos)) ||
	     bitmap_get(data->base, bitmap_pos)) {
		obj->flags |= SEEN;
		return 0;
	}
	return 1;
}

static int add_commit_to_bitmap(struct bitmap_index *bitmap_git,
				struct bitmap **base,
				struct commit *commit)
{
	struct ewah_bitmap *or_with = bitmap_for_commit(bitmap_git, commit);

	if (!or_with)
		return 0;

	if (!*base)
		*base = ewah_to_bitmap(or_with);
	else
		bitmap_or_ewah(*base, or_with);

	return 1;
}

static struct bitmap *find_objects(struct bitmap_index *bitmap_git,
				   struct rev_info *revs,
				   struct object_list *roots,
				   struct bitmap *seen)
{
	struct bitmap *base = NULL;
	int needs_walk = 0;

	struct object_list *not_mapped = NULL;

	/*
	 * Go through all the roots for the walk. The ones that have bitmaps
	 * on the bitmap index will be `or`ed together to form an initial
	 * global reachability analysis.
	 *
	 * The ones without bitmaps in the index will be stored in the
	 * `not_mapped_list` for further processing.
	 */
	while (roots) {
		struct object *object = roots->item;
		roots = roots->next;

		if (object->type == OBJ_COMMIT &&
		    add_commit_to_bitmap(bitmap_git, &base, (struct commit *)object)) {
			object->flags |= SEEN;
			continue;
		}

		object_list_insert(object, &not_mapped);
	}

	/*
	 * Best case scenario: We found bitmaps for all the roots,
	 * so the resulting `or` bitmap has the full reachability analysis
	 */
	if (!not_mapped)
		return base;

	roots = not_mapped;

	/*
	 * Let's iterate through all the roots that don't have bitmaps to
	 * check if we can determine them to be reachable from the existing
	 * global bitmap.
	 *
	 * If we cannot find them in the existing global bitmap, we'll need
	 * to push them to an actual walk and run it until we can confirm
	 * they are reachable
	 */
	while (roots) {
		struct object *object = roots->item;
		int pos;

		roots = roots->next;
		pos = bitmap_position(bitmap_git, &object->oid);

		if (pos < 0 || base == NULL || !bitmap_get(base, pos)) {
			object->flags &= ~UNINTERESTING;
			add_pending_object(revs, object, "");
			needs_walk = 1;
		} else {
			object->flags |= SEEN;
		}
	}

	if (needs_walk) {
		struct include_data incdata;
		struct bitmap_show_data show_data;

		if (!base)
			base = bitmap_new();

		incdata.bitmap_git = bitmap_git;
		incdata.base = base;
		incdata.seen = seen;

		revs->include_check = should_include;
		revs->include_check_obj = should_include_obj;
		revs->include_check_data = &incdata;

		if (prepare_revision_walk(revs))
			die(_("revision walk setup failed"));

		show_data.bitmap_git = bitmap_git;
		show_data.base = base;

		traverse_commit_list(revs,
				     show_commit, show_object,
				     &show_data);

		revs->include_check = NULL;
		revs->include_check_obj = NULL;
		revs->include_check_data = NULL;
	}

	return base;
}

static void show_extended_objects(struct bitmap_index *bitmap_git,
				  struct rev_info *revs,
				  show_reachable_fn show_reach)
{
	struct bitmap *objects = bitmap_git->result;
	struct eindex *eindex = &bitmap_git->ext_index;
	uint32_t i;

	for (i = 0; i < eindex->count; ++i) {
		struct object *obj;

		if (!bitmap_get(objects, bitmap_num_objects(bitmap_git) + i))
			continue;

		obj = eindex->objects[i];
		if ((obj->type == OBJ_BLOB && !revs->blob_objects) ||
		    (obj->type == OBJ_TREE && !revs->tree_objects) ||
		    (obj->type == OBJ_TAG && !revs->tag_objects))
			continue;

		show_reach(&obj->oid, obj->type, 0, eindex->hashes[i], NULL, 0);
	}
}

static void init_type_iterator(struct ewah_iterator *it,
			       struct bitmap_index *bitmap_git,
			       enum object_type type)
{
	switch (type) {
	case OBJ_COMMIT:
		ewah_iterator_init(it, bitmap_git->commits);
		break;

	case OBJ_TREE:
		ewah_iterator_init(it, bitmap_git->trees);
		break;

	case OBJ_BLOB:
		ewah_iterator_init(it, bitmap_git->blobs);
		break;

	case OBJ_TAG:
		ewah_iterator_init(it, bitmap_git->tags);
		break;

	default:
		BUG("object type %d not stored by bitmap type index", type);
		break;
	}
}

static void show_objects_for_type(
	struct bitmap_index *bitmap_git,
	enum object_type object_type,
	show_reachable_fn show_reach)
{
	size_t i = 0;
	uint32_t offset;

	struct ewah_iterator it;
	eword_t filter;

	struct bitmap *objects = bitmap_git->result;

	init_type_iterator(&it, bitmap_git, object_type);

	for (i = 0; i < objects->word_alloc &&
			ewah_iterator_next(&filter, &it); i++) {
		eword_t word = objects->words[i] & filter;
		size_t pos = (i * BITS_IN_EWORD);

		if (!word)
			continue;

		for (offset = 0; offset < BITS_IN_EWORD; ++offset) {
			struct packed_git *pack;
			struct object_id oid;
			uint32_t hash = 0, index_pos;
			off_t ofs;

			if ((word >> offset) == 0)
				break;

			offset += ewah_bit_ctz64(word >> offset);

			if (bitmap_is_midx(bitmap_git)) {
				struct multi_pack_index *m = bitmap_git->midx;
				uint32_t pack_id;

				index_pos = pack_pos_to_midx(m, pos + offset);
				ofs = nth_midxed_offset(m, index_pos);
				nth_midxed_object_oid(&oid, m, index_pos);

				pack_id = nth_midxed_pack_int_id(m, index_pos);
				pack = bitmap_git->midx->packs[pack_id];
			} else {
				index_pos = pack_pos_to_index(bitmap_git->pack, pos + offset);
				ofs = pack_pos_to_offset(bitmap_git->pack, pos + offset);
				nth_bitmap_object_oid(bitmap_git, &oid, index_pos);

				pack = bitmap_git->pack;
			}

			if (bitmap_git->hashes)
				hash = get_be32(bitmap_git->hashes + index_pos);

			show_reach(&oid, object_type, 0, hash, pack, ofs);
		}
	}
}

static int in_bitmapped_pack(struct bitmap_index *bitmap_git,
			     struct object_list *roots)
{
	while (roots) {
		struct object *object = roots->item;
		roots = roots->next;

		if (bitmap_is_midx(bitmap_git)) {
			if (bsearch_midx(&object->oid, bitmap_git->midx, NULL))
				return 1;
		} else {
			if (find_pack_entry_one(object->oid.hash, bitmap_git->pack) > 0)
				return 1;
		}
	}

	return 0;
}

static struct bitmap *find_tip_objects(struct bitmap_index *bitmap_git,
				       struct object_list *tip_objects,
				       enum object_type type)
{
	struct bitmap *result = bitmap_new();
	struct object_list *p;

	for (p = tip_objects; p; p = p->next) {
		int pos;

		if (p->item->type != type)
			continue;

		pos = bitmap_position(bitmap_git, &p->item->oid);
		if (pos < 0)
			continue;

		bitmap_set(result, pos);
	}

	return result;
}

static void filter_bitmap_exclude_type(struct bitmap_index *bitmap_git,
				       struct object_list *tip_objects,
				       struct bitmap *to_filter,
				       enum object_type type)
{
	struct eindex *eindex = &bitmap_git->ext_index;
	struct bitmap *tips;
	struct ewah_iterator it;
	eword_t mask;
	uint32_t i;

	/*
	 * The non-bitmap version of this filter never removes
	 * objects which the other side specifically asked for,
	 * so we must match that behavior.
	 */
	tips = find_tip_objects(bitmap_git, tip_objects, type);

	/*
	 * We can use the type-level bitmap for 'type' to work in whole
	 * words for the objects that are actually in the bitmapped
	 * packfile.
	 */
	for (i = 0, init_type_iterator(&it, bitmap_git, type);
	     i < to_filter->word_alloc && ewah_iterator_next(&mask, &it);
	     i++) {
		if (i < tips->word_alloc)
			mask &= ~tips->words[i];
		to_filter->words[i] &= ~mask;
	}

	/*
	 * Clear any objects that weren't in the packfile (and so would
	 * not have been caught by the loop above. We'll have to check
	 * them individually.
	 */
	for (i = 0; i < eindex->count; i++) {
		uint32_t pos = i + bitmap_num_objects(bitmap_git);
		if (eindex->objects[i]->type == type &&
		    bitmap_get(to_filter, pos) &&
		    !bitmap_get(tips, pos))
			bitmap_unset(to_filter, pos);
	}

	bitmap_free(tips);
}

static void filter_bitmap_blob_none(struct bitmap_index *bitmap_git,
				    struct object_list *tip_objects,
				    struct bitmap *to_filter)
{
	filter_bitmap_exclude_type(bitmap_git, tip_objects, to_filter,
				   OBJ_BLOB);
}

static unsigned long get_size_by_pos(struct bitmap_index *bitmap_git,
				     uint32_t pos)
{
	unsigned long size;
	struct object_info oi = OBJECT_INFO_INIT;

	oi.sizep = &size;

	if (pos < bitmap_num_objects(bitmap_git)) {
		struct packed_git *pack;
		off_t ofs;

		if (bitmap_is_midx(bitmap_git)) {
			uint32_t midx_pos = pack_pos_to_midx(bitmap_git->midx, pos);
			uint32_t pack_id = nth_midxed_pack_int_id(bitmap_git->midx, midx_pos);

			pack = bitmap_git->midx->packs[pack_id];
			ofs = nth_midxed_offset(bitmap_git->midx, midx_pos);
		} else {
			pack = bitmap_git->pack;
			ofs = pack_pos_to_offset(pack, pos);
		}

		if (packed_object_info(the_repository, pack, ofs, &oi) < 0) {
			struct object_id oid;
			nth_bitmap_object_oid(bitmap_git, &oid,
					      pack_pos_to_index(pack, pos));
			die(_("unable to get size of %s"), oid_to_hex(&oid));
		}
	} else {
		struct eindex *eindex = &bitmap_git->ext_index;
		struct object *obj = eindex->objects[pos - bitmap_num_objects(bitmap_git)];
		if (oid_object_info_extended(the_repository, &obj->oid, &oi, 0) < 0)
			die(_("unable to get size of %s"), oid_to_hex(&obj->oid));
	}

	return size;
}

static void filter_bitmap_blob_limit(struct bitmap_index *bitmap_git,
				     struct object_list *tip_objects,
				     struct bitmap *to_filter,
				     unsigned long limit)
{
	struct eindex *eindex = &bitmap_git->ext_index;
	struct bitmap *tips;
	struct ewah_iterator it;
	eword_t mask;
	uint32_t i;

	tips = find_tip_objects(bitmap_git, tip_objects, OBJ_BLOB);

	for (i = 0, init_type_iterator(&it, bitmap_git, OBJ_BLOB);
	     i < to_filter->word_alloc && ewah_iterator_next(&mask, &it);
	     i++) {
		eword_t word = to_filter->words[i] & mask;
		unsigned offset;

		for (offset = 0; offset < BITS_IN_EWORD; offset++) {
			uint32_t pos;

			if ((word >> offset) == 0)
				break;
			offset += ewah_bit_ctz64(word >> offset);
			pos = i * BITS_IN_EWORD + offset;

			if (!bitmap_get(tips, pos) &&
			    get_size_by_pos(bitmap_git, pos) >= limit)
				bitmap_unset(to_filter, pos);
		}
	}

	for (i = 0; i < eindex->count; i++) {
		uint32_t pos = i + bitmap_num_objects(bitmap_git);
		if (eindex->objects[i]->type == OBJ_BLOB &&
		    bitmap_get(to_filter, pos) &&
		    !bitmap_get(tips, pos) &&
		    get_size_by_pos(bitmap_git, pos) >= limit)
			bitmap_unset(to_filter, pos);
	}

	bitmap_free(tips);
}

static void filter_bitmap_tree_depth(struct bitmap_index *bitmap_git,
				     struct object_list *tip_objects,
				     struct bitmap *to_filter,
				     unsigned long limit)
{
	if (limit)
		BUG("filter_bitmap_tree_depth given non-zero limit");

	filter_bitmap_exclude_type(bitmap_git, tip_objects, to_filter,
				   OBJ_TREE);
	filter_bitmap_exclude_type(bitmap_git, tip_objects, to_filter,
				   OBJ_BLOB);
}

static void filter_bitmap_object_type(struct bitmap_index *bitmap_git,
				      struct object_list *tip_objects,
				      struct bitmap *to_filter,
				      enum object_type object_type)
{
	if (object_type < OBJ_COMMIT || object_type > OBJ_TAG)
		BUG("filter_bitmap_object_type given invalid object");

	if (object_type != OBJ_TAG)
		filter_bitmap_exclude_type(bitmap_git, tip_objects, to_filter, OBJ_TAG);
	if (object_type != OBJ_COMMIT)
		filter_bitmap_exclude_type(bitmap_git, tip_objects, to_filter, OBJ_COMMIT);
	if (object_type != OBJ_TREE)
		filter_bitmap_exclude_type(bitmap_git, tip_objects, to_filter, OBJ_TREE);
	if (object_type != OBJ_BLOB)
		filter_bitmap_exclude_type(bitmap_git, tip_objects, to_filter, OBJ_BLOB);
}

static int filter_bitmap(struct bitmap_index *bitmap_git,
			 struct object_list *tip_objects,
			 struct bitmap *to_filter,
			 struct list_objects_filter_options *filter)
{
	if (!filter || filter->choice == LOFC_DISABLED)
		return 0;

	if (filter->choice == LOFC_BLOB_NONE) {
		if (bitmap_git)
			filter_bitmap_blob_none(bitmap_git, tip_objects,
						to_filter);
		return 0;
	}

	if (filter->choice == LOFC_BLOB_LIMIT) {
		if (bitmap_git)
			filter_bitmap_blob_limit(bitmap_git, tip_objects,
						 to_filter,
						 filter->blob_limit_value);
		return 0;
	}

	if (filter->choice == LOFC_TREE_DEPTH &&
	    filter->tree_exclude_depth == 0) {
		if (bitmap_git)
			filter_bitmap_tree_depth(bitmap_git, tip_objects,
						 to_filter,
						 filter->tree_exclude_depth);
		return 0;
	}

	if (filter->choice == LOFC_OBJECT_TYPE) {
		if (bitmap_git)
			filter_bitmap_object_type(bitmap_git, tip_objects,
						  to_filter,
						  filter->object_type);
		return 0;
	}

	if (filter->choice == LOFC_COMBINE) {
		int i;
		for (i = 0; i < filter->sub_nr; i++) {
			if (filter_bitmap(bitmap_git, tip_objects, to_filter,
					  &filter->sub[i]) < 0)
				return -1;
		}
		return 0;
	}

	/* filter choice not handled */
	return -1;
}

static int can_filter_bitmap(struct list_objects_filter_options *filter)
{
	return !filter_bitmap(NULL, NULL, NULL, filter);
}

struct bitmap_index *prepare_bitmap_walk(struct rev_info *revs,
					 int filter_provided_objects)
{
	unsigned int i;

	struct object_list *wants = NULL;
	struct object_list *haves = NULL;

	struct bitmap *wants_bitmap = NULL;
	struct bitmap *haves_bitmap = NULL;

	struct bitmap_index *bitmap_git;

	/*
	 * We can't do pathspec limiting with bitmaps, because we don't know
	 * which commits are associated with which object changes (let alone
	 * even which objects are associated with which paths).
	 */
	if (revs->prune)
		return NULL;

	if (!can_filter_bitmap(&revs->filter))
		return NULL;

	/* try to open a bitmapped pack, but don't parse it yet
	 * because we may not need to use it */
	CALLOC_ARRAY(bitmap_git, 1);
	if (open_bitmap(revs->repo, bitmap_git) < 0)
		goto cleanup;

	for (i = 0; i < revs->pending.nr; ++i) {
		struct object *object = revs->pending.objects[i].item;

		if (object->type == OBJ_NONE)
			parse_object_or_die(&object->oid, NULL);

		while (object->type == OBJ_TAG) {
			struct tag *tag = (struct tag *) object;

			if (object->flags & UNINTERESTING)
				object_list_insert(object, &haves);
			else
				object_list_insert(object, &wants);

			object = parse_object_or_die(get_tagged_oid(tag), NULL);
			object->flags |= (tag->object.flags & UNINTERESTING);
		}

		if (object->flags & UNINTERESTING)
			object_list_insert(object, &haves);
		else
			object_list_insert(object, &wants);
	}

	/*
	 * if we have a HAVES list, but none of those haves is contained
	 * in the packfile that has a bitmap, we don't have anything to
	 * optimize here
	 */
	if (haves && !in_bitmapped_pack(bitmap_git, haves))
		goto cleanup;

	/* if we don't want anything, we're done here */
	if (!wants)
		goto cleanup;

	/*
	 * now we're going to use bitmaps, so load the actual bitmap entries
	 * from disk. this is the point of no return; after this the rev_list
	 * becomes invalidated and we must perform the revwalk through bitmaps
	 */
	if (load_bitmap(bitmap_git) < 0)
		goto cleanup;

	object_array_clear(&revs->pending);

	if (haves) {
		revs->ignore_missing_links = 1;
		haves_bitmap = find_objects(bitmap_git, revs, haves, NULL);
		reset_revision_walk();
		revs->ignore_missing_links = 0;

		if (!haves_bitmap)
			BUG("failed to perform bitmap walk");
	}

	wants_bitmap = find_objects(bitmap_git, revs, wants, haves_bitmap);

	if (!wants_bitmap)
		BUG("failed to perform bitmap walk");

	if (haves_bitmap)
		bitmap_and_not(wants_bitmap, haves_bitmap);

	filter_bitmap(bitmap_git,
		      (revs->filter.choice && filter_provided_objects) ? NULL : wants,
		      wants_bitmap,
		      &revs->filter);

	bitmap_git->result = wants_bitmap;
	bitmap_git->haves = haves_bitmap;

	object_list_free(&wants);
	object_list_free(&haves);

	return bitmap_git;

cleanup:
	free_bitmap_index(bitmap_git);
	object_list_free(&wants);
	object_list_free(&haves);
	return NULL;
}

/*
 * -1 means "stop trying further objects"; 0 means we may or may not have
 * reused, but you can keep feeding bits.
 */
static int try_partial_reuse(struct packed_git *pack,
			     size_t pos,
			     struct bitmap *reuse,
			     struct pack_window **w_curs)
{
	off_t offset, delta_obj_offset;
	enum object_type type;
	unsigned long size;

	/*
	 * try_partial_reuse() is called either on (a) objects in the
	 * bitmapped pack (in the case of a single-pack bitmap) or (b)
	 * objects in the preferred pack of a multi-pack bitmap.
	 * Importantly, the latter can pretend as if only a single pack
	 * exists because:
	 *
	 *   - The first pack->num_objects bits of a MIDX bitmap are
	 *     reserved for the preferred pack, and
	 *
	 *   - Ties due to duplicate objects are always resolved in
	 *     favor of the preferred pack.
	 *
	 * Therefore we do not need to ever ask the MIDX for its copy of
	 * an object by OID, since it will always select it from the
	 * preferred pack. Likewise, the selected copy of the base
	 * object for any deltas will reside in the same pack.
	 *
	 * This means that we can reuse pos when looking up the bit in
	 * the reuse bitmap, too, since bits corresponding to the
	 * preferred pack precede all bits from other packs.
	 */

	if (pos >= pack->num_objects)
		return -1; /* not actually in the pack or MIDX preferred pack */

	offset = delta_obj_offset = pack_pos_to_offset(pack, pos);
	type = unpack_object_header(pack, w_curs, &offset, &size);
	if (type < 0)
		return -1; /* broken packfile, punt */

	if (type == OBJ_REF_DELTA || type == OBJ_OFS_DELTA) {
		off_t base_offset;
		uint32_t base_pos;

		/*
		 * Find the position of the base object so we can look it up
		 * in our bitmaps. If we can't come up with an offset, or if
		 * that offset is not in the revidx, the pack is corrupt.
		 * There's nothing we can do, so just punt on this object,
		 * and the normal slow path will complain about it in
		 * more detail.
		 */
		base_offset = get_delta_base(pack, w_curs, &offset, type,
					     delta_obj_offset);
		if (!base_offset)
			return 0;
		if (offset_to_pack_pos(pack, base_offset, &base_pos) < 0)
			return 0;

		/*
		 * We assume delta dependencies always point backwards. This
		 * lets us do a single pass, and is basically always true
		 * due to the way OFS_DELTAs work. You would not typically
		 * find REF_DELTA in a bitmapped pack, since we only bitmap
		 * packs we write fresh, and OFS_DELTA is the default). But
		 * let's double check to make sure the pack wasn't written with
		 * odd parameters.
		 */
		if (base_pos >= pos)
			return 0;

		/*
		 * And finally, if we're not sending the base as part of our
		 * reuse chunk, then don't send this object either. The base
		 * would come after us, along with other objects not
		 * necessarily in the pack, which means we'd need to convert
		 * to REF_DELTA on the fly. Better to just let the normal
		 * object_entry code path handle it.
		 */
		if (!bitmap_get(reuse, base_pos))
			return 0;
	}

	/*
	 * If we got here, then the object is OK to reuse. Mark it.
	 */
	bitmap_set(reuse, pos);
	return 0;
}

uint32_t midx_preferred_pack(struct bitmap_index *bitmap_git)
{
	struct multi_pack_index *m = bitmap_git->midx;
	if (!m)
		BUG("midx_preferred_pack: requires non-empty MIDX");
	return nth_midxed_pack_int_id(m, pack_pos_to_midx(bitmap_git->midx, 0));
}

int reuse_partial_packfile_from_bitmap(struct bitmap_index *bitmap_git,
				       struct packed_git **packfile_out,
				       uint32_t *entries,
				       struct bitmap **reuse_out)
{
	struct packed_git *pack;
	struct bitmap *result = bitmap_git->result;
	struct bitmap *reuse;
	struct pack_window *w_curs = NULL;
	size_t i = 0;
	uint32_t offset;
	uint32_t objects_nr;

	assert(result);

	load_reverse_index(bitmap_git);

	if (bitmap_is_midx(bitmap_git))
		pack = bitmap_git->midx->packs[midx_preferred_pack(bitmap_git)];
	else
		pack = bitmap_git->pack;
	objects_nr = pack->num_objects;

	while (i < result->word_alloc && result->words[i] == (eword_t)~0)
		i++;

	/*
	 * Don't mark objects not in the packfile or preferred pack. This bitmap
	 * marks objects eligible for reuse, but the pack-reuse code only
	 * understands how to reuse a single pack. Since the preferred pack is
	 * guaranteed to have all bases for its deltas (in a multi-pack bitmap),
	 * we use it instead of another pack. In single-pack bitmaps, the choice
	 * is made for us.
	 */
	if (i > objects_nr / BITS_IN_EWORD)
		i = objects_nr / BITS_IN_EWORD;

	reuse = bitmap_word_alloc(i);
	memset(reuse->words, 0xFF, i * sizeof(eword_t));

	for (; i < result->word_alloc; ++i) {
		eword_t word = result->words[i];
		size_t pos = (i * BITS_IN_EWORD);

		for (offset = 0; offset < BITS_IN_EWORD; ++offset) {
			if ((word >> offset) == 0)
				break;

			offset += ewah_bit_ctz64(word >> offset);
			if (try_partial_reuse(pack, pos + offset,
					      reuse, &w_curs) < 0) {
				/*
				 * try_partial_reuse indicated we couldn't reuse
				 * any bits, so there is no point in trying more
				 * bits in the current word, or any other words
				 * in result.
				 *
				 * Jump out of both loops to avoid future
				 * unnecessary calls to try_partial_reuse.
				 */
				goto done;
			}
		}
	}

done:
	unuse_pack(&w_curs);

	*entries = bitmap_popcount(reuse);
	if (!*entries) {
		bitmap_free(reuse);
		return -1;
	}

	/*
	 * Drop any reused objects from the result, since they will not
	 * need to be handled separately.
	 */
	bitmap_and_not(result, reuse);
	*packfile_out = pack;
	*reuse_out = reuse;
	return 0;
}

int bitmap_walk_contains(struct bitmap_index *bitmap_git,
			 struct bitmap *bitmap, const struct object_id *oid)
{
	int idx;

	if (!bitmap)
		return 0;

	idx = bitmap_position(bitmap_git, oid);
	return idx >= 0 && bitmap_get(bitmap, idx);
}

void traverse_bitmap_commit_list(struct bitmap_index *bitmap_git,
				 struct rev_info *revs,
				 show_reachable_fn show_reachable)
{
	assert(bitmap_git->result);

	show_objects_for_type(bitmap_git, OBJ_COMMIT, show_reachable);
	if (revs->tree_objects)
		show_objects_for_type(bitmap_git, OBJ_TREE, show_reachable);
	if (revs->blob_objects)
		show_objects_for_type(bitmap_git, OBJ_BLOB, show_reachable);
	if (revs->tag_objects)
		show_objects_for_type(bitmap_git, OBJ_TAG, show_reachable);

	show_extended_objects(bitmap_git, revs, show_reachable);
}

static uint32_t count_object_type(struct bitmap_index *bitmap_git,
				  enum object_type type)
{
	struct bitmap *objects = bitmap_git->result;
	struct eindex *eindex = &bitmap_git->ext_index;

	uint32_t i = 0, count = 0;
	struct ewah_iterator it;
	eword_t filter;

	init_type_iterator(&it, bitmap_git, type);

	while (i < objects->word_alloc && ewah_iterator_next(&filter, &it)) {
		eword_t word = objects->words[i++] & filter;
		count += ewah_bit_popcount64(word);
	}

	for (i = 0; i < eindex->count; ++i) {
		if (eindex->objects[i]->type == type &&
			bitmap_get(objects, bitmap_num_objects(bitmap_git) + i))
			count++;
	}

	return count;
}

void count_bitmap_commit_list(struct bitmap_index *bitmap_git,
			      uint32_t *commits, uint32_t *trees,
			      uint32_t *blobs, uint32_t *tags)
{
	assert(bitmap_git->result);

	if (commits)
		*commits = count_object_type(bitmap_git, OBJ_COMMIT);

	if (trees)
		*trees = count_object_type(bitmap_git, OBJ_TREE);

	if (blobs)
		*blobs = count_object_type(bitmap_git, OBJ_BLOB);

	if (tags)
		*tags = count_object_type(bitmap_git, OBJ_TAG);
}

struct bitmap_test_data {
	struct bitmap_index *bitmap_git;
	struct bitmap *base;
	struct bitmap *commits;
	struct bitmap *trees;
	struct bitmap *blobs;
	struct bitmap *tags;
	struct progress *prg;
	size_t seen;
};

static void test_bitmap_type(struct bitmap_test_data *tdata,
			     struct object *obj, int pos)
{
	enum object_type bitmap_type = OBJ_NONE;
	int bitmaps_nr = 0;

	if (bitmap_get(tdata->commits, pos)) {
		bitmap_type = OBJ_COMMIT;
		bitmaps_nr++;
	}
	if (bitmap_get(tdata->trees, pos)) {
		bitmap_type = OBJ_TREE;
		bitmaps_nr++;
	}
	if (bitmap_get(tdata->blobs, pos)) {
		bitmap_type = OBJ_BLOB;
		bitmaps_nr++;
	}
	if (bitmap_get(tdata->tags, pos)) {
		bitmap_type = OBJ_TAG;
		bitmaps_nr++;
	}

	if (bitmap_type == OBJ_NONE)
		die(_("object '%s' not found in type bitmaps"),
		    oid_to_hex(&obj->oid));

	if (bitmaps_nr > 1)
		die(_("object '%s' does not have a unique type"),
		    oid_to_hex(&obj->oid));

	if (bitmap_type != obj->type)
		die(_("object '%s': real type '%s', expected: '%s'"),
		    oid_to_hex(&obj->oid),
		    type_name(obj->type),
		    type_name(bitmap_type));
}

static void test_show_object(struct object *object, const char *name,
			     void *data)
{
	struct bitmap_test_data *tdata = data;
	int bitmap_pos;

	bitmap_pos = bitmap_position(tdata->bitmap_git, &object->oid);
	if (bitmap_pos < 0)
		die(_("object not in bitmap: '%s'"), oid_to_hex(&object->oid));
	test_bitmap_type(tdata, object, bitmap_pos);

	bitmap_set(tdata->base, bitmap_pos);
	display_progress(tdata->prg, ++tdata->seen);
}

static void test_show_commit(struct commit *commit, void *data)
{
	struct bitmap_test_data *tdata = data;
	int bitmap_pos;

	bitmap_pos = bitmap_position(tdata->bitmap_git,
				     &commit->object.oid);
	if (bitmap_pos < 0)
		die(_("object not in bitmap: '%s'"), oid_to_hex(&commit->object.oid));
	test_bitmap_type(tdata, &commit->object, bitmap_pos);

	bitmap_set(tdata->base, bitmap_pos);
	display_progress(tdata->prg, ++tdata->seen);
}

void test_bitmap_walk(struct rev_info *revs)
{
	struct object *root;
	struct bitmap *result = NULL;
	size_t result_popcnt;
	struct bitmap_test_data tdata;
	struct bitmap_index *bitmap_git;
	struct ewah_bitmap *bm;

	if (!(bitmap_git = prepare_bitmap_git(revs->repo)))
		die(_("failed to load bitmap indexes"));

	if (revs->pending.nr != 1)
		die(_("you must specify exactly one commit to test"));

	fprintf_ln(stderr, "Bitmap v%d test (%d entries%s)",
		bitmap_git->version,
		bitmap_git->entry_count,
		bitmap_git->table_lookup ? "" : " loaded");

	root = revs->pending.objects[0].item;
	bm = bitmap_for_commit(bitmap_git, (struct commit *)root);

	if (bm) {
		fprintf_ln(stderr, "Found bitmap for '%s'. %d bits / %08x checksum",
			oid_to_hex(&root->oid), (int)bm->bit_size, ewah_checksum(bm));

		result = ewah_to_bitmap(bm);
	}

	if (!result)
		die(_("commit '%s' doesn't have an indexed bitmap"), oid_to_hex(&root->oid));

	revs->tag_objects = 1;
	revs->tree_objects = 1;
	revs->blob_objects = 1;

	result_popcnt = bitmap_popcount(result);

	if (prepare_revision_walk(revs))
		die(_("revision walk setup failed"));

	tdata.bitmap_git = bitmap_git;
	tdata.base = bitmap_new();
	tdata.commits = ewah_to_bitmap(bitmap_git->commits);
	tdata.trees = ewah_to_bitmap(bitmap_git->trees);
	tdata.blobs = ewah_to_bitmap(bitmap_git->blobs);
	tdata.tags = ewah_to_bitmap(bitmap_git->tags);
	tdata.prg = start_progress("Verifying bitmap entries", result_popcnt);
	tdata.seen = 0;

	traverse_commit_list(revs, &test_show_commit, &test_show_object, &tdata);

	stop_progress(&tdata.prg);

	if (bitmap_equals(result, tdata.base))
		fprintf_ln(stderr, "OK!");
	else
		die(_("mismatch in bitmap results"));

	bitmap_free(result);
	bitmap_free(tdata.base);
	bitmap_free(tdata.commits);
	bitmap_free(tdata.trees);
	bitmap_free(tdata.blobs);
	bitmap_free(tdata.tags);
	free_bitmap_index(bitmap_git);
}

int test_bitmap_commits(struct repository *r)
{
	struct object_id oid;
	MAYBE_UNUSED void *value;
	struct bitmap_index *bitmap_git = prepare_bitmap_git(r);

	if (!bitmap_git)
		die(_("failed to load bitmap indexes"));

	/*
	 * As this function is only used to print bitmap selected
	 * commits, we don't have to read the commit table.
	 */
	if (bitmap_git->table_lookup) {
		if (load_bitmap_entries_v1(bitmap_git) < 0)
			die(_("failed to load bitmap indexes"));
	}

	kh_foreach(bitmap_git->bitmaps, oid, value, {
		printf_ln("%s", oid_to_hex(&oid));
	});

	free_bitmap_index(bitmap_git);

	return 0;
}

int test_bitmap_hashes(struct repository *r)
{
	struct bitmap_index *bitmap_git = prepare_bitmap_git(r);
	struct object_id oid;
	uint32_t i, index_pos;

	if (!bitmap_git || !bitmap_git->hashes)
		goto cleanup;

	for (i = 0; i < bitmap_num_objects(bitmap_git); i++) {
		if (bitmap_is_midx(bitmap_git))
			index_pos = pack_pos_to_midx(bitmap_git->midx, i);
		else
			index_pos = pack_pos_to_index(bitmap_git->pack, i);

		nth_bitmap_object_oid(bitmap_git, &oid, index_pos);

		printf_ln("%s %"PRIu32"",
		       oid_to_hex(&oid), get_be32(bitmap_git->hashes + index_pos));
	}

cleanup:
	free_bitmap_index(bitmap_git);

	return 0;
}

int rebuild_bitmap(const uint32_t *reposition,
		   struct ewah_bitmap *source,
		   struct bitmap *dest)
{
	uint32_t pos = 0;
	struct ewah_iterator it;
	eword_t word;

	ewah_iterator_init(&it, source);

	while (ewah_iterator_next(&word, &it)) {
		uint32_t offset, bit_pos;

		for (offset = 0; offset < BITS_IN_EWORD; ++offset) {
			if ((word >> offset) == 0)
				break;

			offset += ewah_bit_ctz64(word >> offset);

			bit_pos = reposition[pos + offset];
			if (bit_pos > 0)
				bitmap_set(dest, bit_pos - 1);
			else /* can't reuse, we don't have the object */
				return -1;
		}

		pos += BITS_IN_EWORD;
	}
	return 0;
}

uint32_t *create_bitmap_mapping(struct bitmap_index *bitmap_git,
				struct packing_data *mapping)
{
	uint32_t i, num_objects;
	uint32_t *reposition;

	if (!bitmap_is_midx(bitmap_git))
		load_reverse_index(bitmap_git);
	else if (load_midx_revindex(bitmap_git->midx) < 0)
		BUG("rebuild_existing_bitmaps: missing required rev-cache "
		    "extension");

	num_objects = bitmap_num_objects(bitmap_git);
	CALLOC_ARRAY(reposition, num_objects);

	for (i = 0; i < num_objects; ++i) {
		struct object_id oid;
		struct object_entry *oe;
		uint32_t index_pos;

		if (bitmap_is_midx(bitmap_git))
			index_pos = pack_pos_to_midx(bitmap_git->midx, i);
		else
			index_pos = pack_pos_to_index(bitmap_git->pack, i);
		nth_bitmap_object_oid(bitmap_git, &oid, index_pos);
		oe = packlist_find(mapping, &oid);

		if (oe) {
			reposition[i] = oe_in_pack_pos(mapping, oe) + 1;
			if (bitmap_git->hashes && !oe->hash)
				oe->hash = get_be32(bitmap_git->hashes + index_pos);
		}
	}

	return reposition;
}

void free_bitmap_index(struct bitmap_index *b)
{
	if (!b)
		return;

	if (b->map)
		munmap(b->map, b->map_size);
	ewah_pool_free(b->commits);
	ewah_pool_free(b->trees);
	ewah_pool_free(b->blobs);
	ewah_pool_free(b->tags);
	if (b->bitmaps) {
		struct stored_bitmap *sb;
		kh_foreach_value(b->bitmaps, sb, {
			ewah_pool_free(sb->root);
			free(sb);
		});
	}
	kh_destroy_oid_map(b->bitmaps);
	free(b->ext_index.objects);
	free(b->ext_index.hashes);
	kh_destroy_oid_pos(b->ext_index.positions);
	bitmap_free(b->result);
	bitmap_free(b->haves);
	if (bitmap_is_midx(b)) {
		/*
		 * Multi-pack bitmaps need to have resources associated with
		 * their on-disk reverse indexes unmapped so that stale .rev and
		 * .bitmap files can be removed.
		 *
		 * Unlike pack-based bitmaps, multi-pack bitmaps can be read and
		 * written in the same 'git multi-pack-index write --bitmap'
		 * process. Close resources so they can be removed safely on
		 * platforms like Windows.
		 */
		close_midx_revindex(b->midx);
	}
	free(b);
}

int bitmap_has_oid_in_uninteresting(struct bitmap_index *bitmap_git,
				    const struct object_id *oid)
{
	return bitmap_git &&
		bitmap_walk_contains(bitmap_git, bitmap_git->haves, oid);
}

static off_t get_disk_usage_for_type(struct bitmap_index *bitmap_git,
				     enum object_type object_type)
{
	struct bitmap *result = bitmap_git->result;
	off_t total = 0;
	struct ewah_iterator it;
	eword_t filter;
	size_t i;

	init_type_iterator(&it, bitmap_git, object_type);
	for (i = 0; i < result->word_alloc &&
			ewah_iterator_next(&filter, &it); i++) {
		eword_t word = result->words[i] & filter;
		size_t base = (i * BITS_IN_EWORD);
		unsigned offset;

		if (!word)
			continue;

		for (offset = 0; offset < BITS_IN_EWORD; offset++) {
			if ((word >> offset) == 0)
				break;

			offset += ewah_bit_ctz64(word >> offset);

			if (bitmap_is_midx(bitmap_git)) {
				uint32_t pack_pos;
				uint32_t midx_pos = pack_pos_to_midx(bitmap_git->midx, base + offset);
				off_t offset = nth_midxed_offset(bitmap_git->midx, midx_pos);

				uint32_t pack_id = nth_midxed_pack_int_id(bitmap_git->midx, midx_pos);
				struct packed_git *pack = bitmap_git->midx->packs[pack_id];

				if (offset_to_pack_pos(pack, offset, &pack_pos) < 0) {
					struct object_id oid;
					nth_midxed_object_oid(&oid, bitmap_git->midx, midx_pos);

					die(_("could not find '%s' in pack '%s' at offset %"PRIuMAX),
					    oid_to_hex(&oid),
					    pack->pack_name,
					    (uintmax_t)offset);
				}

				total += pack_pos_to_offset(pack, pack_pos + 1) - offset;
			} else {
				size_t pos = base + offset;
				total += pack_pos_to_offset(bitmap_git->pack, pos + 1) -
					 pack_pos_to_offset(bitmap_git->pack, pos);
			}
		}
	}

	return total;
}

static off_t get_disk_usage_for_extended(struct bitmap_index *bitmap_git)
{
	struct bitmap *result = bitmap_git->result;
	struct eindex *eindex = &bitmap_git->ext_index;
	off_t total = 0;
	struct object_info oi = OBJECT_INFO_INIT;
	off_t object_size;
	size_t i;

	oi.disk_sizep = &object_size;

	for (i = 0; i < eindex->count; i++) {
		struct object *obj = eindex->objects[i];

		if (!bitmap_get(result, bitmap_num_objects(bitmap_git) + i))
			continue;

		if (oid_object_info_extended(the_repository, &obj->oid, &oi, 0) < 0)
			die(_("unable to get disk usage of '%s'"),
			    oid_to_hex(&obj->oid));

		total += object_size;
	}
	return total;
}

off_t get_disk_usage_from_bitmap(struct bitmap_index *bitmap_git,
				 struct rev_info *revs)
{
	off_t total = 0;

	total += get_disk_usage_for_type(bitmap_git, OBJ_COMMIT);
	if (revs->tree_objects)
		total += get_disk_usage_for_type(bitmap_git, OBJ_TREE);
	if (revs->blob_objects)
		total += get_disk_usage_for_type(bitmap_git, OBJ_BLOB);
	if (revs->tag_objects)
		total += get_disk_usage_for_type(bitmap_git, OBJ_TAG);

	total += get_disk_usage_for_extended(bitmap_git);

	return total;
}

int bitmap_is_midx(struct bitmap_index *bitmap_git)
{
	return !!bitmap_git->midx;
}

const struct string_list *bitmap_preferred_tips(struct repository *r)
{
	const struct string_list *dest;

	if (!repo_config_get_string_multi(r, "pack.preferbitmaptips", &dest))
		return dest;
	return NULL;
}

int bitmap_is_preferred_refname(struct repository *r, const char *refname)
{
	const struct string_list *preferred_tips = bitmap_preferred_tips(r);
	struct string_list_item *item;

	if (!preferred_tips)
		return 0;

	for_each_string_list_item(item, preferred_tips) {
		if (starts_with(refname, item->string))
			return 1;
	}

	return 0;
}
