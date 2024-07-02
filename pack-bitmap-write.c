#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "object-store-ll.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "progress.h"
#include "pack.h"
#include "pack-bitmap.h"
#include "hash-lookup.h"
#include "pack-objects.h"
#include "path.h"
#include "commit-reach.h"
#include "prio-queue.h"
#include "trace2.h"
#include "tree.h"
#include "tree-walk.h"
#include "pseudo-merge.h"
#include "oid-array.h"
#include "config.h"
#include "alloc.h"
#include "refs.h"
#include "strmap.h"

struct bitmapped_commit {
	struct commit *commit;
	struct ewah_bitmap *bitmap;
	struct ewah_bitmap *write_as;
	int flags;
	int xor_offset;
	uint32_t commit_pos;
	unsigned pseudo_merge : 1;
};

static inline int bitmap_writer_nr_selected_commits(struct bitmap_writer *writer)
{
	return writer->selected_nr - writer->pseudo_merges_nr;
}

void bitmap_writer_init(struct bitmap_writer *writer, struct repository *r)
{
	memset(writer, 0, sizeof(struct bitmap_writer));
	if (writer->bitmaps)
		BUG("bitmap writer already initialized");
	writer->bitmaps = kh_init_oid_map();
	writer->pseudo_merge_commits = kh_init_oid_map();

	string_list_init_dup(&writer->pseudo_merge_groups);

	load_pseudo_merges_from_config(&writer->pseudo_merge_groups);
}

static void free_pseudo_merge_commit_idx(struct pseudo_merge_commit_idx *idx)
{
	if (!idx)
		return;
	free(idx->pseudo_merge);
	free(idx);
}

void bitmap_writer_free(struct bitmap_writer *writer)
{
	uint32_t i;
	struct pseudo_merge_commit_idx *idx;

	if (!writer)
		return;

	ewah_free(writer->commits);
	ewah_free(writer->trees);
	ewah_free(writer->blobs);
	ewah_free(writer->tags);

	kh_destroy_oid_map(writer->bitmaps);

	kh_foreach_value(writer->pseudo_merge_commits, idx,
			 free_pseudo_merge_commit_idx(idx));
	kh_destroy_oid_map(writer->pseudo_merge_commits);

	for (i = 0; i < writer->selected_nr; i++) {
		struct bitmapped_commit *bc = &writer->selected[i];
		if (bc->write_as != bc->bitmap)
			ewah_free(bc->write_as);
		ewah_free(bc->bitmap);
	}
	free(writer->selected);
}

void bitmap_writer_show_progress(struct bitmap_writer *writer, int show)
{
	writer->show_progress = show;
}

/**
 * Build the initial type index for the packfile or multi-pack-index
 */
void bitmap_writer_build_type_index(struct bitmap_writer *writer,
				    struct packing_data *to_pack,
				    struct pack_idx_entry **index,
				    uint32_t index_nr)
{
	uint32_t i;

	writer->commits = ewah_new();
	writer->trees = ewah_new();
	writer->blobs = ewah_new();
	writer->tags = ewah_new();
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
			ewah_set(writer->commits, i);
			break;

		case OBJ_TREE:
			ewah_set(writer->trees, i);
			break;

		case OBJ_BLOB:
			ewah_set(writer->blobs, i);
			break;

		case OBJ_TAG:
			ewah_set(writer->tags, i);
			break;

		default:
			die("Missing type information for %s (%d/%d)",
			    oid_to_hex(&entry->idx.oid), real_type,
			    oe_type(entry));
		}
	}
}

int bitmap_writer_has_bitmapped_object_id(struct bitmap_writer *writer,
					  const struct object_id *oid)
{
	return kh_get_oid_map(writer->bitmaps, *oid) != kh_end(writer->bitmaps);
}

/**
 * Compute the actual bitmaps
 */

void bitmap_writer_push_commit(struct bitmap_writer *writer,
			       struct commit *commit, unsigned pseudo_merge)
{
	if (writer->selected_nr >= writer->selected_alloc) {
		writer->selected_alloc = (writer->selected_alloc + 32) * 2;
		REALLOC_ARRAY(writer->selected, writer->selected_alloc);
	}

	if (!pseudo_merge) {
		int hash_ret;
		khiter_t hash_pos = kh_put_oid_map(writer->bitmaps,
						   commit->object.oid,
						   &hash_ret);

		if (!hash_ret)
			die(_("duplicate entry when writing bitmap index: %s"),
			    oid_to_hex(&commit->object.oid));
		kh_value(writer->bitmaps, hash_pos) = NULL;
	}

	writer->selected[writer->selected_nr].commit = commit;
	writer->selected[writer->selected_nr].bitmap = NULL;
	writer->selected[writer->selected_nr].write_as = NULL;
	writer->selected[writer->selected_nr].flags = 0;
	writer->selected[writer->selected_nr].pseudo_merge = pseudo_merge;

	writer->selected_nr++;
}

static uint32_t find_object_pos(struct bitmap_writer *writer,
				const struct object_id *oid, int *found)
{
	struct object_entry *entry = packlist_find(writer->to_pack, oid);

	if (!entry) {
		if (found)
			*found = 0;
		warning("Failed to write bitmap index. Packfile doesn't have full closure "
			"(object %s is missing)", oid_to_hex(oid));
		return 0;
	}

	if (found)
		*found = 1;
	return oe_in_pack_pos(writer->to_pack, entry);
}

static void compute_xor_offsets(struct bitmap_writer *writer)
{
	static const int MAX_XOR_OFFSET_SEARCH = 10;

	int i, next = 0;

	while (next < writer->selected_nr) {
		struct bitmapped_commit *stored = &writer->selected[next];
		int best_offset = 0;
		struct ewah_bitmap *best_bitmap = stored->bitmap;
		struct ewah_bitmap *test_xor;

		if (stored->pseudo_merge)
			goto next;

		for (i = 1; i <= MAX_XOR_OFFSET_SEARCH; ++i) {
			int curr = next - i;

			if (curr < 0)
				break;
			if (writer->selected[curr].pseudo_merge)
				continue;

			test_xor = ewah_pool_new();
			ewah_xor(writer->selected[curr].bitmap, stored->bitmap, test_xor);

			if (test_xor->buffer_size < best_bitmap->buffer_size) {
				if (best_bitmap != stored->bitmap)
					ewah_pool_free(best_bitmap);

				best_bitmap = test_xor;
				best_offset = i;
			} else {
				ewah_pool_free(test_xor);
			}
		}

next:
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
		 maximal:1,
		 pseudo_merge:1;
	unsigned idx; /* within selected array */
};

static void clear_bb_commit(struct bb_commit *commit)
{
	free_commit_list(commit->reverse_edges);
	bitmap_free(commit->commit_mask);
	bitmap_free(commit->bitmap);
}

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
		struct bitmapped_commit *bc = &writer->selected[i];
		struct bb_commit *ent = bb_data_at(&bb->data, bc->commit);

		ent->selected = 1;
		ent->maximal = 1;
		ent->pseudo_merge = bc->pseudo_merge;
		ent->idx = i;

		ent->commit_mask = bitmap_new();
		bitmap_set(ent->commit_mask, i);

		add_pending_object(&revs, &bc->commit->object, "");
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
	deep_clear_bb_data(&bb->data, clear_bb_commit);
	free(bb->commits);
	bb->commits_nr = bb->commits_alloc = 0;
}

static int fill_bitmap_tree(struct bitmap_writer *writer,
			    struct bitmap *bitmap,
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
	pos = find_object_pos(writer, &tree->object.oid, &found);
	if (!found)
		return -1;
	if (bitmap_get(bitmap, pos))
		return 0;
	bitmap_set(bitmap, pos);

	if (parse_tree(tree) < 0)
		die("unable to load tree object %s",
		    oid_to_hex(&tree->object.oid));
	init_tree_desc(&desc, &tree->object.oid, tree->buffer, tree->size);

	while (tree_entry(&desc, &entry)) {
		switch (object_type(entry.mode)) {
		case OBJ_TREE:
			if (fill_bitmap_tree(writer, bitmap,
					     lookup_tree(the_repository, &entry.oid)) < 0)
				return -1;
			break;
		case OBJ_BLOB:
			pos = find_object_pos(writer, &entry.oid, &found);
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
static int reused_pseudo_merge_bitmaps_nr;

static int fill_bitmap_commit(struct bitmap_writer *writer,
			      struct bb_commit *ent,
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
			struct ewah_bitmap *old;
			struct bitmap *remapped = bitmap_new();

			if (commit->object.flags & BITMAP_PSEUDO_MERGE)
				old = pseudo_merge_bitmap_for_commit(old_bitmap, c);
			else
				old = bitmap_for_commit(old_bitmap, c);
			/*
			 * If this commit has an old bitmap, then translate that
			 * bitmap and add its bits to this one. No need to walk
			 * parents or the tree for this commit.
			 */
			if (old && !rebuild_bitmap(mapping, old, remapped)) {
				bitmap_or(ent->bitmap, remapped);
				bitmap_free(remapped);
				if (commit->object.flags & BITMAP_PSEUDO_MERGE)
					reused_pseudo_merge_bitmaps_nr++;
				else
					reused_bitmaps_nr++;
				continue;
			}
			bitmap_free(remapped);
		}

		/*
		 * Mark ourselves and queue our tree. The commit
		 * walk ensures we cover all parents.
		 */
		if (!(c->object.flags & BITMAP_PSEUDO_MERGE)) {
			pos = find_object_pos(writer, &c->object.oid, &found);
			if (!found)
				return -1;
			bitmap_set(ent->bitmap, pos);
			prio_queue_put(tree_queue,
				       repo_get_commit_tree(the_repository, c));
		}

		for (p = c->parents; p; p = p->next) {
			pos = find_object_pos(writer, &p->item->object.oid,
					      &found);
			if (!found)
				return -1;
			if (!bitmap_get(ent->bitmap, pos)) {
				bitmap_set(ent->bitmap, pos);
				prio_queue_put(queue, p->item);
			}
		}
	}

	while (tree_queue->nr) {
		if (fill_bitmap_tree(writer, ent->bitmap,
				     prio_queue_get(tree_queue)) < 0)
			return -1;
	}
	return 0;
}

static void store_selected(struct bitmap_writer *writer,
			   struct bb_commit *ent, struct commit *commit)
{
	struct bitmapped_commit *stored = &writer->selected[ent->idx];
	khiter_t hash_pos;

	stored->bitmap = bitmap_to_ewah(ent->bitmap);

	if (ent->pseudo_merge)
		return;

	hash_pos = kh_get_oid_map(writer->bitmaps, commit->object.oid);
	if (hash_pos == kh_end(writer->bitmaps))
		die(_("attempted to store non-selected commit: '%s'"),
		    oid_to_hex(&commit->object.oid));

	kh_value(writer->bitmaps, hash_pos) = stored;
}

int bitmap_writer_build(struct bitmap_writer *writer,
			struct packing_data *to_pack)
{
	struct bitmap_builder bb;
	size_t i;
	int nr_stored = 0; /* for progress */
	struct prio_queue queue = { compare_commits_by_gen_then_commit_date };
	struct prio_queue tree_queue = { NULL };
	struct bitmap_index *old_bitmap;
	uint32_t *mapping;
	int closed = 1; /* until proven otherwise */

	writer->to_pack = to_pack;

	if (writer->show_progress)
		writer->progress = start_progress("Building bitmaps",
						  writer->selected_nr);
	trace2_region_enter("pack-bitmap-write", "building_bitmaps_total",
			    the_repository);

	old_bitmap = prepare_bitmap_git(to_pack->repo);
	if (old_bitmap)
		mapping = create_bitmap_mapping(old_bitmap, to_pack);
	else
		mapping = NULL;

	bitmap_builder_init(&bb, writer, old_bitmap);
	for (i = bb.commits_nr; i > 0; i--) {
		struct commit *commit = bb.commits[i-1];
		struct bb_commit *ent = bb_data_at(&bb.data, commit);
		struct commit *child;
		int reused = 0;

		if (fill_bitmap_commit(writer, ent, commit, &queue, &tree_queue,
				       old_bitmap, mapping) < 0) {
			closed = 0;
			break;
		}

		if (ent->selected) {
			store_selected(writer, ent, commit);
			nr_stored++;
			display_progress(writer->progress, nr_stored);
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
	trace2_data_intmax("pack-bitmap-write", the_repository,
			   "building_bitmaps_pseudo_merge_reused",
			   reused_pseudo_merge_bitmaps_nr);

	stop_progress(&writer->progress);

	if (closed)
		compute_xor_offsets(writer);
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

void bitmap_writer_select_commits(struct bitmap_writer *writer,
				  struct commit **indexed_commits,
				  unsigned int indexed_commits_nr)
{
	unsigned int i = 0, j, next;

	QSORT(indexed_commits, indexed_commits_nr, date_compare);

	if (indexed_commits_nr < 100) {
		for (i = 0; i < indexed_commits_nr; ++i)
			bitmap_writer_push_commit(writer, indexed_commits[i], 0);
		return;
	}

	if (writer->show_progress)
		writer->progress = start_progress("Selecting bitmap commits", 0);

	for (;;) {
		struct commit *chosen = NULL;

		next = next_commit_index(i);

		if (i + next >= indexed_commits_nr)
			break;

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

		bitmap_writer_push_commit(writer, chosen, 0);

		i += next + 1;
		display_progress(writer->progress, i);
	}

	stop_progress(&writer->progress);

	select_pseudo_merges(writer, indexed_commits, indexed_commits_nr);
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

static void write_selected_commits_v1(struct bitmap_writer *writer,
				      struct hashfile *f, off_t *offsets)
{
	int i;

	for (i = 0; i < bitmap_writer_nr_selected_commits(writer); ++i) {
		struct bitmapped_commit *stored = &writer->selected[i];
		if (stored->pseudo_merge)
			BUG("unexpected pseudo-merge among selected: %s",
			    oid_to_hex(&stored->commit->object.oid));

		if (offsets)
			offsets[i] = hashfile_total(f);

		hashwrite_be32(f, stored->commit_pos);
		hashwrite_u8(f, stored->xor_offset);
		hashwrite_u8(f, stored->flags);

		dump_bitmap(f, stored->write_as);
	}
}

static void write_pseudo_merges(struct bitmap_writer *writer,
				struct hashfile *f)
{
	struct oid_array commits = OID_ARRAY_INIT;
	struct bitmap **commits_bitmap = NULL;
	off_t *pseudo_merge_ofs = NULL;
	off_t start, table_start, next_ext;

	uint32_t base = bitmap_writer_nr_selected_commits(writer);
	size_t i, j = 0;

	CALLOC_ARRAY(commits_bitmap, writer->pseudo_merges_nr);
	CALLOC_ARRAY(pseudo_merge_ofs, writer->pseudo_merges_nr);

	for (i = 0; i < writer->pseudo_merges_nr; i++) {
		struct bitmapped_commit *merge = &writer->selected[base + i];
		struct commit_list *p;

		if (!merge->pseudo_merge)
			BUG("found non-pseudo merge commit at %"PRIuMAX, (uintmax_t)i);

		commits_bitmap[i] = bitmap_new();

		for (p = merge->commit->parents; p; p = p->next)
			bitmap_set(commits_bitmap[i],
				   find_object_pos(writer, &p->item->object.oid,
						   NULL));
	}

	start = hashfile_total(f);

	for (i = 0; i < writer->pseudo_merges_nr; i++) {
		struct ewah_bitmap *commits_ewah = bitmap_to_ewah(commits_bitmap[i]);

		pseudo_merge_ofs[i] = hashfile_total(f);

		dump_bitmap(f, commits_ewah);
		dump_bitmap(f, writer->selected[base+i].write_as);

		ewah_free(commits_ewah);
	}

	next_ext = st_add(hashfile_total(f),
			  st_mult(kh_size(writer->pseudo_merge_commits),
				  sizeof(uint64_t)));

	table_start = hashfile_total(f);

	commits.alloc = kh_size(writer->pseudo_merge_commits);
	CALLOC_ARRAY(commits.oid, commits.alloc);

	for (i = kh_begin(writer->pseudo_merge_commits); i != kh_end(writer->pseudo_merge_commits); i++) {
		if (!kh_exist(writer->pseudo_merge_commits, i))
			continue;
		oid_array_append(&commits, &kh_key(writer->pseudo_merge_commits, i));
	}

	oid_array_sort(&commits);

	/* write lookup table (non-extended) */
	for (i = 0; i < commits.nr; i++) {
		int hash_pos;
		struct pseudo_merge_commit_idx *c;

		hash_pos = kh_get_oid_map(writer->pseudo_merge_commits,
					  commits.oid[i]);
		if (hash_pos == kh_end(writer->pseudo_merge_commits))
			BUG("could not find pseudo-merge commit %s",
			    oid_to_hex(&commits.oid[i]));

		c = kh_value(writer->pseudo_merge_commits, hash_pos);

		hashwrite_be32(f, find_object_pos(writer, &commits.oid[i],
						  NULL));
		if (c->nr == 1)
			hashwrite_be64(f, pseudo_merge_ofs[c->pseudo_merge[0]]);
		else if (c->nr > 1) {
			if (next_ext & ((uint64_t)1<<63))
				die(_("too many pseudo-merges"));
			hashwrite_be64(f, next_ext | ((uint64_t)1<<63));
			next_ext = st_add3(next_ext,
					   sizeof(uint32_t),
					   st_mult(c->nr, sizeof(uint64_t)));
		} else
			BUG("expected commit '%s' to have at least one "
			    "pseudo-merge", oid_to_hex(&commits.oid[i]));
	}

	/* write lookup table (extended) */
	for (i = 0; i < commits.nr; i++) {
		int hash_pos;
		struct pseudo_merge_commit_idx *c;

		hash_pos = kh_get_oid_map(writer->pseudo_merge_commits,
					  commits.oid[i]);
		if (hash_pos == kh_end(writer->pseudo_merge_commits))
			BUG("could not find pseudo-merge commit %s",
			    oid_to_hex(&commits.oid[i]));

		c = kh_value(writer->pseudo_merge_commits, hash_pos);
		if (c->nr == 1)
			continue;

		hashwrite_be32(f, c->nr);
		for (j = 0; j < c->nr; j++)
			hashwrite_be64(f, pseudo_merge_ofs[c->pseudo_merge[j]]);
	}

	/* write positions for all pseudo merges */
	for (i = 0; i < writer->pseudo_merges_nr; i++)
		hashwrite_be64(f, pseudo_merge_ofs[i]);

	hashwrite_be32(f, writer->pseudo_merges_nr);
	hashwrite_be32(f, kh_size(writer->pseudo_merge_commits));
	hashwrite_be64(f, table_start - start);
	hashwrite_be64(f, hashfile_total(f) - start + sizeof(uint64_t));

	for (i = 0; i < writer->pseudo_merges_nr; i++)
		bitmap_free(commits_bitmap[i]);

	free(pseudo_merge_ofs);
	free(commits_bitmap);
}

static int table_cmp(const void *_va, const void *_vb, void *_data)
{
	struct bitmap_writer *writer = _data;
	struct bitmapped_commit *a = &writer->selected[*(uint32_t *)_va];
	struct bitmapped_commit *b = &writer->selected[*(uint32_t *)_vb];

	if (a->commit_pos < b->commit_pos)
		return -1;
	else if (a->commit_pos > b->commit_pos)
		return 1;

	return 0;
}

static void write_lookup_table(struct bitmap_writer *writer, struct hashfile *f,
			       off_t *offsets)
{
	uint32_t i;
	uint32_t *table, *table_inv;

	ALLOC_ARRAY(table, bitmap_writer_nr_selected_commits(writer));
	ALLOC_ARRAY(table_inv, bitmap_writer_nr_selected_commits(writer));

	for (i = 0; i < bitmap_writer_nr_selected_commits(writer); i++)
		table[i] = i;

	/*
	 * At the end of this sort table[j] = i means that the i'th
	 * bitmap corresponds to j'th bitmapped commit (among the selected
	 * commits) in lex order of OIDs.
	 */
	QSORT_S(table, bitmap_writer_nr_selected_commits(writer), table_cmp, writer);

	/* table_inv helps us discover that relationship (i'th bitmap
	 * to j'th commit by j = table_inv[i])
	 */
	for (i = 0; i < bitmap_writer_nr_selected_commits(writer); i++)
		table_inv[table[i]] = i;

	trace2_region_enter("pack-bitmap-write", "writing_lookup_table", the_repository);
	for (i = 0; i < bitmap_writer_nr_selected_commits(writer); i++) {
		struct bitmapped_commit *selected = &writer->selected[table[i]];
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

		hashwrite_be32(f, writer->selected[table[i]].commit_pos);
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

void bitmap_writer_set_checksum(struct bitmap_writer *writer,
				const unsigned char *sha1)
{
	hashcpy(writer->pack_checksum, sha1, the_repository->hash_algo);
}

void bitmap_writer_finish(struct bitmap_writer *writer,
			  struct pack_idx_entry **index,
			  uint32_t index_nr,
			  const char *filename,
			  uint16_t options)
{
	static uint16_t default_version = 1;
	static uint16_t flags = BITMAP_OPT_FULL_DAG;
	struct strbuf tmp_file = STRBUF_INIT;
	struct hashfile *f;
	off_t *offsets = NULL;
	uint32_t i;

	struct bitmap_disk_header header;

	int fd = odb_mkstemp(&tmp_file, "pack/tmp_bitmap_XXXXXX");

	if (writer->pseudo_merges_nr)
		options |= BITMAP_OPT_PSEUDO_MERGES;

	f = hashfd(fd, tmp_file.buf);

	memcpy(header.magic, BITMAP_IDX_SIGNATURE, sizeof(BITMAP_IDX_SIGNATURE));
	header.version = htons(default_version);
	header.options = htons(flags | options);
	header.entry_count = htonl(bitmap_writer_nr_selected_commits(writer));
	hashcpy(header.checksum, writer->pack_checksum, the_repository->hash_algo);

	hashwrite(f, &header, sizeof(header) - GIT_MAX_RAWSZ + the_hash_algo->rawsz);
	dump_bitmap(f, writer->commits);
	dump_bitmap(f, writer->trees);
	dump_bitmap(f, writer->blobs);
	dump_bitmap(f, writer->tags);

	if (options & BITMAP_OPT_LOOKUP_TABLE)
		CALLOC_ARRAY(offsets, index_nr);

	for (i = 0; i < bitmap_writer_nr_selected_commits(writer); i++) {
		struct bitmapped_commit *stored = &writer->selected[i];
		int commit_pos = oid_pos(&stored->commit->object.oid, index,
					 index_nr, oid_access);

		if (commit_pos < 0)
			BUG(_("trying to write commit not in index"));
		stored->commit_pos = commit_pos;
	}

	write_selected_commits_v1(writer, f, offsets);

	if (options & BITMAP_OPT_PSEUDO_MERGES)
		write_pseudo_merges(writer, f);

	if (options & BITMAP_OPT_LOOKUP_TABLE)
		write_lookup_table(writer, f, offsets);

	if (options & BITMAP_OPT_HASH_CACHE)
		write_hash_cache(f, index, index_nr);

	finalize_hashfile(f, NULL, FSYNC_COMPONENT_PACK_METADATA,
			  CSUM_HASH_IN_STREAM | CSUM_FSYNC | CSUM_CLOSE);

	if (adjust_shared_perm(tmp_file.buf))
		die_errno("unable to make temporary bitmap file readable");

	if (rename(tmp_file.buf, filename))
		die_errno("unable to rename temporary bitmap file to '%s'", filename);

	strbuf_release(&tmp_file);
	free(offsets);
}
