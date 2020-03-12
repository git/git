#include "cache.h"
#include "commit.h"
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
	/* Packfile to which this bitmap index belongs to */
	struct packed_git *pack;

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

	if (st->xor == NULL)
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
		error("Failed to load bitmap index (corrupted?)");
		ewah_pool_free(b);
		return NULL;
	}

	index->map_pos += bitmap_size;
	return b;
}

static int load_bitmap_header(struct bitmap_index *index)
{
	struct bitmap_disk_header *header = (void *)index->map;

	if (index->map_size < sizeof(*header) + the_hash_algo->rawsz)
		return error("Corrupted bitmap index (missing header data)");

	if (memcmp(header->magic, BITMAP_IDX_SIGNATURE, sizeof(BITMAP_IDX_SIGNATURE)) != 0)
		return error("Corrupted bitmap index file (wrong header)");

	index->version = ntohs(header->version);
	if (index->version != 1)
		return error("Unsupported version for bitmap index file (%d)", index->version);

	/* Parse known bitmap format options */
	{
		uint32_t flags = ntohs(header->options);

		if ((flags & BITMAP_OPT_FULL_DAG) == 0)
			return error("Unsupported options for bitmap index file "
				"(Git requires BITMAP_OPT_FULL_DAG)");

		if (flags & BITMAP_OPT_HASH_CACHE) {
			unsigned char *end = index->map + index->map_size - the_hash_algo->rawsz;
			index->hashes = ((uint32_t *)end) - index->pack->num_objects;
		}
	}

	index->entry_count = ntohl(header->entry_count);
	index->map_pos += sizeof(*header) - GIT_MAX_RAWSZ + the_hash_algo->rawsz;
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

	/* a 0 return code means the insertion succeeded with no changes,
	 * because the SHA1 already existed on the map. this is bad, there
	 * shouldn't be duplicated commits in the index */
	if (ret == 0) {
		error("Duplicate entry in bitmap index: %s", oid_to_hex(oid));
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

		commit_idx_pos = read_be32(index->map, &index->map_pos);
		xor_offset = read_u8(index->map, &index->map_pos);
		flags = read_u8(index->map, &index->map_pos);

		nth_packed_object_id(&oid, index->pack, commit_idx_pos);

		bitmap = read_bitmap_1(index);
		if (!bitmap)
			return -1;

		if (xor_offset > MAX_XOR_OFFSET || xor_offset > i)
			return error("Corrupted bitmap pack index");

		if (xor_offset > 0) {
			xor_bitmap = recent_bitmaps[(i - xor_offset) % MAX_XOR_OFFSET];

			if (xor_bitmap == NULL)
				return error("Invalid XOR offset in bitmap pack index");
		}

		recent_bitmaps[i % MAX_XOR_OFFSET] = store_bitmap(
			index, bitmap, &oid, xor_bitmap, flags);
	}

	return 0;
}

static char *pack_bitmap_filename(struct packed_git *p)
{
	size_t len;

	if (!strip_suffix(p->pack_name, ".pack", &len))
		BUG("pack_name does not end in .pack");
	return xstrfmt("%.*s.bitmap", (int)len, p->pack_name);
}

static int open_pack_bitmap_1(struct bitmap_index *bitmap_git, struct packed_git *packfile)
{
	int fd;
	struct stat st;
	char *idx_name;

	if (open_pack_index(packfile))
		return -1;

	idx_name = pack_bitmap_filename(packfile);
	fd = git_open(idx_name);
	free(idx_name);

	if (fd < 0)
		return -1;

	if (fstat(fd, &st)) {
		close(fd);
		return -1;
	}

	if (bitmap_git->pack) {
		warning("ignoring extra bitmap file: %s", packfile->pack_name);
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
		return -1;
	}

	return 0;
}

static int load_pack_bitmap(struct bitmap_index *bitmap_git)
{
	assert(bitmap_git->map);

	bitmap_git->bitmaps = kh_init_oid_map();
	bitmap_git->ext_index.positions = kh_init_oid_pos();
	if (load_pack_revindex(bitmap_git->pack))
		goto failed;

	if (!(bitmap_git->commits = read_bitmap_1(bitmap_git)) ||
		!(bitmap_git->trees = read_bitmap_1(bitmap_git)) ||
		!(bitmap_git->blobs = read_bitmap_1(bitmap_git)) ||
		!(bitmap_git->tags = read_bitmap_1(bitmap_git)))
		goto failed;

	if (load_bitmap_entries_v1(bitmap_git) < 0)
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

	assert(!bitmap_git->map);

	for (p = get_all_packs(r); p; p = p->next) {
		if (open_pack_bitmap_1(bitmap_git, p) == 0)
			ret = 0;
	}

	return ret;
}

struct bitmap_index *prepare_bitmap_git(struct repository *r)
{
	struct bitmap_index *bitmap_git = xcalloc(1, sizeof(*bitmap_git));

	if (!open_pack_bitmap(r, bitmap_git) && !load_pack_bitmap(bitmap_git))
		return bitmap_git;

	free_bitmap_index(bitmap_git);
	return NULL;
}

struct include_data {
	struct bitmap_index *bitmap_git;
	struct bitmap *base;
	struct bitmap *seen;
};

static inline int bitmap_position_extended(struct bitmap_index *bitmap_git,
					   const struct object_id *oid)
{
	kh_oid_pos_t *positions = bitmap_git->ext_index.positions;
	khiter_t pos = kh_get_oid_pos(positions, *oid);

	if (pos < kh_end(positions)) {
		int bitmap_pos = kh_value(positions, pos);
		return bitmap_pos + bitmap_git->pack->num_objects;
	}

	return -1;
}

static inline int bitmap_position_packfile(struct bitmap_index *bitmap_git,
					   const struct object_id *oid)
{
	off_t offset = find_pack_entry_one(oid->hash, bitmap_git->pack);
	if (!offset)
		return -1;

	return find_revindex_position(bitmap_git->pack, offset);
}

static int bitmap_position(struct bitmap_index *bitmap_git,
			   const struct object_id *oid)
{
	int pos = bitmap_position_packfile(bitmap_git, oid);
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

	return bitmap_pos + bitmap_git->pack->num_objects;
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
			      const struct object_id *oid,
			      int bitmap_pos)
{
	khiter_t hash_pos;

	if (data->seen && bitmap_get(data->seen, bitmap_pos))
		return 0;

	if (bitmap_get(data->base, bitmap_pos))
		return 0;

	hash_pos = kh_get_oid_map(bitmap_git->bitmaps, *oid);
	if (hash_pos < kh_end(bitmap_git->bitmaps)) {
		struct stored_bitmap *st = kh_value(bitmap_git->bitmaps, hash_pos);
		bitmap_or_ewah(data->base, lookup_stored_bitmap(st));
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

	if (!add_to_include_set(data->bitmap_git, data, &commit->object.oid,
				bitmap_pos)) {
		struct commit_list *parent = commit->parents;

		while (parent) {
			parent->item->object.flags |= SEEN;
			parent = parent->next;
		}

		return 0;
	}

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

		if (object->type == OBJ_COMMIT) {
			khiter_t pos = kh_get_oid_map(bitmap_git->bitmaps, object->oid);

			if (pos < kh_end(bitmap_git->bitmaps)) {
				struct stored_bitmap *st = kh_value(bitmap_git->bitmaps, pos);
				struct ewah_bitmap *or_with = lookup_stored_bitmap(st);

				if (base == NULL)
					base = ewah_to_bitmap(or_with);
				else
					bitmap_or_ewah(base, or_with);

				object->flags |= SEEN;
				continue;
			}
		}

		object_list_insert(object, &not_mapped);
	}

	/*
	 * Best case scenario: We found bitmaps for all the roots,
	 * so the resulting `or` bitmap has the full reachability analysis
	 */
	if (not_mapped == NULL)
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

		if (base == NULL)
			base = bitmap_new();

		incdata.bitmap_git = bitmap_git;
		incdata.base = base;
		incdata.seen = seen;

		revs->include_check = should_include;
		revs->include_check_data = &incdata;

		if (prepare_revision_walk(revs))
			die("revision walk setup failed");

		show_data.bitmap_git = bitmap_git;
		show_data.base = base;

		traverse_commit_list(revs, show_commit, show_object,
				     &show_data);
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

		if (!bitmap_get(objects, bitmap_git->pack->num_objects + i))
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
			struct object_id oid;
			struct revindex_entry *entry;
			uint32_t hash = 0;

			if ((word >> offset) == 0)
				break;

			offset += ewah_bit_ctz64(word >> offset);

			entry = &bitmap_git->pack->revindex[pos + offset];
			nth_packed_object_id(&oid, bitmap_git->pack, entry->nr);

			if (bitmap_git->hashes)
				hash = get_be32(bitmap_git->hashes + entry->nr);

			show_reach(&oid, object_type, 0, hash, bitmap_git->pack, entry->offset);
		}
	}
}

static int in_bitmapped_pack(struct bitmap_index *bitmap_git,
			     struct object_list *roots)
{
	while (roots) {
		struct object *object = roots->item;
		roots = roots->next;

		if (find_pack_entry_one(object->oid.hash, bitmap_git->pack) > 0)
			return 1;
	}

	return 0;
}

static struct bitmap *find_tip_blobs(struct bitmap_index *bitmap_git,
				     struct object_list *tip_objects)
{
	struct bitmap *result = bitmap_new();
	struct object_list *p;

	for (p = tip_objects; p; p = p->next) {
		int pos;

		if (p->item->type != OBJ_BLOB)
			continue;

		pos = bitmap_position(bitmap_git, &p->item->oid);
		if (pos < 0)
			continue;

		bitmap_set(result, pos);
	}

	return result;
}

static void filter_bitmap_blob_none(struct bitmap_index *bitmap_git,
				    struct object_list *tip_objects,
				    struct bitmap *to_filter)
{
	struct eindex *eindex = &bitmap_git->ext_index;
	struct bitmap *tips;
	struct ewah_iterator it;
	eword_t mask;
	uint32_t i;

	/*
	 * The non-bitmap version of this filter never removes
	 * blobs which the other side specifically asked for,
	 * so we must match that behavior.
	 */
	tips = find_tip_blobs(bitmap_git, tip_objects);

	/*
	 * We can use the blob type-bitmap to work in whole words
	 * for the objects that are actually in the bitmapped packfile.
	 */
	for (i = 0, init_type_iterator(&it, bitmap_git, OBJ_BLOB);
	     i < to_filter->word_alloc && ewah_iterator_next(&mask, &it);
	     i++) {
		if (i < tips->word_alloc)
			mask &= ~tips->words[i];
		to_filter->words[i] &= ~mask;
	}

	/*
	 * Clear any blobs that weren't in the packfile (and so would not have
	 * been caught by the loop above. We'll have to check them
	 * individually.
	 */
	for (i = 0; i < eindex->count; i++) {
		uint32_t pos = i + bitmap_git->pack->num_objects;
		if (eindex->objects[i]->type == OBJ_BLOB &&
		    bitmap_get(to_filter, pos) &&
		    !bitmap_get(tips, pos))
			bitmap_unset(to_filter, pos);
	}

	bitmap_free(tips);
}

static unsigned long get_size_by_pos(struct bitmap_index *bitmap_git,
				     uint32_t pos)
{
	struct packed_git *pack = bitmap_git->pack;
	unsigned long size;
	struct object_info oi = OBJECT_INFO_INIT;

	oi.sizep = &size;

	if (pos < pack->num_objects) {
		struct revindex_entry *entry = &pack->revindex[pos];
		if (packed_object_info(the_repository, pack,
				       entry->offset, &oi) < 0) {
			struct object_id oid;
			nth_packed_object_id(&oid, pack, entry->nr);
			die(_("unable to get size of %s"), oid_to_hex(&oid));
		}
	} else {
		struct eindex *eindex = &bitmap_git->ext_index;
		struct object *obj = eindex->objects[pos - pack->num_objects];
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

	tips = find_tip_blobs(bitmap_git, tip_objects);

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
		uint32_t pos = i + bitmap_git->pack->num_objects;
		if (eindex->objects[i]->type == OBJ_BLOB &&
		    bitmap_get(to_filter, pos) &&
		    !bitmap_get(tips, pos) &&
		    get_size_by_pos(bitmap_git, pos) >= limit)
			bitmap_unset(to_filter, pos);
	}

	bitmap_free(tips);
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

	/* filter choice not handled */
	return -1;
}

static int can_filter_bitmap(struct list_objects_filter_options *filter)
{
	return !filter_bitmap(NULL, NULL, NULL, filter);
}

struct bitmap_index *prepare_bitmap_walk(struct rev_info *revs,
					 struct list_objects_filter_options *filter)
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

	if (!can_filter_bitmap(filter))
		return NULL;

	/* try to open a bitmapped pack, but don't parse it yet
	 * because we may not need to use it */
	bitmap_git = xcalloc(1, sizeof(*bitmap_git));
	if (open_pack_bitmap(revs->repo, bitmap_git) < 0)
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
	if (load_pack_bitmap(bitmap_git) < 0)
		goto cleanup;

	object_array_clear(&revs->pending);

	if (haves) {
		revs->ignore_missing_links = 1;
		haves_bitmap = find_objects(bitmap_git, revs, haves, NULL);
		reset_revision_walk();
		revs->ignore_missing_links = 0;

		if (haves_bitmap == NULL)
			BUG("failed to perform bitmap walk");
	}

	wants_bitmap = find_objects(bitmap_git, revs, wants, haves_bitmap);

	if (!wants_bitmap)
		BUG("failed to perform bitmap walk");

	if (haves_bitmap)
		bitmap_and_not(wants_bitmap, haves_bitmap);

	filter_bitmap(bitmap_git, wants, wants_bitmap, filter);

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

static void try_partial_reuse(struct bitmap_index *bitmap_git,
			      size_t pos,
			      struct bitmap *reuse,
			      struct pack_window **w_curs)
{
	struct revindex_entry *revidx;
	off_t offset;
	enum object_type type;
	unsigned long size;

	if (pos >= bitmap_git->pack->num_objects)
		return; /* not actually in the pack */

	revidx = &bitmap_git->pack->revindex[pos];
	offset = revidx->offset;
	type = unpack_object_header(bitmap_git->pack, w_curs, &offset, &size);
	if (type < 0)
		return; /* broken packfile, punt */

	if (type == OBJ_REF_DELTA || type == OBJ_OFS_DELTA) {
		off_t base_offset;
		int base_pos;

		/*
		 * Find the position of the base object so we can look it up
		 * in our bitmaps. If we can't come up with an offset, or if
		 * that offset is not in the revidx, the pack is corrupt.
		 * There's nothing we can do, so just punt on this object,
		 * and the normal slow path will complain about it in
		 * more detail.
		 */
		base_offset = get_delta_base(bitmap_git->pack, w_curs,
					     &offset, type, revidx->offset);
		if (!base_offset)
			return;
		base_pos = find_revindex_position(bitmap_git->pack, base_offset);
		if (base_pos < 0)
			return;

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
			return;

		/*
		 * And finally, if we're not sending the base as part of our
		 * reuse chunk, then don't send this object either. The base
		 * would come after us, along with other objects not
		 * necessarily in the pack, which means we'd need to convert
		 * to REF_DELTA on the fly. Better to just let the normal
		 * object_entry code path handle it.
		 */
		if (!bitmap_get(reuse, base_pos))
			return;
	}

	/*
	 * If we got here, then the object is OK to reuse. Mark it.
	 */
	bitmap_set(reuse, pos);
}

int reuse_partial_packfile_from_bitmap(struct bitmap_index *bitmap_git,
				       struct packed_git **packfile_out,
				       uint32_t *entries,
				       struct bitmap **reuse_out)
{
	struct bitmap *result = bitmap_git->result;
	struct bitmap *reuse;
	struct pack_window *w_curs = NULL;
	size_t i = 0;
	uint32_t offset;

	assert(result);

	while (i < result->word_alloc && result->words[i] == (eword_t)~0)
		i++;

	/* Don't mark objects not in the packfile */
	if (i > bitmap_git->pack->num_objects / BITS_IN_EWORD)
		i = bitmap_git->pack->num_objects / BITS_IN_EWORD;

	reuse = bitmap_word_alloc(i);
	memset(reuse->words, 0xFF, i * sizeof(eword_t));

	for (; i < result->word_alloc; ++i) {
		eword_t word = result->words[i];
		size_t pos = (i * BITS_IN_EWORD);

		for (offset = 0; offset < BITS_IN_EWORD; ++offset) {
			if ((word >> offset) == 0)
				break;

			offset += ewah_bit_ctz64(word >> offset);
			try_partial_reuse(bitmap_git, pos + offset, reuse, &w_curs);
		}
	}

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
	*packfile_out = bitmap_git->pack;
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
			bitmap_get(objects, bitmap_git->pack->num_objects + i))
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
	struct progress *prg;
	size_t seen;
};

static void test_show_object(struct object *object, const char *name,
			     void *data)
{
	struct bitmap_test_data *tdata = data;
	int bitmap_pos;

	bitmap_pos = bitmap_position(tdata->bitmap_git, &object->oid);
	if (bitmap_pos < 0)
		die("Object not in bitmap: %s\n", oid_to_hex(&object->oid));

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
		die("Object not in bitmap: %s\n", oid_to_hex(&commit->object.oid));

	bitmap_set(tdata->base, bitmap_pos);
	display_progress(tdata->prg, ++tdata->seen);
}

void test_bitmap_walk(struct rev_info *revs)
{
	struct object *root;
	struct bitmap *result = NULL;
	khiter_t pos;
	size_t result_popcnt;
	struct bitmap_test_data tdata;
	struct bitmap_index *bitmap_git;

	if (!(bitmap_git = prepare_bitmap_git(revs->repo)))
		die("failed to load bitmap indexes");

	if (revs->pending.nr != 1)
		die("you must specify exactly one commit to test");

	fprintf(stderr, "Bitmap v%d test (%d entries loaded)\n",
		bitmap_git->version, bitmap_git->entry_count);

	root = revs->pending.objects[0].item;
	pos = kh_get_oid_map(bitmap_git->bitmaps, root->oid);

	if (pos < kh_end(bitmap_git->bitmaps)) {
		struct stored_bitmap *st = kh_value(bitmap_git->bitmaps, pos);
		struct ewah_bitmap *bm = lookup_stored_bitmap(st);

		fprintf(stderr, "Found bitmap for %s. %d bits / %08x checksum\n",
			oid_to_hex(&root->oid), (int)bm->bit_size, ewah_checksum(bm));

		result = ewah_to_bitmap(bm);
	}

	if (result == NULL)
		die("Commit %s doesn't have an indexed bitmap", oid_to_hex(&root->oid));

	revs->tag_objects = 1;
	revs->tree_objects = 1;
	revs->blob_objects = 1;

	result_popcnt = bitmap_popcount(result);

	if (prepare_revision_walk(revs))
		die("revision walk setup failed");

	tdata.bitmap_git = bitmap_git;
	tdata.base = bitmap_new();
	tdata.prg = start_progress("Verifying bitmap entries", result_popcnt);
	tdata.seen = 0;

	traverse_commit_list(revs, &test_show_commit, &test_show_object, &tdata);

	stop_progress(&tdata.prg);

	if (bitmap_equals(result, tdata.base))
		fprintf(stderr, "OK!\n");
	else
		fprintf(stderr, "Mismatch!\n");

	free_bitmap_index(bitmap_git);
}

static int rebuild_bitmap(uint32_t *reposition,
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

int rebuild_existing_bitmaps(struct bitmap_index *bitmap_git,
			     struct packing_data *mapping,
			     kh_oid_map_t *reused_bitmaps,
			     int show_progress)
{
	uint32_t i, num_objects;
	uint32_t *reposition;
	struct bitmap *rebuild;
	struct stored_bitmap *stored;
	struct progress *progress = NULL;

	khiter_t hash_pos;
	int hash_ret;

	num_objects = bitmap_git->pack->num_objects;
	reposition = xcalloc(num_objects, sizeof(uint32_t));

	for (i = 0; i < num_objects; ++i) {
		struct object_id oid;
		struct revindex_entry *entry;
		struct object_entry *oe;

		entry = &bitmap_git->pack->revindex[i];
		nth_packed_object_id(&oid, bitmap_git->pack, entry->nr);
		oe = packlist_find(mapping, &oid);

		if (oe)
			reposition[i] = oe_in_pack_pos(mapping, oe) + 1;
	}

	rebuild = bitmap_new();
	i = 0;

	if (show_progress)
		progress = start_progress("Reusing bitmaps", 0);

	kh_foreach_value(bitmap_git->bitmaps, stored, {
		if (stored->flags & BITMAP_FLAG_REUSE) {
			if (!rebuild_bitmap(reposition,
					    lookup_stored_bitmap(stored),
					    rebuild)) {
				hash_pos = kh_put_oid_map(reused_bitmaps,
							  stored->oid,
							  &hash_ret);
				kh_value(reused_bitmaps, hash_pos) =
					bitmap_to_ewah(rebuild);
			}
			bitmap_reset(rebuild);
			display_progress(progress, ++i);
		}
	});

	stop_progress(&progress);

	free(reposition);
	bitmap_free(rebuild);
	return 0;
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
	kh_destroy_oid_map(b->bitmaps);
	free(b->ext_index.objects);
	free(b->ext_index.hashes);
	bitmap_free(b->result);
	bitmap_free(b->haves);
	free(b);
}

int bitmap_has_oid_in_uninteresting(struct bitmap_index *bitmap_git,
				    const struct object_id *oid)
{
	return bitmap_git &&
		bitmap_walk_contains(bitmap_git, bitmap_git->haves, oid);
}
