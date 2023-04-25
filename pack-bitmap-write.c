#include "git-compat-util.h"
#include "alloc.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "object-store.h"
#include "commit.h"
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
#include "commit-reach.h"
#include "prio-queue.h"
#include "trace2.h"

struct bitmapped_commit {
	struct commit *commit;
	struct ewah_bitmap *bitmap;
	struct ewah_bitmap *write_as;
	int flags;
	int xor_offset;
	uint32_t commit_pos;
};

struct bitmap_writer {
	struct ewah_bitmap *commits;
	struct ewah_bitmap *trees;
	struct ewah_bitmap *blobs;
	struct ewah_bitmap *tags;

	kh_oid_map_t *bitmaps;
	struct packing_data *to_pack;

	struct bitmapped_commit *selected;
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

	writer.commits = ewah_new();
	writer.trees = ewah_new();
	writer.blobs = ewah_new();
	writer.tags = ewah_new();
	ALLOC_ARRAY(to_pack->in_pack_pos, to_pack->nr_objects);

	for (i = 0; i < index_nr; ++i) {
		struct object_entry *entry = (struct object_entry *)index[i];
		enum object_type real_type;

		oe_set_in_pack_pos(to_pack, entry, i);

		switch (oe_type(entry)) {
		case OBJ_COMMIT:
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
		case OBJ_COMMIT:
			ewah_set(writer.commits, i);
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

static inline void push_bitmapped_commit(struct commit *commit)
{
	if (writer.selected_nr >= writer.selected_alloc) {
		writer.selected_alloc = (writer.selected_alloc + 32) * 2;
		REALLOC_ARRAY(writer.selected, writer.selected_alloc);
	}

	writer.selected[writer.selected_nr].commit = commit;
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
		struct bitmapped_commit *stored = &writer.selected[next];

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

struct bb_commit {
	struct commit_list *reverse_edges;
	struct bitmap *commit_mask;
	struct bitmap *bitmap;
	unsigned selected:1,
		 maximal:1;
	unsigned idx; /* within selected array */
};

define_commit_slab(bb_data, struct bb_commit);

struct bitmap_builder {
	struct bb_data data;
	struct commit **commits;
	size_t commits_nr, commits_alloc;
};

static void bitmap_builder_init(struct bitmap_builder *bb,
				struct bitmap_writer *writer,
				struct bitmap_index *old_bitmap)
{
	struct rev_info revs;
	struct commit *commit;
	struct commit_list *reusable = NULL;
	struct commit_list *r;
	unsigned int i, num_maximal = 0;

	memset(bb, 0, sizeof(*bb));
	init_bb_data(&bb->data);

	reset_revision_walk();
	repo_init_revisions(writer->to_pack->repo, &revs, NULL);
	revs.topo_order = 1;
	revs.first_parent_only = 1;

	for (i = 0; i < writer->selected_nr; i++) {
		struct commit *c = writer->selected[i].commit;
		struct bb_commit *ent = bb_data_at(&bb->data, c);

		ent->selected = 1;
		ent->maximal = 1;
		ent->idx = i;

		ent->commit_mask = bitmap_new();
		bitmap_set(ent->commit_mask, i);

		add_pending_object(&revs, &c->object, "");
	}

	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");

	while ((commit = get_revision(&revs))) {
		struct commit_list *p = commit->parents;
		struct bb_commit *c_ent;

		parse_commit_or_die(commit);

		c_ent = bb_data_at(&bb->data, commit);

		/*
		 * If there is no commit_mask, there is no reason to iterate
		 * over this commit; it is not selected (if it were, it would
		 * not have a blank commit mask) and all its children have
		 * existing bitmaps (see the comment starting with "This commit
		 * has an existing bitmap" below), so it does not contribute
		 * anything to the final bitmap file or its descendants.
		 */
		if (!c_ent->commit_mask)
			continue;

		if (old_bitmap && bitmap_for_commit(old_bitmap, commit)) {
			/*
			 * This commit has an existing bitmap, so we can
			 * get its bits immediately without an object
			 * walk. That is, it is reusable as-is and there is no
			 * need to continue walking beyond it.
			 *
			 * Mark it as such and add it to bb->commits separately
			 * to avoid allocating a position in the commit mask.
			 */
			commit_list_insert(commit, &reusable);
			goto next;
		}

		if (c_ent->maximal) {
			num_maximal++;
			ALLOC_GROW(bb->commits, bb->commits_nr + 1, bb->commits_alloc);
			bb->commits[bb->commits_nr++] = commit;
		}

		if (p) {
			struct bb_commit *p_ent = bb_data_at(&bb->data, p->item);
			int c_not_p, p_not_c;

			if (!p_ent->commit_mask) {
				p_ent->commit_mask = bitmap_new();
				c_not_p = 1;
				p_not_c = 0;
			} else {
				c_not_p = bitmap_is_subset(c_ent->commit_mask, p_ent->commit_mask);
				p_not_c = bitmap_is_subset(p_ent->commit_mask, c_ent->commit_mask);
			}

			if (!c_not_p)
				continue;

			bitmap_or(p_ent->commit_mask, c_ent->commit_mask);

			if (p_not_c)
				p_ent->maximal = 1;
			else {
				p_ent->maximal = 0;
				free_commit_list(p_ent->reverse_edges);
				p_ent->reverse_edges = NULL;
			}

			if (c_ent->maximal) {
				commit_list_insert(commit, &p_ent->reverse_edges);
			} else {
				struct commit_list *cc = c_ent->reverse_edges;

				for (; cc; cc = cc->next) {
					if (!commit_list_contains(cc->item, p_ent->reverse_edges))
						commit_list_insert(cc->item, &p_ent->reverse_edges);
				}
			}
		}

next:
		bitmap_free(c_ent->commit_mask);
		c_ent->commit_mask = NULL;
	}

	for (r = reusable; r; r = r->next) {
		ALLOC_GROW(bb->commits, bb->commits_nr + 1, bb->commits_alloc);
		bb->commits[bb->commits_nr++] = r->item;
	}

	trace2_data_intmax("pack-bitmap-write", the_repository,
			   "num_selected_commits", writer->selected_nr);
	trace2_data_intmax("pack-bitmap-write", the_repository,
			   "num_maximal_commits", num_maximal);

	release_revisions(&revs);
	free_commit_list(reusable);
}

static void bitmap_builder_clear(struct bitmap_builder *bb)
{
	clear_bb_data(&bb->data);
	free(bb->commits);
	bb->commits_nr = bb->commits_alloc = 0;
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

static int reused_bitmaps_nr;

static int fill_bitmap_commit(struct bb_commit *ent,
			      struct commit *commit,
			      struct prio_queue *queue,
			      struct prio_queue *tree_queue,
			      struct bitmap_index *old_bitmap,
			      const uint32_t *mapping)
{
	int found;
	uint32_t pos;
	if (!ent->bitmap)
		ent->bitmap = bitmap_new();

	prio_queue_put(queue, commit);

	while (queue->nr) {
		struct commit_list *p;
		struct commit *c = prio_queue_get(queue);

		if (old_bitmap && mapping) {
			struct ewah_bitmap *old = bitmap_for_commit(old_bitmap, c);
			/*
			 * If this commit has an old bitmap, then translate that
			 * bitmap and add its bits to this one. No need to walk
			 * parents or the tree for this commit.
			 */
			if (old && !rebuild_bitmap(mapping, old, ent->bitmap)) {
				reused_bitmaps_nr++;
				continue;
			}
		}

		/*
		 * Mark ourselves and queue our tree. The commit
		 * walk ensures we cover all parents.
		 */
		pos = find_object_pos(&c->object.oid, &found);
		if (!found)
			return -1;
		bitmap_set(ent->bitmap, pos);
		prio_queue_put(tree_queue,
			       repo_get_commit_tree(the_repository, c));

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

static void store_selected(struct bb_commit *ent, struct commit *commit)
{
	struct bitmapped_commit *stored = &writer.selected[ent->idx];
	khiter_t hash_pos;
	int hash_ret;

	stored->bitmap = bitmap_to_ewah(ent->bitmap);

	hash_pos = kh_put_oid_map(writer.bitmaps, commit->object.oid, &hash_ret);
	if (hash_ret == 0)
		die("Duplicate entry when writing index: %s",
		    oid_to_hex(&commit->object.oid));
	kh_value(writer.bitmaps, hash_pos) = stored;
}

int bitmap_writer_build(struct packing_data *to_pack)
{
	struct bitmap_builder bb;
	size_t i;
	int nr_stored = 0; /* for progress */
	struct prio_queue queue = { compare_commits_by_gen_then_commit_date };
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
	for (i = bb.commits_nr; i > 0; i--) {
		struct commit *commit = bb.commits[i-1];
		struct bb_commit *ent = bb_data_at(&bb.data, commit);
		struct commit *child;
		int reused = 0;

		if (fill_bitmap_commit(ent, commit, &queue, &tree_queue,
				       old_bitmap, mapping) < 0) {
			closed = 0;
			break;
		}

		if (ent->selected) {
			store_selected(ent, commit);
			nr_stored++;
			display_progress(writer.progress, nr_stored);
		}

		while ((child = pop_commit(&ent->reverse_edges))) {
			struct bb_commit *child_ent =
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
	trace2_data_intmax("pack-bitmap-write", the_repository,
			   "building_bitmaps_reused", reused_bitmaps_nr);

	stop_progress(&writer.progress);

	if (closed)
		compute_xor_offsets();
	return closed ? 0 : -1;
}

/**
 * Select the commits that will be bitmapped
 */
static inline unsigned int next_commit_index(unsigned int idx)
{
	static const unsigned int MIN_COMMITS = 100;
	static const unsigned int MAX_COMMITS = 5000;

	static const unsigned int MUST_REGION = 100;
	static const unsigned int MIN_REGION = 20000;

	unsigned int offset, next;

	if (idx <= MUST_REGION)
		return 0;

	if (idx <= MIN_REGION) {
		offset = idx - MUST_REGION;
		return (offset < MIN_COMMITS) ? offset : MIN_COMMITS;
	}

	offset = idx - MIN_REGION;
	next = (offset < MAX_COMMITS) ? offset : MAX_COMMITS;

	return (next > MIN_COMMITS) ? next : MIN_COMMITS;
}

static int date_compare(const void *_a, const void *_b)
{
	struct commit *a = *(struct commit **)_a;
	struct commit *b = *(struct commit **)_b;
	return (long)b->date - (long)a->date;
}

void bitmap_writer_select_commits(struct commit **indexed_commits,
				  unsigned int indexed_commits_nr,
				  int max_bitmaps)
{
	unsigned int i = 0, j, next;

	QSORT(indexed_commits, indexed_commits_nr, date_compare);

	if (indexed_commits_nr < 100) {
		for (i = 0; i < indexed_commits_nr; ++i)
			push_bitmapped_commit(indexed_commits[i]);
		return;
	}

	if (writer.show_progress)
		writer.progress = start_progress("Selecting bitmap commits", 0);

	for (;;) {
		struct commit *chosen = NULL;

		next = next_commit_index(i);

		if (i + next >= indexed_commits_nr)
			break;

		if (max_bitmaps > 0 && writer.selected_nr >= max_bitmaps) {
			writer.selected_nr = max_bitmaps;
			break;
		}

		if (next == 0) {
			chosen = indexed_commits[i];
		} else {
			chosen = indexed_commits[i + next];

			for (j = 0; j <= next; ++j) {
				struct commit *cm = indexed_commits[i + j];

				if ((cm->object.flags & NEEDS_BITMAP) != 0) {
					chosen = cm;
					break;
				}

				if (cm->parents && cm->parents->next)
					chosen = cm;
			}
		}

		push_bitmapped_commit(chosen);

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

static void write_selected_commits_v1(struct hashfile *f,
				      uint32_t *commit_positions,
				      off_t *offsets)
{
	int i;

	for (i = 0; i < writer.selected_nr; ++i) {
		struct bitmapped_commit *stored = &writer.selected[i];

		if (offsets)
			offsets[i] = hashfile_total(f);

		hashwrite_be32(f, commit_positions[i]);
		hashwrite_u8(f, stored->xor_offset);
		hashwrite_u8(f, stored->flags);

		dump_bitmap(f, stored->write_as);
	}
}

static int table_cmp(const void *_va, const void *_vb, void *_data)
{
	uint32_t *commit_positions = _data;
	uint32_t a = commit_positions[*(uint32_t *)_va];
	uint32_t b = commit_positions[*(uint32_t *)_vb];

	if (a > b)
		return 1;
	else if (a < b)
		return -1;

	return 0;
}

static void write_lookup_table(struct hashfile *f,
			       uint32_t *commit_positions,
			       off_t *offsets)
{
	uint32_t i;
	uint32_t *table, *table_inv;

	ALLOC_ARRAY(table, writer.selected_nr);
	ALLOC_ARRAY(table_inv, writer.selected_nr);

	for (i = 0; i < writer.selected_nr; i++)
		table[i] = i;

	/*
	 * At the end of this sort table[j] = i means that the i'th
	 * bitmap corresponds to j'th bitmapped commit (among the selected
	 * commits) in lex order of OIDs.
	 */
	QSORT_S(table, writer.selected_nr, table_cmp, commit_positions);

	/* table_inv helps us discover that relationship (i'th bitmap
	 * to j'th commit by j = table_inv[i])
	 */
	for (i = 0; i < writer.selected_nr; i++)
		table_inv[table[i]] = i;

	trace2_region_enter("pack-bitmap-write", "writing_lookup_table", the_repository);
	for (i = 0; i < writer.selected_nr; i++) {
		struct bitmapped_commit *selected = &writer.selected[table[i]];
		uint32_t xor_offset = selected->xor_offset;
		uint32_t xor_row;

		if (xor_offset) {
			/*
			 * xor_index stores the index (in the bitmap entries)
			 * of the corresponding xor bitmap. But we need to convert
			 * this index into lookup table's index. So, table_inv[xor_index]
			 * gives us the index position w.r.t. the lookup table.
			 *
			 * If "k = table[i] - xor_offset" then the xor base is the k'th
			 * bitmap. `table_inv[k]` gives us the position of that bitmap
			 * in the lookup table.
			 */
			uint32_t xor_index = table[i] - xor_offset;
			xor_row = table_inv[xor_index];
		} else {
			xor_row = 0xffffffff;
		}

		hashwrite_be32(f, commit_positions[table[i]]);
		hashwrite_be64(f, (uint64_t)offsets[table[i]]);
		hashwrite_be32(f, xor_row);
	}
	trace2_region_leave("pack-bitmap-write", "writing_lookup_table", the_repository);

	free(table);
	free(table_inv);
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

void bitmap_writer_set_checksum(const unsigned char *sha1)
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
	uint32_t *commit_positions = NULL;
	off_t *offsets = NULL;
	uint32_t i;

	struct bitmap_disk_header header;

	int fd = odb_mkstemp(&tmp_file, "pack/tmp_bitmap_XXXXXX");

	f = hashfd(fd, tmp_file.buf);

	memcpy(header.magic, BITMAP_IDX_SIGNATURE, sizeof(BITMAP_IDX_SIGNATURE));
	header.version = htons(default_version);
	header.options = htons(flags | options);
	header.entry_count = htonl(writer.selected_nr);
	hashcpy(header.checksum, writer.pack_checksum);

	hashwrite(f, &header, sizeof(header) - GIT_MAX_RAWSZ + the_hash_algo->rawsz);
	dump_bitmap(f, writer.commits);
	dump_bitmap(f, writer.trees);
	dump_bitmap(f, writer.blobs);
	dump_bitmap(f, writer.tags);

	if (options & BITMAP_OPT_LOOKUP_TABLE)
		CALLOC_ARRAY(offsets, index_nr);

	ALLOC_ARRAY(commit_positions, writer.selected_nr);

	for (i = 0; i < writer.selected_nr; i++) {
		struct bitmapped_commit *stored = &writer.selected[i];
		int commit_pos = oid_pos(&stored->commit->object.oid, index, index_nr, oid_access);

		if (commit_pos < 0)
			BUG(_("trying to write commit not in index"));

		commit_positions[i] = commit_pos;
	}

	write_selected_commits_v1(f, commit_positions, offsets);

	if (options & BITMAP_OPT_LOOKUP_TABLE)
		write_lookup_table(f, commit_positions, offsets);

	if (options & BITMAP_OPT_HASH_CACHE)
		write_hash_cache(f, index, index_nr);

	finalize_hashfile(f, NULL, FSYNC_COMPONENT_PACK_METADATA,
			  CSUM_HASH_IN_STREAM | CSUM_FSYNC | CSUM_CLOSE);

	if (adjust_shared_perm(tmp_file.buf))
		die_errno("unable to make temporary bitmap file readable");

	if (rename(tmp_file.buf, filename))
		die_errno("unable to rename temporary bitmap file to '%s'", filename);

	strbuf_release(&tmp_file);
	free(commit_positions);
	free(offsets);
}
