#include "cache.h"
#include "object-store.h"
#include "cummit.h"
#include "tag.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "progress.h"
#include "pack-revindex.h"
#include "pack.h"
#include "pack-bitmap.h"
#include "hash-lookup.h"
#include "pack-objects.h"
#include "cummit-reach.h"
#include "prio-queue.h"

struct bitmapped_cummit {
	struct cummit *cummit;
	struct ewah_bitmap *bitmap;
	struct ewah_bitmap *write_as;
	int flags;
	int xor_offset;
	uint32_t cummit_pos;
};

struct bitmap_writer {
	struct ewah_bitmap *cummits;
	struct ewah_bitmap *trees;
	struct ewah_bitmap *blobs;
	struct ewah_bitmap *tags;

	kh_oid_map_t *bitmaps;
	struct packing_data *to_pack;

	struct bitmapped_cummit *selected;
	unsigned int selected_nr, selected_alloc;

	struct progress *progress;
	int show_progress;
	unsigned char pack_checksum[GIT_MAX_RAWSZ];
};

static struct bitmap_writer writer;

void bitmap_writer_show_progress(int show)
{
	writer.show_progress = show;
}

/**
 * Build the initial type index for the packfile or multi-pack-index
 */
void bitmap_writer_build_type_index(struct packing_data *to_pack,
				    struct pack_idx_entry **index,
				    uint32_t index_nr)
{
	uint32_t i;

	writer.cummits = ewah_new();
	writer.trees = ewah_new();
	writer.blobs = ewah_new();
	writer.tags = ewah_new();
	ALLOC_ARRAY(to_pack->in_pack_pos, to_pack->nr_objects);

	for (i = 0; i < index_nr; ++i) {
		struct object_entry *entry = (struct object_entry *)index[i];
		enum object_type real_type;

		oe_set_in_pack_pos(to_pack, entry, i);

		switch (oe_type(entry)) {
		case OBJ_cummit:
		case OBJ_TREE:
		case OBJ_BLOB:
		case OBJ_TAG:
			real_type = oe_type(entry);
			break;

		default:
			real_type = oid_object_info(to_pack->repo,
						    &entry->idx.oid, NULL);
			break;
		}

		switch (real_type) {
		case OBJ_cummit:
			ewah_set(writer.cummits, i);
			break;

		case OBJ_TREE:
			ewah_set(writer.trees, i);
			break;

		case OBJ_BLOB:
			ewah_set(writer.blobs, i);
			break;

		case OBJ_TAG:
			ewah_set(writer.tags, i);
			break;

		default:
			die("Missing type information for %s (%d/%d)",
			    oid_to_hex(&entry->idx.oid), real_type,
			    oe_type(entry));
		}
	}
}

/**
 * Compute the actual bitmaps
 */

static inline void push_bitmapped_cummit(struct cummit *cummit)
{
	if (writer.selected_nr >= writer.selected_alloc) {
		writer.selected_alloc = (writer.selected_alloc + 32) * 2;
		REALLOC_ARRAY(writer.selected, writer.selected_alloc);
	}

	writer.selected[writer.selected_nr].cummit = cummit;
	writer.selected[writer.selected_nr].bitmap = NULL;
	writer.selected[writer.selected_nr].flags = 0;

	writer.selected_nr++;
}

static uint32_t find_object_pos(const struct object_id *oid, int *found)
{
	struct object_entry *entry = packlist_find(writer.to_pack, oid);

	if (!entry) {
		if (found)
			*found = 0;
		warning("Failed to write bitmap index. Packfile doesn't have full closure "
			"(object %s is missing)", oid_to_hex(oid));
		return 0;
	}

	if (found)
		*found = 1;
	return oe_in_pack_pos(writer.to_pack, entry);
}

static void compute_xor_offsets(void)
{
	static const int MAX_XOR_OFFSET_SEARCH = 10;

	int i, next = 0;

	while (next < writer.selected_nr) {
		struct bitmapped_cummit *stored = &writer.selected[next];

		int best_offset = 0;
		struct ewah_bitmap *best_bitmap = stored->bitmap;
		struct ewah_bitmap *test_xor;

		for (i = 1; i <= MAX_XOR_OFFSET_SEARCH; ++i) {
			int curr = next - i;

			if (curr < 0)
				break;

			test_xor = ewah_pool_new();
			ewah_xor(writer.selected[curr].bitmap, stored->bitmap, test_xor);

			if (test_xor->buffer_size < best_bitmap->buffer_size) {
				if (best_bitmap != stored->bitmap)
					ewah_pool_free(best_bitmap);

				best_bitmap = test_xor;
				best_offset = i;
			} else {
				ewah_pool_free(test_xor);
			}
		}

		stored->xor_offset = best_offset;
		stored->write_as = best_bitmap;

		next++;
	}
}

struct bb_cummit {
	struct cummit_list *reverse_edges;
	struct bitmap *cummit_mask;
	struct bitmap *bitmap;
	unsigned selected:1,
		 maximal:1;
	unsigned idx; /* within selected array */
};

define_cummit_slab(bb_data, struct bb_cummit);

struct bitmap_builder {
	struct bb_data data;
	struct cummit **cummits;
	size_t cummits_nr, cummits_alloc;
};

static void bitmap_builder_init(struct bitmap_builder *bb,
				struct bitmap_writer *writer,
				struct bitmap_index *old_bitmap)
{
	struct rev_info revs;
	struct cummit *cummit;
	struct cummit_list *reusable = NULL;
	struct cummit_list *r;
	unsigned int i, num_maximal = 0;

	memset(bb, 0, sizeof(*bb));
	init_bb_data(&bb->data);

	reset_revision_walk();
	repo_init_revisions(writer->to_pack->repo, &revs, NULL);
	revs.topo_order = 1;
	revs.first_parent_only = 1;

	for (i = 0; i < writer->selected_nr; i++) {
		struct cummit *c = writer->selected[i].cummit;
		struct bb_cummit *ent = bb_data_at(&bb->data, c);

		ent->selected = 1;
		ent->maximal = 1;
		ent->idx = i;

		ent->cummit_mask = bitmap_new();
		bitmap_set(ent->cummit_mask, i);

		add_pending_object(&revs, &c->object, "");
	}

	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");

	while ((cummit = get_revision(&revs))) {
		struct cummit_list *p = cummit->parents;
		struct bb_cummit *c_ent;

		parse_cummit_or_die(cummit);

		c_ent = bb_data_at(&bb->data, cummit);

		/*
		 * If there is no cummit_mask, there is no reason to iterate
		 * over this cummit; it is not selected (if it were, it would
		 * not have a blank cummit mask) and all its children have
		 * existing bitmaps (see the comment starting with "This cummit
		 * has an existing bitmap" below), so it does not contribute
		 * anything to the final bitmap file or its descendants.
		 */
		if (!c_ent->cummit_mask)
			continue;

		if (old_bitmap && bitmap_for_cummit(old_bitmap, cummit)) {
			/*
			 * This commit has an existing bitmap, so we can
			 * get its bits immediately without an object
			 * walk. That is, it is reusable as-is and there is no
			 * need to continue walking beyond it.
			 *
			 * Mark it as such and add it to bb->cummits separately
			 * to avoid allocating a position in the cummit mask.
			 */
			cummit_list_insert(cummit, &reusable);
			goto next;
		}

		if (c_ent->maximal) {
			num_maximal++;
			ALLOC_GROW(bb->cummits, bb->cummits_nr + 1, bb->cummits_alloc);
			bb->cummits[bb->cummits_nr++] = cummit;
		}

		if (p) {
			struct bb_cummit *p_ent = bb_data_at(&bb->data, p->item);
			int c_not_p, p_not_c;

			if (!p_ent->cummit_mask) {
				p_ent->cummit_mask = bitmap_new();
				c_not_p = 1;
				p_not_c = 0;
			} else {
				c_not_p = bitmap_is_subset(c_ent->cummit_mask, p_ent->cummit_mask);
				p_not_c = bitmap_is_subset(p_ent->cummit_mask, c_ent->cummit_mask);
			}

			if (!c_not_p)
				continue;

			bitmap_or(p_ent->cummit_mask, c_ent->cummit_mask);

			if (p_not_c)
				p_ent->maximal = 1;
			else {
				p_ent->maximal = 0;
				free_cummit_list(p_ent->reverse_edges);
				p_ent->reverse_edges = NULL;
			}

			if (c_ent->maximal) {
				cummit_list_insert(cummit, &p_ent->reverse_edges);
			} else {
				struct cummit_list *cc = c_ent->reverse_edges;

				for (; cc; cc = cc->next) {
					if (!cummit_list_contains(cc->item, p_ent->reverse_edges))
						cummit_list_insert(cc->item, &p_ent->reverse_edges);
				}
			}
		}

next:
		bitmap_free(c_ent->cummit_mask);
		c_ent->cummit_mask = NULL;
	}

	for (r = reusable; r; r = r->next) {
		ALLOC_GROW(bb->cummits, bb->cummits_nr + 1, bb->cummits_alloc);
		bb->cummits[bb->cummits_nr++] = r->item;
	}

	trace2_data_intmax("pack-bitmap-write", the_repository,
			   "num_selected_cummits", writer->selected_nr);
	trace2_data_intmax("pack-bitmap-write", the_repository,
			   "num_maximal_cummits", num_maximal);

	free_cummit_list(reusable);
}

static void bitmap_builder_clear(struct bitmap_builder *bb)
{
	clear_bb_data(&bb->data);
	free(bb->cummits);
	bb->cummits_nr = bb->cummits_alloc = 0;
}

static int fill_bitmap_tree(struct bitmap *bitmap,
			    struct tree *tree)
{
	int found;
	uint32_t pos;
	struct tree_desc desc;
	struct name_entry entry;

	/*
	 * If our bit is already set, then there is nothing to do. Both this
	 * tree and all of its children will be set.
	 */
	pos = find_object_pos(&tree->object.oid, &found);
	if (!found)
		return -1;
	if (bitmap_get(bitmap, pos))
		return 0;
	bitmap_set(bitmap, pos);

	if (parse_tree(tree) < 0)
		die("unable to load tree object %s",
		    oid_to_hex(&tree->object.oid));
	init_tree_desc(&desc, tree->buffer, tree->size);

	while (tree_entry(&desc, &entry)) {
		switch (object_type(entry.mode)) {
		case OBJ_TREE:
			if (fill_bitmap_tree(bitmap,
					     lookup_tree(the_repository, &entry.oid)) < 0)
				return -1;
			break;
		case OBJ_BLOB:
			pos = find_object_pos(&entry.oid, &found);
			if (!found)
				return -1;
			bitmap_set(bitmap, pos);
			break;
		default:
			/* Gitlink, etc; not reachable */
			break;
		}
	}

	free_tree_buffer(tree);
	return 0;
}

static int fill_bitmap_cummit(struct bb_cummit *ent,
			      struct cummit *cummit,
			      struct prio_queue *queue,
			      struct prio_queue *tree_queue,
			      struct bitmap_index *old_bitmap,
			      const uint32_t *mapping)
{
	int found;
	uint32_t pos;
	if (!ent->bitmap)
		ent->bitmap = bitmap_new();

	prio_queue_put(queue, cummit);

	while (queue->nr) {
		struct cummit_list *p;
		struct cummit *c = prio_queue_get(queue);

		if (old_bitmap && mapping) {
			struct ewah_bitmap *old = bitmap_for_cummit(old_bitmap, c);
			/*
			 * If this commit has an old bitmap, then translate that
			 * bitmap and add its bits to this one. No need to walk
			 * parents or the tree for this cummit.
			 */
			if (old && !rebuild_bitmap(mapping, old, ent->bitmap))
				continue;
		}

		/*
		 * Mark ourselves and queue our tree. The cummit
		 * walk ensures we cover all parents.
		 */
		pos = find_object_pos(&c->object.oid, &found);
		if (!found)
			return -1;
		bitmap_set(ent->bitmap, pos);
		prio_queue_put(tree_queue, get_cummit_tree(c));

		for (p = c->parents; p; p = p->next) {
			pos = find_object_pos(&p->item->object.oid, &found);
			if (!found)
				return -1;
			if (!bitmap_get(ent->bitmap, pos)) {
				bitmap_set(ent->bitmap, pos);
				prio_queue_put(queue, p->item);
			}
		}
	}

	while (tree_queue->nr) {
		if (fill_bitmap_tree(ent->bitmap,
				     prio_queue_get(tree_queue)) < 0)
			return -1;
	}
	return 0;
}

static void store_selected(struct bb_cummit *ent, struct cummit *cummit)
{
	struct bitmapped_cummit *stored = &writer.selected[ent->idx];
	khiter_t hash_pos;
	int hash_ret;

	stored->bitmap = bitmap_to_ewah(ent->bitmap);

	hash_pos = kh_put_oid_map(writer.bitmaps, cummit->object.oid, &hash_ret);
	if (hash_ret == 0)
		die("Duplicate entry when writing index: %s",
		    oid_to_hex(&cummit->object.oid));
	kh_value(writer.bitmaps, hash_pos) = stored;
}

int bitmap_writer_build(struct packing_data *to_pack)
{
	struct bitmap_builder bb;
	size_t i;
	int nr_stored = 0; /* for progress */
	struct prio_queue queue = { compare_cummits_by_gen_then_cummit_date };
	struct prio_queue tree_queue = { NULL };
	struct bitmap_index *old_bitmap;
	uint32_t *mapping;
	int closed = 1; /* until proven otherwise */

	writer.bitmaps = kh_init_oid_map();
	writer.to_pack = to_pack;

	if (writer.show_progress)
		writer.progress = start_progress("Building bitmaps", writer.selected_nr);
	trace2_region_enter("pack-bitmap-write", "building_bitmaps_total",
			    the_repository);

	old_bitmap = prepare_bitmap_git(to_pack->repo);
	if (old_bitmap)
		mapping = create_bitmap_mapping(old_bitmap, to_pack);
	else
		mapping = NULL;

	bitmap_builder_init(&bb, &writer, old_bitmap);
	for (i = bb.cummits_nr; i > 0; i--) {
		struct cummit *cummit = bb.cummits[i-1];
		struct bb_cummit *ent = bb_data_at(&bb.data, cummit);
		struct cummit *child;
		int reused = 0;

		if (fill_bitmap_cummit(ent, cummit, &queue, &tree_queue,
				       old_bitmap, mapping) < 0) {
			closed = 0;
			break;
		}

		if (ent->selected) {
			store_selected(ent, cummit);
			nr_stored++;
			display_progress(writer.progress, nr_stored);
		}

		while ((child = pop_cummit(&ent->reverse_edges))) {
			struct bb_cummit *child_ent =
				bb_data_at(&bb.data, child);

			if (child_ent->bitmap)
				bitmap_or(child_ent->bitmap, ent->bitmap);
			else if (reused)
				child_ent->bitmap = bitmap_dup(ent->bitmap);
			else {
				child_ent->bitmap = ent->bitmap;
				reused = 1;
			}
		}
		if (!reused)
			bitmap_free(ent->bitmap);
		ent->bitmap = NULL;
	}
	clear_prio_queue(&queue);
	clear_prio_queue(&tree_queue);
	bitmap_builder_clear(&bb);
	free_bitmap_index(old_bitmap);
	free(mapping);

	trace2_region_leave("pack-bitmap-write", "building_bitmaps_total",
			    the_repository);

	stop_progress(&writer.progress);

	if (closed)
		compute_xor_offsets();
	return closed ? 0 : -1;
}

/**
 * Select the cummits that will be bitmapped
 */
static inline unsigned int next_cummit_index(unsigned int idx)
{
	static const unsigned int MIN_cummitS = 100;
	static const unsigned int MAX_cummitS = 5000;

	static const unsigned int MUST_REGION = 100;
	static const unsigned int MIN_REGION = 20000;

	unsigned int offset, next;

	if (idx <= MUST_REGION)
		return 0;

	if (idx <= MIN_REGION) {
		offset = idx - MUST_REGION;
		return (offset < MIN_cummitS) ? offset : MIN_cummitS;
	}

	offset = idx - MIN_REGION;
	next = (offset < MAX_cummitS) ? offset : MAX_cummitS;

	return (next > MIN_cummitS) ? next : MIN_cummitS;
}

static int date_compare(const void *_a, const void *_b)
{
	struct cummit *a = *(struct cummit **)_a;
	struct cummit *b = *(struct cummit **)_b;
	return (long)b->date - (long)a->date;
}

void bitmap_writer_select_cummits(struct cummit **indexed_cummits,
				  unsigned int indexed_cummits_nr,
				  int max_bitmaps)
{
	unsigned int i = 0, j, next;

	QSORT(indexed_cummits, indexed_cummits_nr, date_compare);

	if (indexed_cummits_nr < 100) {
		for (i = 0; i < indexed_cummits_nr; ++i)
			push_bitmapped_cummit(indexed_cummits[i]);
		return;
	}

	if (writer.show_progress)
		writer.progress = start_progress("Selecting bitmap cummits", 0);

	for (;;) {
		struct cummit *chosen = NULL;

		next = next_cummit_index(i);

		if (i + next >= indexed_cummits_nr)
			break;

		if (max_bitmaps > 0 && writer.selected_nr >= max_bitmaps) {
			writer.selected_nr = max_bitmaps;
			break;
		}

		if (next == 0) {
			chosen = indexed_cummits[i];
		} else {
			chosen = indexed_cummits[i + next];

			for (j = 0; j <= next; ++j) {
				struct cummit *cm = indexed_cummits[i + j];

				if ((cm->object.flags & NEEDS_BITMAP) != 0) {
					chosen = cm;
					break;
				}

				if (cm->parents && cm->parents->next)
					chosen = cm;
			}
		}

		push_bitmapped_cummit(chosen);

		i += next + 1;
		display_progress(writer.progress, i);
	}

	stop_progress(&writer.progress);
}


static int hashwrite_ewah_helper(void *f, const void *buf, size_t len)
{
	/* hashwrite will die on error */
	hashwrite(f, buf, len);
	return len;
}

/**
 * Write the bitmap index to disk
 */
static inline void dump_bitmap(struct hashfile *f, struct ewah_bitmap *bitmap)
{
	if (ewah_serialize_to(bitmap, hashwrite_ewah_helper, f) < 0)
		die("Failed to write bitmap index");
}

static const struct object_id *oid_access(size_t pos, const void *table)
{
	const struct pack_idx_entry * const *index = table;
	return &index[pos]->oid;
}

static void write_selected_cummits_v1(struct hashfile *f,
				      struct pack_idx_entry **index,
				      uint32_t index_nr)
{
	int i;

	for (i = 0; i < writer.selected_nr; ++i) {
		struct bitmapped_cummit *stored = &writer.selected[i];

		int cummit_pos =
			oid_pos(&stored->cummit->object.oid, index, index_nr, oid_access);

		if (cummit_pos < 0)
			BUG("trying to write cummit not in index");

		hashwrite_be32(f, cummit_pos);
		hashwrite_u8(f, stored->xor_offset);
		hashwrite_u8(f, stored->flags);

		dump_bitmap(f, stored->write_as);
	}
}

static void write_hash_cache(struct hashfile *f,
			     struct pack_idx_entry **index,
			     uint32_t index_nr)
{
	uint32_t i;

	for (i = 0; i < index_nr; ++i) {
		struct object_entry *entry = (struct object_entry *)index[i];
		hashwrite_be32(f, entry->hash);
	}
}

void bitmap_writer_set_checksum(unsigned char *sha1)
{
	hashcpy(writer.pack_checksum, sha1);
}

void bitmap_writer_finish(struct pack_idx_entry **index,
			  uint32_t index_nr,
			  const char *filename,
			  uint16_t options)
{
	static uint16_t default_version = 1;
	static uint16_t flags = BITMAP_OPT_FULL_DAG;
	struct strbuf tmp_file = STRBUF_INIT;
	struct hashfile *f;

	struct bitmap_disk_header header;

	int fd = odb_mkstemp(&tmp_file, "pack/tmp_bitmap_XXXXXX");

	f = hashfd(fd, tmp_file.buf);

	memcpy(header.magic, BITMAP_IDX_SIGNATURE, sizeof(BITMAP_IDX_SIGNATURE));
	header.version = htons(default_version);
	header.options = htons(flags | options);
	header.entry_count = htonl(writer.selected_nr);
	hashcpy(header.checksum, writer.pack_checksum);

	hashwrite(f, &header, sizeof(header) - GIT_MAX_RAWSZ + the_hash_algo->rawsz);
	dump_bitmap(f, writer.cummits);
	dump_bitmap(f, writer.trees);
	dump_bitmap(f, writer.blobs);
	dump_bitmap(f, writer.tags);
	write_selected_cummits_v1(f, index, index_nr);

	if (options & BITMAP_OPT_HASH_CACHE)
		write_hash_cache(f, index, index_nr);

	finalize_hashfile(f, NULL, FSYNC_COMPONENT_PACK_METADATA,
			  CSUM_HASH_IN_STREAM | CSUM_FSYNC | CSUM_CLOSE);

	if (adjust_shared_perm(tmp_file.buf))
		die_errno("unable to make temporary bitmap file readable");

	if (rename(tmp_file.buf, filename))
		die_errno("unable to rename temporary bitmap file to '%s'", filename);

	strbuf_release(&tmp_file);
}
