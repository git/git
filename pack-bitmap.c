#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "commit.h"
#include "gettext.h"
#include "hex.h"
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
#include "trace2.h"
#include "odb.h"
#include "list-objects-filter-options.h"
#include "midx.h"
#include "config.h"
#include "pseudo-merge.h"

/*
 * An entry on the bitmap index, representing the bitmap for a given
 * commit.
 */
struct stored_bitmap {
	struct object_id oid;
	struct ewah_bitmap *root;
	struct stored_bitmap *xor;
	size_t map_pos;
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
	 * If using a multi-pack index chain, 'base' points to the
	 * bitmap index corresponding to this bitmap's midx->base_midx.
	 *
	 * base_nr indicates how many layers precede this one, and is
	 * zero when base is NULL.
	 */
	struct bitmap_index *base;
	uint32_t base_nr;

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

	/*
	 * Type index arrays when this bitmap is associated with an
	 * incremental multi-pack index chain.
	 *
	 * If n is the number of unique layers in the MIDX chain, then
	 * commits_all[n-1] is this structs 'commits' field,
	 * commits_all[n-2] is the commits field of this bitmap's
	 * 'base', and so on.
	 *
	 * When associated either with a non-incremental MIDX or a
	 * single packfile, these arrays each contain a single element.
	 */
	struct ewah_bitmap **commits_all;
	struct ewah_bitmap **trees_all;
	struct ewah_bitmap **blobs_all;
	struct ewah_bitmap **tags_all;

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

	/* This contains the pseudo-merge cache within 'map' (if found). */
	struct pseudo_merge_map pseudo_merges;

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

static int pseudo_merges_satisfied_nr;
static int pseudo_merges_cascades_nr;
static int existing_bitmaps_hits_nr;
static int existing_bitmaps_misses_nr;
static int roots_with_bitmaps_nr;
static int roots_without_bitmaps_nr;

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

struct ewah_bitmap *read_bitmap(const unsigned char *map,
				size_t map_size, size_t *map_pos)
{
	struct ewah_bitmap *b = ewah_pool_new();

	ssize_t bitmap_size = ewah_read_mmap(b, map + *map_pos,
					     map_size - *map_pos);

	if (bitmap_size < 0) {
		error(_("failed to load bitmap index (corrupted?)"));
		ewah_pool_free(b);
		return NULL;
	}

	*map_pos += bitmap_size;

	return b;
}

/*
 * Read a bitmap from the current read position on the mmaped
 * index, and increase the read position accordingly
 */
static struct ewah_bitmap *read_bitmap_1(struct bitmap_index *index)
{
	return read_bitmap(index->map, index->map_size, &index->map_pos);
}

static uint32_t bitmap_num_objects_total(struct bitmap_index *index)
{
	if (index->midx) {
		struct multi_pack_index *m = index->midx;
		return m->num_objects + m->num_objects_in_base;
	}
	return index->pack->num_objects;
}

static uint32_t bitmap_num_objects(struct bitmap_index *index)
{
	if (index->midx)
		return index->midx->num_objects;
	return index->pack->num_objects;
}

static uint32_t bitmap_name_hash(struct bitmap_index *index, uint32_t pos)
{
	if (bitmap_is_midx(index)) {
		while (index && pos < index->midx->num_objects_in_base) {
			ASSERT(bitmap_is_midx(index));
			index = index->base;
		}

		if (!index)
			BUG("NULL base bitmap for object position: %"PRIu32, pos);

		pos -= index->midx->num_objects_in_base;
		if (pos >= index->midx->num_objects)
			BUG("out-of-bounds midx bitmap object at %"PRIu32, pos);
	}

	if (!index->hashes)
		return 0;

	return get_be32(index->hashes + pos);
}

static struct repository *bitmap_repo(struct bitmap_index *bitmap_git)
{
	if (bitmap_is_midx(bitmap_git))
		return bitmap_git->midx->source->odb->repo;
	return bitmap_git->pack->repo;
}

static int load_bitmap_header(struct bitmap_index *index)
{
	struct bitmap_disk_header *header = (void *)index->map;
	const struct git_hash_algo *hash_algo = bitmap_repo(index)->hash_algo;

	size_t header_size = sizeof(*header) - GIT_MAX_RAWSZ + hash_algo->rawsz;

	if (index->map_size < header_size + hash_algo->rawsz)
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
		unsigned char *index_end = index->map + index->map_size - hash_algo->rawsz;

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

		if (flags & BITMAP_OPT_PSEUDO_MERGES) {
			unsigned char *pseudo_merge_ofs;
			size_t table_size;
			uint32_t i;

			if (sizeof(table_size) > index_end - index->map - header_size)
				return error(_("corrupted bitmap index file (too short to fit pseudo-merge table header)"));

			table_size = get_be64(index_end - 8);
			if (table_size > index_end - index->map - header_size)
				return error(_("corrupted bitmap index file (too short to fit pseudo-merge table)"));

			if (git_env_bool("GIT_TEST_USE_PSEUDO_MERGES", 1)) {
				const unsigned char *ext = (index_end - table_size);

				index->pseudo_merges.map = index->map;
				index->pseudo_merges.map_size = index->map_size;
				index->pseudo_merges.commits = ext + get_be64(index_end - 16);
				index->pseudo_merges.commits_nr = get_be32(index_end - 20);
				index->pseudo_merges.nr = get_be32(index_end - 24);

				if (st_add(st_mult(index->pseudo_merges.nr,
						   sizeof(uint64_t)),
					   24) > table_size)
					return error(_("corrupted bitmap index file, pseudo-merge table too short"));

				CALLOC_ARRAY(index->pseudo_merges.v,
					     index->pseudo_merges.nr);

				pseudo_merge_ofs = index_end - 24 -
					(index->pseudo_merges.nr * sizeof(uint64_t));
				for (i = 0; i < index->pseudo_merges.nr; i++) {
					index->pseudo_merges.v[i].at = get_be64(pseudo_merge_ofs);
					pseudo_merge_ofs += sizeof(uint64_t);
				}
			}

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
					  int flags, size_t map_pos)
{
	struct stored_bitmap *stored;
	khiter_t hash_pos;
	int ret;

	stored = xmalloc(sizeof(struct stored_bitmap));
	stored->map_pos = map_pos;
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
		size_t entry_map_pos;

		if (index->map_size - index->map_pos < 6)
			return error(_("corrupt ewah bitmap: truncated header for entry %d"), i);

		entry_map_pos = index->map_pos;
		commit_idx_pos = read_be32(index->map, &index->map_pos);
		xor_offset = read_u8(index->map, &index->map_pos);
		flags = read_u8(index->map, &index->map_pos);

		if (nth_bitmap_object_oid(index, &oid, commit_idx_pos) < 0)
			return error(_("corrupt ewah bitmap: commit index %u out of range"),
				     (unsigned)commit_idx_pos);

		if (xor_offset > MAX_XOR_OFFSET || xor_offset > i)
			return error(_("corrupted bitmap pack index"));

		if (xor_offset > 0) {
			xor_bitmap = recent_bitmaps[(i - xor_offset) % MAX_XOR_OFFSET];

			if (!xor_bitmap)
				return error(_("invalid XOR offset in bitmap pack index"));
		}

		bitmap = read_bitmap_1(index);
		if (!bitmap)
			return -1;

		recent_bitmaps[i % MAX_XOR_OFFSET] =
			store_bitmap(index, bitmap, &oid, xor_bitmap, flags,
				     entry_map_pos);
	}

	return 0;
}

char *midx_bitmap_filename(struct multi_pack_index *midx)
{
	struct strbuf buf = STRBUF_INIT;
	if (midx->has_chain)
		get_split_midx_filename_ext(midx->source, &buf,
					    get_midx_hash(midx),
					    MIDX_EXT_BITMAP);
	else
		get_midx_filename_ext(midx->source, &buf,
				      get_midx_hash(midx),
				      MIDX_EXT_BITMAP);

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
		get_midx_filename(midx->source, &buf);
		trace2_data_string("bitmap", bitmap_repo(bitmap_git),
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

	if (!hasheq(get_midx_hash(bitmap_git->midx), bitmap_git->checksum,
		    bitmap_repo(bitmap_git)->hash_algo)) {
		error(_("checksum doesn't match in MIDX and bitmap"));
		goto cleanup;
	}

	if (load_midx_revindex(bitmap_git->midx)) {
		warning(_("multi-pack bitmap is missing required reverse index"));
		goto cleanup;
	}

	for (i = 0; i < bitmap_git->midx->num_packs + bitmap_git->midx->num_packs_in_base; i++) {
		if (prepare_midx_pack(bitmap_git->midx, i)) {
			warning(_("could not open pack %s"),
				bitmap_git->midx->pack_names[i]);
			goto cleanup;
		}
	}

	if (midx->base_midx) {
		bitmap_git->base = prepare_midx_bitmap_git(midx->base_midx);
		bitmap_git->base_nr = bitmap_git->base->base_nr + 1;
	} else {
		bitmap_git->base_nr = 0;
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
		trace2_data_string("bitmap", bitmap_repo(bitmap_git),
				   "ignoring extra bitmap file",
				   packfile->pack_name);
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
	bitmap_git->base_nr = 0;
	close(fd);

	if (load_bitmap_header(bitmap_git) < 0) {
		munmap(bitmap_git->map, bitmap_git->map_size);
		bitmap_git->map = NULL;
		bitmap_git->map_size = 0;
		bitmap_git->map_pos = 0;
		bitmap_git->pack = NULL;
		return -1;
	}

	trace2_data_string("bitmap", bitmap_repo(bitmap_git),
			   "opened bitmap file", packfile->pack_name);
	return 0;
}

static int load_reverse_index(struct repository *r, struct bitmap_index *bitmap_git)
{
	if (bitmap_is_midx(bitmap_git)) {
		struct multi_pack_index *m;

		/*
		 * The multi-pack-index's .rev file is already loaded via
		 * open_pack_bitmap_1().
		 *
		 * But we still need to open the individual pack .rev files,
		 * since we will need to make use of them in pack-objects.
		 */
		for (m = bitmap_git->midx; m; m = m->base_midx) {
			uint32_t i;
			int ret;

			for (i = 0; i < m->num_packs; i++) {
				ret = load_pack_revindex(r, m->packs[i]);
				if (ret)
					return ret;
			}
		}
		return 0;
	}
	return load_pack_revindex(r, bitmap_git->pack);
}

static void load_all_type_bitmaps(struct bitmap_index *bitmap_git)
{
	struct bitmap_index *curr = bitmap_git;
	size_t i = bitmap_git->base_nr;

	ALLOC_ARRAY(bitmap_git->commits_all, bitmap_git->base_nr + 1);
	ALLOC_ARRAY(bitmap_git->trees_all, bitmap_git->base_nr + 1);
	ALLOC_ARRAY(bitmap_git->blobs_all, bitmap_git->base_nr + 1);
	ALLOC_ARRAY(bitmap_git->tags_all, bitmap_git->base_nr + 1);

	while (curr) {
		bitmap_git->commits_all[i] = curr->commits;
		bitmap_git->trees_all[i] = curr->trees;
		bitmap_git->blobs_all[i] = curr->blobs;
		bitmap_git->tags_all[i] = curr->tags;

		curr = curr->base;
		if (curr && !i)
			BUG("unexpected number of bitmap layers, expected %"PRIu32,
			    bitmap_git->base_nr + 1);
		i -= 1;
	}
}

static int load_bitmap(struct repository *r, struct bitmap_index *bitmap_git,
		       int recursing)
{
	assert(bitmap_git->map);

	bitmap_git->bitmaps = kh_init_oid_map();
	bitmap_git->ext_index.positions = kh_init_oid_pos();

	if (load_reverse_index(r, bitmap_git))
		return -1;

	if (!(bitmap_git->commits = read_bitmap_1(bitmap_git)) ||
		!(bitmap_git->trees = read_bitmap_1(bitmap_git)) ||
		!(bitmap_git->blobs = read_bitmap_1(bitmap_git)) ||
		!(bitmap_git->tags = read_bitmap_1(bitmap_git)))
		return -1;

	if (!bitmap_git->table_lookup && load_bitmap_entries_v1(bitmap_git) < 0)
		return -1;

	if (bitmap_git->base) {
		if (!bitmap_is_midx(bitmap_git))
			BUG("non-MIDX bitmap has non-NULL base bitmap index");
		if (load_bitmap(r, bitmap_git->base, 1) < 0)
			return -1;
	}

	if (!recursing)
		load_all_type_bitmaps(bitmap_git);

	return 0;
}

static int open_pack_bitmap(struct repository *r,
			    struct bitmap_index *bitmap_git)
{
	struct packed_git *p;
	int ret = -1;

	repo_for_each_pack(r, p) {
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
	struct odb_source *source;
	int ret = -1;

	assert(!bitmap_git->map);

	odb_prepare_alternates(r->objects);
	for (source = r->objects->sources; source; source = source->next) {
		struct multi_pack_index *midx = get_multi_pack_index(source);
		if (midx && !open_midx_bitmap_1(bitmap_git, midx))
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

	if (!open_bitmap(r, bitmap_git) && !load_bitmap(r, bitmap_git, 0))
		return bitmap_git;

	free_bitmap_index(bitmap_git);
	return NULL;
}

struct bitmap_index *prepare_midx_bitmap_git(struct multi_pack_index *midx)
{
	struct bitmap_index *bitmap_git = xcalloc(1, sizeof(*bitmap_git));

	if (!open_midx_bitmap_1(bitmap_git, midx))
		return bitmap_git;

	free_bitmap_index(bitmap_git);
	return NULL;
}

int bitmap_index_contains_pack(struct bitmap_index *bitmap, struct packed_git *pack)
{
	for (; bitmap; bitmap = bitmap->base) {
		if (bitmap_is_midx(bitmap)) {
			for (size_t i = 0; i < bitmap->midx->num_packs; i++)
				if (bitmap->midx->packs[i] == pack)
					return 1;
		} else if (bitmap->pack == pack) {
			return 1;
		}
	}

	return 0;
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
	size_t entry_map_pos;

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

		entry_map_pos = bitmap_git->map_pos;
		bitmap_git->map_pos += sizeof(uint32_t) + sizeof(uint8_t);
		xor_flags = read_u8(bitmap_git->map, &bitmap_git->map_pos);
		bitmap = read_bitmap_1(bitmap_git);

		if (!bitmap)
			goto corrupt;

		xor_bitmap = store_bitmap(bitmap_git, bitmap, &xor_item->oid,
					  xor_bitmap, xor_flags, entry_map_pos);
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
	entry_map_pos = bitmap_git->map_pos;
	bitmap_git->map_pos += sizeof(uint32_t) + sizeof(uint8_t);
	flags = read_u8(bitmap_git->map, &bitmap_git->map_pos);
	bitmap = read_bitmap_1(bitmap_git);

	if (!bitmap)
		goto corrupt;

	return store_bitmap(bitmap_git, bitmap, oid, xor_bitmap, flags,
			    entry_map_pos);

corrupt:
	free(xor_items);
	is_corrupt = 1;
	return NULL;
}

static struct ewah_bitmap *find_bitmap_for_commit(struct bitmap_index *bitmap_git,
						  struct commit *commit,
						  struct bitmap_index **found)
{
	khiter_t hash_pos;
	if (!bitmap_git)
		return NULL;

	hash_pos = kh_get_oid_map(bitmap_git->bitmaps, commit->object.oid);
	if (hash_pos >= kh_end(bitmap_git->bitmaps)) {
		struct stored_bitmap *bitmap = NULL;
		if (!bitmap_git->table_lookup)
			return find_bitmap_for_commit(bitmap_git->base, commit,
						      found);

		/* this is a fairly hot codepath - no trace2_region please */
		/* NEEDSWORK: cache misses aren't recorded */
		bitmap = lazy_bitmap_for_commit(bitmap_git, commit);
		if (!bitmap)
			return find_bitmap_for_commit(bitmap_git->base, commit,
						      found);
		if (found)
			*found = bitmap_git;
		return lookup_stored_bitmap(bitmap);
	}
	if (found)
		*found = bitmap_git;
	return lookup_stored_bitmap(kh_value(bitmap_git->bitmaps, hash_pos));
}

struct ewah_bitmap *bitmap_for_commit(struct bitmap_index *bitmap_git,
				      struct commit *commit)
{
	return find_bitmap_for_commit(bitmap_git, commit, NULL);
}

static inline int bitmap_position_extended(struct bitmap_index *bitmap_git,
					   const struct object_id *oid)
{
	kh_oid_pos_t *positions = bitmap_git->ext_index.positions;
	khiter_t pos = kh_get_oid_pos(positions, *oid);

	if (pos < kh_end(positions)) {
		int bitmap_pos = kh_value(positions, pos);
		return bitmap_pos + bitmap_num_objects_total(bitmap_git);
	}

	return -1;
}

static inline int bitmap_position_packfile(struct bitmap_index *bitmap_git,
					   const struct object_id *oid)
{
	uint32_t pos;
	off_t offset = find_pack_entry_one(oid, bitmap_git->pack);
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

	return bitmap_pos + bitmap_num_objects_total(bitmap_git);
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

static void show_commit(struct commit *commit UNUSED,
			void *data UNUSED)
{
}

static unsigned apply_pseudo_merges_for_commit_1(struct bitmap_index *bitmap_git,
						 struct bitmap *result,
						 struct commit *commit,
						 uint32_t commit_pos)
{
	struct bitmap_index *curr = bitmap_git;
	int ret = 0;

	while (curr) {
		ret += apply_pseudo_merges_for_commit(&curr->pseudo_merges,
						      result, commit,
						      commit_pos);
		curr = curr->base;
	}

	if (ret)
		pseudo_merges_satisfied_nr += ret;

	return ret;
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
		existing_bitmaps_hits_nr++;

		bitmap_or_ewah(data->base, partial);
		return 0;
	}

	existing_bitmaps_misses_nr++;

	bitmap_set(data->base, bitmap_pos);
	if (apply_pseudo_merges_for_commit_1(bitmap_git, data->base, commit,
					     bitmap_pos))
		return 0;

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

	if (!or_with) {
		existing_bitmaps_misses_nr++;
		return 0;
	}

	existing_bitmaps_hits_nr++;

	if (!*base)
		*base = ewah_to_bitmap(or_with);
	else
		bitmap_or_ewah(*base, or_with);

	return 1;
}

static struct bitmap *fill_in_bitmap(struct bitmap_index *bitmap_git,
				     struct rev_info *revs,
				     struct bitmap *base,
				     struct bitmap *seen)
{
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

	traverse_commit_list(revs, show_commit, show_object, &show_data);

	revs->include_check = NULL;
	revs->include_check_obj = NULL;
	revs->include_check_data = NULL;

	return base;
}

struct bitmap_boundary_cb {
	struct bitmap_index *bitmap_git;
	struct bitmap *base;

	struct object_array boundary;
};

static void show_boundary_commit(struct commit *commit, void *_data)
{
	struct bitmap_boundary_cb *data = _data;

	if (commit->object.flags & BOUNDARY)
		add_object_array(&commit->object, "", &data->boundary);

	if (commit->object.flags & UNINTERESTING) {
		if (bitmap_walk_contains(data->bitmap_git, data->base,
					 &commit->object.oid))
			return;

		add_commit_to_bitmap(data->bitmap_git, &data->base, commit);
	}
}

static void show_boundary_object(struct object *object UNUSED,
				 const char *name UNUSED,
				 void *data UNUSED)
{
	BUG("should not be called");
}

static unsigned cascade_pseudo_merges_1(struct bitmap_index *bitmap_git,
					struct bitmap *result,
					struct bitmap *roots)
{
	int ret = cascade_pseudo_merges(&bitmap_git->pseudo_merges,
					result, roots);
	if (ret) {
		pseudo_merges_cascades_nr++;
		pseudo_merges_satisfied_nr += ret;
	}

	return ret;
}

static struct bitmap *find_boundary_objects(struct bitmap_index *bitmap_git,
					    struct rev_info *revs,
					    struct object_list *roots)
{
	struct bitmap_boundary_cb cb;
	struct object_list *root;
	struct repository *repo;
	unsigned int i;
	unsigned int tmp_blobs, tmp_trees, tmp_tags;
	int any_missing = 0;
	int existing_bitmaps = 0;

	cb.bitmap_git = bitmap_git;
	cb.base = bitmap_new();
	object_array_init(&cb.boundary);

	repo = bitmap_repo(bitmap_git);

	revs->ignore_missing_links = 1;

	if (bitmap_git->pseudo_merges.nr) {
		struct bitmap *roots_bitmap = bitmap_new();
		struct object_list *objects = NULL;

		for (objects = roots; objects; objects = objects->next) {
			struct object *object = objects->item;
			int pos;

			pos = bitmap_position(bitmap_git, &object->oid);
			if (pos < 0)
				continue;

			bitmap_set(roots_bitmap, pos);
		}

		cascade_pseudo_merges_1(bitmap_git, cb.base, roots_bitmap);
		bitmap_free(roots_bitmap);
	}

	/*
	 * OR in any existing reachability bitmaps among `roots` into
	 * `cb.base`.
	 */
	for (root = roots; root; root = root->next) {
		struct object *object = root->item;
		if (object->type != OBJ_COMMIT ||
		    bitmap_walk_contains(bitmap_git, cb.base, &object->oid))
			continue;

		if (add_commit_to_bitmap(bitmap_git, &cb.base,
					 (struct commit *)object)) {
			existing_bitmaps = 1;
			continue;
		}

		any_missing = 1;
	}

	if (!any_missing)
		goto cleanup;

	if (existing_bitmaps)
		cascade_pseudo_merges_1(bitmap_git, cb.base, NULL);

	tmp_blobs = revs->blob_objects;
	tmp_trees = revs->tree_objects;
	tmp_tags = revs->tag_objects;
	revs->blob_objects = 0;
	revs->tree_objects = 0;
	revs->tag_objects = 0;

	/*
	 * We didn't have complete coverage of the roots. First setup a
	 * revision walk to (a) OR in any bitmaps that are UNINTERESTING
	 * between the tips and boundary, and (b) record the boundary.
	 */
	trace2_region_enter("pack-bitmap", "boundary-prepare", repo);
	if (prepare_revision_walk(revs))
		die("revision walk setup failed");
	trace2_region_leave("pack-bitmap", "boundary-prepare", repo);

	trace2_region_enter("pack-bitmap", "boundary-traverse", repo);
	revs->boundary = 1;
	traverse_commit_list_filtered(revs,
				      show_boundary_commit,
				      show_boundary_object,
				      &cb, NULL);
	revs->boundary = 0;
	trace2_region_leave("pack-bitmap", "boundary-traverse", repo);

	revs->blob_objects = tmp_blobs;
	revs->tree_objects = tmp_trees;
	revs->tag_objects = tmp_tags;

	reset_revision_walk();
	clear_object_flags(repo, UNINTERESTING);

	/*
	 * Then add the boundary commit(s) as fill-in traversal tips.
	 */
	trace2_region_enter("pack-bitmap", "boundary-fill-in", repo);
	for (i = 0; i < cb.boundary.nr; i++) {
		struct object *obj = cb.boundary.objects[i].item;
		if (bitmap_walk_contains(bitmap_git, cb.base, &obj->oid))
			obj->flags |= SEEN;
		else
			add_pending_object(revs, obj, "");
	}
	if (revs->pending.nr)
		cb.base = fill_in_bitmap(bitmap_git, revs, cb.base, NULL);
	trace2_region_leave("pack-bitmap", "boundary-fill-in", repo);

cleanup:
	object_array_clear(&cb.boundary);
	revs->ignore_missing_links = 0;

	return cb.base;
}

struct ewah_bitmap *pseudo_merge_bitmap_for_commit(struct bitmap_index *bitmap_git,
						   struct commit *commit)
{
	struct commit_list *p;
	struct bitmap *parents;
	struct pseudo_merge *match = NULL;

	if (!bitmap_git->pseudo_merges.nr)
		return NULL;

	parents = bitmap_new();

	for (p = commit->parents; p; p = p->next) {
		int pos = bitmap_position(bitmap_git, &p->item->object.oid);
		if (pos < 0 || pos >= bitmap_num_objects(bitmap_git))
			goto done;

		/*
		 * Use bitmap-relative positions instead of offsetting
		 * by bitmap_git->num_objects_in_base because we use
		 * this to find a match in pseudo_merge_for_parents(),
		 * and pseudo-merge groups cannot span multiple bitmap
		 * layers.
		 */
		bitmap_set(parents, pos);
	}

	match = pseudo_merge_for_parents(&bitmap_git->pseudo_merges, parents);

done:
	bitmap_free(parents);
	if (match)
		return pseudo_merge_bitmap(&bitmap_git->pseudo_merges, match);

	return NULL;
}

static void unsatisfy_all_pseudo_merges(struct bitmap_index *bitmap_git)
{
	uint32_t i;
	for (i = 0; i < bitmap_git->pseudo_merges.nr; i++)
		bitmap_git->pseudo_merges.v[i].satisfied = 0;
}

static struct bitmap *find_objects(struct bitmap_index *bitmap_git,
				   struct rev_info *revs,
				   struct object_list *roots,
				   struct bitmap *seen)
{
	struct bitmap *base = NULL;
	int needs_walk = 0;
	unsigned existing_bitmaps = 0;

	struct object_list *not_mapped = NULL;

	unsatisfy_all_pseudo_merges(bitmap_git);

	if (bitmap_git->pseudo_merges.nr) {
		struct bitmap *roots_bitmap = bitmap_new();
		struct object_list *objects = NULL;

		for (objects = roots; objects; objects = objects->next) {
			struct object *object = objects->item;
			int pos;

			pos = bitmap_position(bitmap_git, &object->oid);
			if (pos < 0)
				continue;

			bitmap_set(roots_bitmap, pos);
		}

		base = bitmap_new();
		cascade_pseudo_merges_1(bitmap_git, base, roots_bitmap);
		bitmap_free(roots_bitmap);
	}

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

		if (base) {
			int pos = bitmap_position(bitmap_git, &object->oid);
			if (pos > 0 && bitmap_get(base, pos)) {
				object->flags |= SEEN;
				continue;
			}
		}

		if (object->type == OBJ_COMMIT &&
		    add_commit_to_bitmap(bitmap_git, &base, (struct commit *)object)) {
			object->flags |= SEEN;
			existing_bitmaps = 1;
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

	if (existing_bitmaps)
		cascade_pseudo_merges_1(bitmap_git, base, NULL);

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

			roots_without_bitmaps_nr++;
		} else {
			object->flags |= SEEN;

			roots_with_bitmaps_nr++;
		}
	}

	if (needs_walk) {
		/*
		 * This fill-in traversal may walk over some objects
		 * again, since we have already traversed in order to
		 * find the boundary.
		 *
		 * But this extra walk should be extremely cheap, since
		 * all commit objects are loaded into memory, and
		 * because we skip walking to parents that are
		 * UNINTERESTING, since it will be marked in the haves
		 * bitmap already (or it has an on-disk bitmap, since
		 * OR-ing it in covers all of its ancestors).
		 */
		base = fill_in_bitmap(bitmap_git, revs, base, seen);
	}

	object_list_free(&not_mapped);

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

		if (!bitmap_get(objects,
				st_add(bitmap_num_objects_total(bitmap_git),
				       i)))
			continue;

		obj = eindex->objects[i];
		if ((obj->type == OBJ_BLOB && !revs->blob_objects) ||
		    (obj->type == OBJ_TREE && !revs->tree_objects) ||
		    (obj->type == OBJ_TAG && !revs->tag_objects))
			continue;

		show_reach(&obj->oid, obj->type, 0, eindex->hashes[i], NULL, 0, NULL);
	}
}

static void init_type_iterator(struct ewah_or_iterator *it,
			       struct bitmap_index *bitmap_git,
			       enum object_type type)
{
	switch (type) {
	case OBJ_COMMIT:
		ewah_or_iterator_init(it, bitmap_git->commits_all,
				      bitmap_git->base_nr + 1);
		break;

	case OBJ_TREE:
		ewah_or_iterator_init(it, bitmap_git->trees_all,
				      bitmap_git->base_nr + 1);
		break;

	case OBJ_BLOB:
		ewah_or_iterator_init(it, bitmap_git->blobs_all,
				      bitmap_git->base_nr + 1);
		break;

	case OBJ_TAG:
		ewah_or_iterator_init(it, bitmap_git->tags_all,
				      bitmap_git->base_nr + 1);
		break;

	default:
		BUG("object type %d not stored by bitmap type index", type);
		break;
	}
}

static void show_objects_for_type(
	struct bitmap_index *bitmap_git,
	struct bitmap *objects,
	enum object_type object_type,
	show_reachable_fn show_reach,
	void *payload)
{
	size_t i = 0;
	uint32_t offset;

	struct ewah_or_iterator it;
	eword_t filter;

	init_type_iterator(&it, bitmap_git, object_type);

	for (i = 0; i < objects->word_alloc &&
			ewah_or_iterator_next(&filter, &it); i++) {
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
				pack = nth_midxed_pack(bitmap_git->midx, pack_id);
			} else {
				index_pos = pack_pos_to_index(bitmap_git->pack, pos + offset);
				ofs = pack_pos_to_offset(bitmap_git->pack, pos + offset);
				nth_bitmap_object_oid(bitmap_git, &oid, index_pos);

				pack = bitmap_git->pack;
			}

			hash = bitmap_name_hash(bitmap_git, index_pos);

			show_reach(&oid, object_type, 0, hash, pack, ofs, payload);
		}
	}

	ewah_or_iterator_release(&it);
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
			if (find_pack_entry_one(&object->oid, bitmap_git->pack) > 0)
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
	struct ewah_or_iterator it;
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
	     i < to_filter->word_alloc && ewah_or_iterator_next(&mask, &it);
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
		size_t pos = st_add(i, bitmap_num_objects_total(bitmap_git));
		if (eindex->objects[i]->type == type &&
		    bitmap_get(to_filter, pos) &&
		    !bitmap_get(tips, pos))
			bitmap_unset(to_filter, pos);
	}

	ewah_or_iterator_release(&it);
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

	if (pos < bitmap_num_objects_total(bitmap_git)) {
		struct packed_git *pack;
		off_t ofs;

		if (bitmap_is_midx(bitmap_git)) {
			uint32_t midx_pos = pack_pos_to_midx(bitmap_git->midx, pos);
			uint32_t pack_id = nth_midxed_pack_int_id(bitmap_git->midx, midx_pos);

			pack = nth_midxed_pack(bitmap_git->midx, pack_id);
			ofs = nth_midxed_offset(bitmap_git->midx, midx_pos);
		} else {
			pack = bitmap_git->pack;
			ofs = pack_pos_to_offset(pack, pos);
		}

		if (packed_object_info(pack, ofs, &oi) < 0) {
			struct object_id oid;
			nth_bitmap_object_oid(bitmap_git, &oid,
					      pack_pos_to_index(pack, pos));
			die(_("unable to get size of %s"), oid_to_hex(&oid));
		}
	} else {
		size_t eindex_pos = pos - bitmap_num_objects_total(bitmap_git);
		struct eindex *eindex = &bitmap_git->ext_index;
		struct object *obj = eindex->objects[eindex_pos];
		if (odb_read_object_info_extended(bitmap_repo(bitmap_git)->objects, &obj->oid,
						  &oi, 0) < 0)
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
	struct ewah_or_iterator it;
	eword_t mask;
	uint32_t i;

	tips = find_tip_objects(bitmap_git, tip_objects, OBJ_BLOB);

	for (i = 0, init_type_iterator(&it, bitmap_git, OBJ_BLOB);
	     i < to_filter->word_alloc && ewah_or_iterator_next(&mask, &it);
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
		size_t pos = st_add(i, bitmap_num_objects(bitmap_git));
		if (eindex->objects[i]->type == OBJ_BLOB &&
		    bitmap_get(to_filter, pos) &&
		    !bitmap_get(tips, pos) &&
		    get_size_by_pos(bitmap_git, pos) >= limit)
			bitmap_unset(to_filter, pos);
	}

	ewah_or_iterator_release(&it);
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


static void filter_packed_objects_from_bitmap(struct bitmap_index *bitmap_git,
					      struct bitmap *result)
{
	struct eindex *eindex = &bitmap_git->ext_index;
	uint32_t objects_nr;
	size_t i, pos;

	objects_nr = bitmap_num_objects_total(bitmap_git);
	pos = objects_nr / BITS_IN_EWORD;

	if (pos > result->word_alloc)
		pos = result->word_alloc;

	memset(result->words, 0x00, sizeof(eword_t) * pos);
	for (i = pos * BITS_IN_EWORD; i < objects_nr; i++)
		bitmap_unset(result, i);

	for (i = 0; i < eindex->count; ++i) {
		if (has_object_pack(bitmap_repo(bitmap_git),
				    &eindex->objects[i]->oid))
			bitmap_unset(result, objects_nr + i);
	}
}

int for_each_bitmapped_object(struct bitmap_index *bitmap_git,
			      struct list_objects_filter_options *filter,
			      show_reachable_fn show_reach,
			      void *payload)
{
	struct bitmap *filtered_bitmap = NULL;
	uint32_t objects_nr;
	size_t full_word_count;
	int ret;

	if (!can_filter_bitmap(filter)) {
		ret = -1;
		goto out;
	}

	objects_nr = bitmap_num_objects(bitmap_git);
	full_word_count = objects_nr / BITS_IN_EWORD;

	/* We start from the all-1 bitmap and then filter down from there. */
	filtered_bitmap = bitmap_word_alloc(full_word_count + !!(objects_nr % BITS_IN_EWORD));
	memset(filtered_bitmap->words, 0xff, full_word_count * sizeof(*filtered_bitmap->words));
	for (size_t i = full_word_count * BITS_IN_EWORD; i < objects_nr; i++)
		bitmap_set(filtered_bitmap, i);

	if (filter_bitmap(bitmap_git, NULL, filtered_bitmap, filter) < 0) {
		ret = -1;
		goto out;
	}

	show_objects_for_type(bitmap_git, filtered_bitmap,
			      OBJ_COMMIT, show_reach, payload);
	show_objects_for_type(bitmap_git, filtered_bitmap,
			      OBJ_TREE, show_reach, payload);
	show_objects_for_type(bitmap_git, filtered_bitmap,
			      OBJ_BLOB, show_reach, payload);
	show_objects_for_type(bitmap_git, filtered_bitmap,
			      OBJ_TAG, show_reach, payload);

	ret = 0;
out:
	bitmap_free(filtered_bitmap);
	return ret;
}

struct bitmap_index *prepare_bitmap_walk(struct rev_info *revs,
					 int filter_provided_objects)
{
	unsigned int i;
	int use_boundary_traversal;

	struct object_list *wants = NULL;
	struct object_list *haves = NULL;

	struct bitmap *wants_bitmap = NULL;
	struct bitmap *haves_bitmap = NULL;

	struct bitmap_index *bitmap_git;
	struct repository *repo;

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
			parse_object_or_die(revs->repo, &object->oid, NULL);

		while (object->type == OBJ_TAG) {
			struct tag *tag = (struct tag *) object;

			if (object->flags & UNINTERESTING)
				object_list_insert(object, &haves);
			else
				object_list_insert(object, &wants);

			object = parse_object_or_die(revs->repo, get_tagged_oid(tag), NULL);
			object->flags |= (tag->object.flags & UNINTERESTING);
		}

		if (object->flags & UNINTERESTING)
			object_list_insert(object, &haves);
		else
			object_list_insert(object, &wants);
	}

	use_boundary_traversal = git_env_bool(GIT_TEST_PACK_USE_BITMAP_BOUNDARY_TRAVERSAL, -1);
	if (use_boundary_traversal < 0) {
		prepare_repo_settings(revs->repo);
		use_boundary_traversal = revs->repo->settings.pack_use_bitmap_boundary_traversal;
	}

	if (!use_boundary_traversal) {
		/*
		 * if we have a HAVES list, but none of those haves is contained
		 * in the packfile that has a bitmap, we don't have anything to
		 * optimize here
		 */
		if (haves && !in_bitmapped_pack(bitmap_git, haves))
			goto cleanup;
	}

	/* if we don't want anything, we're done here */
	if (!wants)
		goto cleanup;

	/*
	 * now we're going to use bitmaps, so load the actual bitmap entries
	 * from disk. this is the point of no return; after this the rev_list
	 * becomes invalidated and we must perform the revwalk through bitmaps
	 */
	if (load_bitmap(revs->repo, bitmap_git, 0) < 0)
		goto cleanup;

	if (!use_boundary_traversal)
		object_array_clear(&revs->pending);

	repo = bitmap_repo(bitmap_git);

	if (haves) {
		if (use_boundary_traversal) {
			trace2_region_enter("pack-bitmap", "haves/boundary", repo);
			haves_bitmap = find_boundary_objects(bitmap_git, revs, haves);
			trace2_region_leave("pack-bitmap", "haves/boundary", repo);
		} else {
			trace2_region_enter("pack-bitmap", "haves/classic", repo);
			revs->ignore_missing_links = 1;
			haves_bitmap = find_objects(bitmap_git, revs, haves, NULL);
			reset_revision_walk();
			revs->ignore_missing_links = 0;
			trace2_region_leave("pack-bitmap", "haves/classic", repo);
		}

		if (!haves_bitmap)
			BUG("failed to perform bitmap walk");
	}

	if (use_boundary_traversal) {
		object_array_clear(&revs->pending);
		reset_revision_walk();
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

	if (revs->unpacked)
		filter_packed_objects_from_bitmap(bitmap_git, wants_bitmap);

	bitmap_git->result = wants_bitmap;
	bitmap_git->haves = haves_bitmap;

	object_list_free(&wants);
	object_list_free(&haves);

	trace2_data_intmax("bitmap", repo, "pseudo_merges_satisfied",
			   pseudo_merges_satisfied_nr);
	trace2_data_intmax("bitmap", repo, "pseudo_merges_cascades",
			   pseudo_merges_cascades_nr);
	trace2_data_intmax("bitmap", repo, "bitmap/hits",
			   existing_bitmaps_hits_nr);
	trace2_data_intmax("bitmap", repo, "bitmap/misses",
			   existing_bitmaps_misses_nr);
	trace2_data_intmax("bitmap", repo, "bitmap/roots_with_bitmap",
			   roots_with_bitmaps_nr);
	trace2_data_intmax("bitmap", repo, "bitmap/roots_without_bitmap",
			   roots_without_bitmaps_nr);

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
static int try_partial_reuse(struct bitmap_index *bitmap_git,
			     struct bitmapped_pack *pack,
			     size_t bitmap_pos,
			     uint32_t pack_pos,
			     off_t offset,
			     struct bitmap *reuse,
			     struct pack_window **w_curs)
{
	off_t delta_obj_offset;
	enum object_type type;
	unsigned long size;

	if (pack_pos >= pack->p->num_objects)
		return -1; /* not actually in the pack */

	delta_obj_offset = offset;
	type = unpack_object_header(pack->p, w_curs, &offset, &size);
	if (type < 0)
		return -1; /* broken packfile, punt */

	if (type == OBJ_REF_DELTA || type == OBJ_OFS_DELTA) {
		off_t base_offset;
		uint32_t base_pos;
		uint32_t base_bitmap_pos;

		/*
		 * Find the position of the base object so we can look it up
		 * in our bitmaps. If we can't come up with an offset, or if
		 * that offset is not in the revidx, the pack is corrupt.
		 * There's nothing we can do, so just punt on this object,
		 * and the normal slow path will complain about it in
		 * more detail.
		 */
		base_offset = get_delta_base(pack->p, w_curs, &offset, type,
					     delta_obj_offset);
		if (!base_offset)
			return 0;

		offset_to_pack_pos(pack->p, base_offset, &base_pos);

		if (bitmap_is_midx(bitmap_git)) {
			/*
			 * Cross-pack deltas are rejected for now, but could
			 * theoretically be supported in the future.
			 *
			 * We would need to ensure that we're sending both
			 * halves of the delta/base pair, regardless of whether
			 * or not the two cross a pack boundary. If they do,
			 * then we must convert the delta to an REF_DELTA to
			 * refer back to the base in the other pack.
			 * */
			if (midx_pair_to_pack_pos(bitmap_git->midx,
						  pack->pack_int_id,
						  base_offset,
						  &base_bitmap_pos) < 0) {
				return 0;
			}
		} else {
			if (offset_to_pack_pos(pack->p, base_offset,
					       &base_pos) < 0)
				return 0;
			/*
			 * We assume delta dependencies always point backwards.
			 * This lets us do a single pass, and is basically
			 * always true due to the way OFS_DELTAs work. You would
			 * not typically find REF_DELTA in a bitmapped pack,
			 * since we only bitmap packs we write fresh, and
			 * OFS_DELTA is the default). But let's double check to
			 * make sure the pack wasn't written with odd
			 * parameters.
			 */
			if (base_pos >= pack_pos)
				return 0;
			base_bitmap_pos = pack->bitmap_pos + base_pos;
		}

		/*
		 * And finally, if we're not sending the base as part of our
		 * reuse chunk, then don't send this object either. The base
		 * would come after us, along with other objects not
		 * necessarily in the pack, which means we'd need to convert
		 * to REF_DELTA on the fly. Better to just let the normal
		 * object_entry code path handle it.
		 */
		if (!bitmap_get(reuse, base_bitmap_pos))
			return 0;
	}

	/*
	 * If we got here, then the object is OK to reuse. Mark it.
	 */
	bitmap_set(reuse, bitmap_pos);
	return 0;
}

static void reuse_partial_packfile_from_bitmap_1(struct bitmap_index *bitmap_git,
						 struct bitmapped_pack *pack,
						 struct bitmap *reuse)
{
	struct bitmap *result = bitmap_git->result;
	struct pack_window *w_curs = NULL;
	size_t pos = pack->bitmap_pos / BITS_IN_EWORD;

	if (!pack->bitmap_pos) {
		/*
		 * If we're processing the first (in the case of a MIDX, the
		 * preferred pack) or the only (in the case of single-pack
		 * bitmaps) pack, then we can reuse whole words at a time.
		 *
		 * This is because we know that any deltas in this range *must*
		 * have their bases chosen from the same pack, since:
		 *
		 * - In the single pack case, there is no other pack to choose
		 *   them from.
		 *
		 * - In the MIDX case, the first pack is the preferred pack, so
		 *   all ties are broken in favor of that pack (i.e. the one
		 *   we're currently processing). So any duplicate bases will be
		 *   resolved in favor of the pack we're processing.
		 */
		while (pos < result->word_alloc &&
		       pos < pack->bitmap_nr / BITS_IN_EWORD &&
		       result->words[pos] == (eword_t)~0)
			pos++;
		memset(reuse->words, 0xFF, pos * sizeof(eword_t));
	}

	for (; pos < result->word_alloc; pos++) {
		eword_t word = result->words[pos];
		size_t offset;

		for (offset = 0; offset < BITS_IN_EWORD; offset++) {
			size_t bit_pos;
			uint32_t pack_pos;
			off_t ofs;

			if (word >> offset == 0)
				break;

			offset += ewah_bit_ctz64(word >> offset);

			bit_pos = pos * BITS_IN_EWORD + offset;
			if (bit_pos < pack->bitmap_pos)
				continue;
			if (bit_pos >= pack->bitmap_pos + pack->bitmap_nr)
				goto done;

			if (bitmap_is_midx(bitmap_git)) {
				uint32_t midx_pos;

				midx_pos = pack_pos_to_midx(bitmap_git->midx, bit_pos);
				ofs = nth_midxed_offset(bitmap_git->midx, midx_pos);

				if (offset_to_pack_pos(pack->p, ofs, &pack_pos) < 0)
					BUG("could not find object in pack %s "
					    "at offset %"PRIuMAX" in MIDX",
					    pack_basename(pack->p), (uintmax_t)ofs);
			} else {
				pack_pos = cast_size_t_to_uint32_t(st_sub(bit_pos, pack->bitmap_pos));
				if (pack_pos >= pack->p->num_objects)
					BUG("advanced beyond the end of pack %s (%"PRIuMAX" > %"PRIu32")",
					    pack_basename(pack->p), (uintmax_t)pack_pos,
					    pack->p->num_objects);

				ofs = pack_pos_to_offset(pack->p, pack_pos);
			}

			if (try_partial_reuse(bitmap_git, pack, bit_pos,
					      pack_pos, ofs, reuse, &w_curs) < 0) {
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
}

static int bitmapped_pack_cmp(const void *va, const void *vb)
{
	const struct bitmapped_pack *a = va;
	const struct bitmapped_pack *b = vb;

	if (a->bitmap_pos < b->bitmap_pos)
		return -1;
	if (a->bitmap_pos > b->bitmap_pos)
		return 1;
	return 0;
}

void reuse_partial_packfile_from_bitmap(struct bitmap_index *bitmap_git,
					struct bitmapped_pack **packs_out,
					size_t *packs_nr_out,
					struct bitmap **reuse_out,
					int multi_pack_reuse)
{
	struct repository *r = bitmap_repo(bitmap_git);
	struct bitmapped_pack *packs = NULL;
	struct bitmap *result = bitmap_git->result;
	struct bitmap *reuse;
	size_t i;
	size_t packs_nr = 0, packs_alloc = 0;
	size_t word_alloc;
	uint32_t objects_nr = 0;

	assert(result);

	load_reverse_index(r, bitmap_git);

	if (!bitmap_is_midx(bitmap_git) || !bitmap_git->midx->chunk_bitmapped_packs)
		multi_pack_reuse = 0;

	if (multi_pack_reuse) {
		struct multi_pack_index *m = bitmap_git->midx;
		for (i = 0; i < m->num_packs + m->num_packs_in_base; i++) {
			struct bitmapped_pack pack;
			if (nth_bitmapped_pack(bitmap_git->midx, &pack, i) < 0) {
				warning(_("unable to load pack: '%s', disabling pack-reuse"),
					bitmap_git->midx->pack_names[i]);
				free(packs);
				return;
			}

			if (!pack.bitmap_nr)
				continue;

			if (is_pack_valid(pack.p)) {
				ALLOC_GROW(packs, packs_nr + 1, packs_alloc);
				memcpy(&packs[packs_nr++], &pack, sizeof(pack));
			}

			objects_nr += pack.p->num_objects;
		}

		QSORT(packs, packs_nr, bitmapped_pack_cmp);
	} else {
		struct packed_git *pack;
		uint32_t pack_int_id;

		if (bitmap_is_midx(bitmap_git)) {
			struct multi_pack_index *m = bitmap_git->midx;
			uint32_t preferred_pack_pos;

			while (m->base_midx)
				m = m->base_midx;

			if (midx_preferred_pack(m, &preferred_pack_pos) < 0) {
				warning(_("unable to compute preferred pack, disabling pack-reuse"));
				return;
			}

			pack = nth_midxed_pack(m, preferred_pack_pos);
			pack_int_id = preferred_pack_pos;
		} else {
			pack = bitmap_git->pack;
			/*
			 * Any value for 'pack_int_id' will do here. When we
			 * process the pack via try_partial_reuse(), we won't
			 * use the `pack_int_id` field since we have a non-MIDX
			 * bitmap.
			 *
			 * Use '-1' as a sentinel value to make it clear
			 * that we do not expect to read this field.
			 */
			pack_int_id = -1;
		}

		if (is_pack_valid(pack)) {
			ALLOC_GROW(packs, packs_nr + 1, packs_alloc);
			packs[packs_nr].p = pack;
			packs[packs_nr].pack_int_id = pack_int_id;
			packs[packs_nr].bitmap_nr = pack->num_objects;
			packs[packs_nr].bitmap_pos = 0;
			packs[packs_nr].from_midx = bitmap_git->midx;
			packs_nr++;
		}

		objects_nr = pack->num_objects;
	}

	if (!packs_nr)
		return;

	word_alloc = objects_nr / BITS_IN_EWORD;
	if (objects_nr % BITS_IN_EWORD)
		word_alloc++;
	reuse = bitmap_word_alloc(word_alloc);

	for (i = 0; i < packs_nr; i++)
		reuse_partial_packfile_from_bitmap_1(bitmap_git, &packs[i], reuse);

	if (bitmap_is_empty(reuse)) {
		free(packs);
		bitmap_free(reuse);
		return;
	}

	/*
	 * Drop any reused objects from the result, since they will not
	 * need to be handled separately.
	 */
	bitmap_and_not(result, reuse);
	*packs_out = packs;
	*packs_nr_out = packs_nr;
	*reuse_out = reuse;
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

	show_objects_for_type(bitmap_git, bitmap_git->result,
			      OBJ_COMMIT, show_reachable, NULL);
	if (revs->tree_objects)
		show_objects_for_type(bitmap_git, bitmap_git->result,
				      OBJ_TREE, show_reachable, NULL);
	if (revs->blob_objects)
		show_objects_for_type(bitmap_git, bitmap_git->result,
				      OBJ_BLOB, show_reachable, NULL);
	if (revs->tag_objects)
		show_objects_for_type(bitmap_git, bitmap_git->result,
				      OBJ_TAG, show_reachable, NULL);

	show_extended_objects(bitmap_git, revs, show_reachable);
}

static uint32_t count_object_type(struct bitmap_index *bitmap_git,
				  enum object_type type)
{
	struct bitmap *objects = bitmap_git->result;
	struct eindex *eindex = &bitmap_git->ext_index;

	uint32_t i = 0, count = 0;
	struct ewah_or_iterator it;
	eword_t filter;

	init_type_iterator(&it, bitmap_git, type);

	while (i < objects->word_alloc && ewah_or_iterator_next(&filter, &it)) {
		eword_t word = objects->words[i++] & filter;
		count += ewah_bit_popcount64(word);
	}

	for (i = 0; i < eindex->count; ++i) {
		if (eindex->objects[i]->type == type &&
		    bitmap_get(objects,
			       st_add(bitmap_num_objects_total(bitmap_git), i)))
			count++;
	}

	ewah_or_iterator_release(&it);

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

	struct bitmap_test_data *base_tdata;
};

static void test_bitmap_type(struct bitmap_test_data *tdata,
			     struct object *obj, int pos)
{
	enum object_type bitmap_type = OBJ_NONE;
	int bitmaps_nr = 0;

	if (bitmap_is_midx(tdata->bitmap_git)) {
		while (pos < tdata->bitmap_git->midx->num_objects_in_base)
			tdata = tdata->base_tdata;
	}

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

static void test_show_object(struct object *object,
			     const char *name UNUSED,
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

static uint32_t bitmap_total_entry_count(struct bitmap_index *bitmap_git)
{
	uint32_t total = 0;
	do {
		total = st_add(total, bitmap_git->entry_count);
		bitmap_git = bitmap_git->base;
	} while (bitmap_git);

	return total;
}

static void bitmap_test_data_prepare(struct bitmap_test_data *tdata,
				     struct bitmap_index *bitmap_git)
{
	memset(tdata, 0, sizeof(struct bitmap_test_data));

	tdata->bitmap_git = bitmap_git;
	tdata->base = bitmap_new();
	tdata->commits = ewah_to_bitmap(bitmap_git->commits);
	tdata->trees = ewah_to_bitmap(bitmap_git->trees);
	tdata->blobs = ewah_to_bitmap(bitmap_git->blobs);
	tdata->tags = ewah_to_bitmap(bitmap_git->tags);

	if (bitmap_git->base) {
		tdata->base_tdata = xmalloc(sizeof(struct bitmap_test_data));
		bitmap_test_data_prepare(tdata->base_tdata, bitmap_git->base);
	}
}

static void bitmap_test_data_release(struct bitmap_test_data *tdata)
{
	if (!tdata)
		return;

	bitmap_test_data_release(tdata->base_tdata);
	free(tdata->base_tdata);

	bitmap_free(tdata->base);
	bitmap_free(tdata->commits);
	bitmap_free(tdata->trees);
	bitmap_free(tdata->blobs);
	bitmap_free(tdata->tags);
}

void test_bitmap_walk(struct rev_info *revs)
{
	struct object *root;
	struct bitmap *result = NULL;
	size_t result_popcnt;
	struct bitmap_test_data tdata;
	struct bitmap_index *bitmap_git, *found;
	struct ewah_bitmap *bm;

	if (!(bitmap_git = prepare_bitmap_git(revs->repo)))
		die(_("failed to load bitmap indexes"));

	if (revs->pending.nr != 1)
		die(_("you must specify exactly one commit to test"));

	fprintf_ln(stderr, "Bitmap v%d test (%d entries%s, %d total)",
		bitmap_git->version,
		bitmap_git->entry_count,
		bitmap_git->table_lookup ? "" : " loaded",
		bitmap_total_entry_count(bitmap_git));

	root = revs->pending.objects[0].item;
	bm = find_bitmap_for_commit(bitmap_git, (struct commit *)root, &found);

	if (bm) {
		fprintf_ln(stderr, "Found bitmap for '%s'. %d bits / %08x checksum",
			oid_to_hex(&root->oid),
			(int)bm->bit_size, ewah_checksum(bm));

		if (bitmap_is_midx(found))
			fprintf_ln(stderr, "Located via MIDX '%s'.",
				   get_midx_checksum(found->midx));
		else
			fprintf_ln(stderr, "Located via pack '%s'.",
				   hash_to_hex_algop(found->pack->hash,
						     revs->repo->hash_algo));

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

	bitmap_test_data_prepare(&tdata, bitmap_git);
	tdata.prg = start_progress(revs->repo,
				   "Verifying bitmap entries",
				   result_popcnt);

	traverse_commit_list(revs, &test_show_commit, &test_show_object, &tdata);

	stop_progress(&tdata.prg);

	if (bitmap_equals(result, tdata.base))
		fprintf_ln(stderr, "OK!");
	else
		die(_("mismatch in bitmap results"));

	bitmap_free(result);
	bitmap_test_data_release(&tdata);
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
	 * Since this function needs to print the bitmapped
	 * commits, bypass the commit lookup table (if one exists)
	 * by forcing the bitmap to eagerly load its entries.
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

int test_bitmap_commits_with_offset(struct repository *r)
{
	struct object_id oid;
	struct stored_bitmap *stored;
	struct bitmap_index *bitmap_git;
	size_t commit_idx_pos_map_pos, xor_offset_map_pos, flag_map_pos,
		ewah_bitmap_map_pos;

	bitmap_git = prepare_bitmap_git(r);
	if (!bitmap_git)
		die(_("failed to load bitmap indexes"));

	/*
	 * Since this function needs to know the position of each individual
	 * bitmap, bypass the commit lookup table (if one exists) by forcing
	 * the bitmap to eagerly load its entries.
	 */
	if (bitmap_git->table_lookup) {
		if (load_bitmap_entries_v1(bitmap_git) < 0)
			die(_("failed to load bitmap indexes"));
	}

	kh_foreach (bitmap_git->bitmaps, oid, stored, {
		commit_idx_pos_map_pos = stored->map_pos;
		xor_offset_map_pos = stored->map_pos + sizeof(uint32_t);
		flag_map_pos = xor_offset_map_pos + sizeof(uint8_t);
		ewah_bitmap_map_pos = flag_map_pos + sizeof(uint8_t);

		printf_ln("%s %"PRIuMAX" %"PRIuMAX" %"PRIuMAX" %"PRIuMAX,
			  oid_to_hex(&oid),
			  (uintmax_t)commit_idx_pos_map_pos,
			  (uintmax_t)xor_offset_map_pos,
			  (uintmax_t)flag_map_pos,
			  (uintmax_t)ewah_bitmap_map_pos);
	})
		;

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

static void bit_pos_to_object_id(struct bitmap_index *bitmap_git,
				 uint32_t bit_pos,
				 struct object_id *oid)
{
	uint32_t index_pos;

	if (bitmap_is_midx(bitmap_git))
		index_pos = pack_pos_to_midx(bitmap_git->midx, bit_pos);
	else
		index_pos = pack_pos_to_index(bitmap_git->pack, bit_pos);

	nth_bitmap_object_oid(bitmap_git, oid, index_pos);
}

int test_bitmap_pseudo_merges(struct repository *r)
{
	struct bitmap_index *bitmap_git;
	uint32_t i;

	bitmap_git = prepare_bitmap_git(r);
	if (!bitmap_git || !bitmap_git->pseudo_merges.nr)
		goto cleanup;

	for (i = 0; i < bitmap_git->pseudo_merges.nr; i++) {
		struct pseudo_merge *merge;
		struct ewah_bitmap *commits_bitmap, *merge_bitmap;

		merge = use_pseudo_merge(&bitmap_git->pseudo_merges,
					 &bitmap_git->pseudo_merges.v[i]);
		commits_bitmap = merge->commits;
		merge_bitmap = pseudo_merge_bitmap(&bitmap_git->pseudo_merges,
						   merge);

		printf("at=%"PRIuMAX", commits=%"PRIuMAX", objects=%"PRIuMAX"\n",
		       (uintmax_t)merge->at,
		       (uintmax_t)ewah_bitmap_popcount(commits_bitmap),
		       (uintmax_t)ewah_bitmap_popcount(merge_bitmap));
	}

cleanup:
	free_bitmap_index(bitmap_git);
	return 0;
}

static void dump_ewah_object_ids(struct bitmap_index *bitmap_git,
				 struct ewah_bitmap *bitmap)

{
	struct ewah_iterator it;
	eword_t word;
	uint32_t pos = 0;

	ewah_iterator_init(&it, bitmap);

	while (ewah_iterator_next(&word, &it)) {
		struct object_id oid;
		uint32_t offset;

		for (offset = 0; offset < BITS_IN_EWORD; offset++) {
			if (!(word >> offset))
				break;

			offset += ewah_bit_ctz64(word >> offset);

			bit_pos_to_object_id(bitmap_git, pos + offset, &oid);
			printf("%s\n", oid_to_hex(&oid));
		}
		pos += BITS_IN_EWORD;
	}
}

int test_bitmap_pseudo_merge_commits(struct repository *r, uint32_t n)
{
	struct bitmap_index *bitmap_git;
	struct pseudo_merge *merge;
	int ret = 0;

	bitmap_git = prepare_bitmap_git(r);
	if (!bitmap_git || !bitmap_git->pseudo_merges.nr)
		goto cleanup;

	if (n >= bitmap_git->pseudo_merges.nr) {
		ret = error(_("pseudo-merge index out of range "
			      "(%"PRIu32" >= %"PRIuMAX")"),
			    n, (uintmax_t)bitmap_git->pseudo_merges.nr);
		goto cleanup;
	}

	merge = use_pseudo_merge(&bitmap_git->pseudo_merges,
				 &bitmap_git->pseudo_merges.v[n]);
	dump_ewah_object_ids(bitmap_git, merge->commits);

cleanup:
	free_bitmap_index(bitmap_git);
	return ret;
}

int test_bitmap_pseudo_merge_objects(struct repository *r, uint32_t n)
{
	struct bitmap_index *bitmap_git;
	struct pseudo_merge *merge;
	int ret = 0;

	bitmap_git = prepare_bitmap_git(r);
	if (!bitmap_git || !bitmap_git->pseudo_merges.nr)
		goto cleanup;

	if (n >= bitmap_git->pseudo_merges.nr) {
		ret = error(_("pseudo-merge index out of range "
			      "(%"PRIu32" >= %"PRIuMAX")"),
			    n, (uintmax_t)bitmap_git->pseudo_merges.nr);
		goto cleanup;
	}

	merge = use_pseudo_merge(&bitmap_git->pseudo_merges,
				 &bitmap_git->pseudo_merges.v[n]);

	dump_ewah_object_ids(bitmap_git,
			     pseudo_merge_bitmap(&bitmap_git->pseudo_merges,
						 merge));

cleanup:
	free_bitmap_index(bitmap_git);
	return ret;
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
	struct repository *r = bitmap_repo(bitmap_git);
	uint32_t i, num_objects;
	uint32_t *reposition;

	if (!bitmap_is_midx(bitmap_git))
		load_reverse_index(r, bitmap_git);
	else if (load_midx_revindex(bitmap_git->midx))
		BUG("rebuild_existing_bitmaps: missing required rev-cache "
		    "extension");

	num_objects = bitmap_num_objects_total(bitmap_git);
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
			if (!oe->hash)
				oe->hash = bitmap_name_hash(bitmap_git, index_pos);
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
	free(b->commits_all);
	free(b->trees_all);
	free(b->blobs_all);
	free(b->tags_all);
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
	free_pseudo_merge_map(&b->pseudo_merges);
	free_bitmap_index(b->base);
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
	struct ewah_or_iterator it;
	eword_t filter;
	size_t i;

	init_type_iterator(&it, bitmap_git, object_type);
	for (i = 0; i < result->word_alloc &&
			ewah_or_iterator_next(&filter, &it); i++) {
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
				struct packed_git *pack = nth_midxed_pack(bitmap_git->midx, pack_id);

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

	ewah_or_iterator_release(&it);

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

		if (!bitmap_get(result,
				st_add(bitmap_num_objects_total(bitmap_git),
				       i)))
			continue;

		if (odb_read_object_info_extended(bitmap_repo(bitmap_git)->objects,
						  &obj->oid, &oi, 0) < 0)
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

static int verify_bitmap_file(const struct git_hash_algo *algop,
			      const char *name)
{
	struct stat st;
	unsigned char *data;
	int fd = git_open(name);
	int res = 0;

	/* It is OK to not have the file. */
	if (fd < 0 || fstat(fd, &st)) {
		if (fd >= 0)
			close(fd);
		return 0;
	}

	data = xmmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (!hashfile_checksum_valid(algop, data, st.st_size))
		res = error(_("bitmap file '%s' has invalid checksum"),
			    name);

	munmap(data, st.st_size);
	return res;
}

int verify_bitmap_files(struct repository *r)
{
	struct odb_source *source;
	struct packed_git *p;
	int res = 0;

	odb_prepare_alternates(r->objects);
	for (source = r->objects->sources; source; source = source->next) {
		struct multi_pack_index *m = get_multi_pack_index(source);
		char *midx_bitmap_name;

		if (!m)
			continue;

		midx_bitmap_name = midx_bitmap_filename(m);
		res |= verify_bitmap_file(r->hash_algo, midx_bitmap_name);
		free(midx_bitmap_name);
	}

	repo_for_each_pack(r, p) {
		char *pack_bitmap_name = pack_bitmap_filename(p);
		res |= verify_bitmap_file(r->hash_algo, pack_bitmap_name);
		free(pack_bitmap_name);
	}

	return res;
}
