#include "cache.h"
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
#include "sha1-lookup.h"
#include "pack-objects.h"
#include "commit-reach.h"
#include "prio-queue.h"

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
	kh_oid_map_t *reused;
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
 * Build the initial type index for the packfile
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

static inline void push_bitmapped_commit(struct commit *commit, struct ewah_bitmap *reused)
{
	if (writer.selected_nr >= writer.selected_alloc) {
		writer.selected_alloc = (writer.selected_alloc + 32) * 2;
		REALLOC_ARRAY(writer.selected, writer.selected_alloc);
	}

	writer.selected[writer.selected_nr].commit = commit;
	writer.selected[writer.selected_nr].bitmap = reused;
	writer.selected[writer.selected_nr].flags = 0;

	writer.selected_nr++;
}

static uint32_t find_object_pos(const struct object_id *oid)
{
	struct object_entry *entry = packlist_find(writer.to_pack, oid);

	if (!entry) {
		die("Failed to write bitmap index. Packfile doesn't have full closure "
			"(object %s is missing)", oid_to_hex(oid));
	}

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
				struct bitmap_writer *writer)
{
	struct rev_info revs;
	struct commit *commit;
	unsigned int i, num_maximal;

	memset(bb, 0, sizeof(*bb));
	init_bb_data(&bb->data);

	reset_revision_walk();
	repo_init_revisions(writer->to_pack->repo, &revs, NULL);
	revs.topo_order = 1;

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
	num_maximal = writer->selected_nr;

	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");

	while ((commit = get_revision(&revs))) {
		struct commit_list *p;
		struct bb_commit *c_ent;

		parse_commit_or_die(commit);

		c_ent = bb_data_at(&bb->data, commit);

		if (c_ent->maximal) {
			if (!c_ent->selected) {
				bitmap_set(c_ent->commit_mask, num_maximal);
				num_maximal++;
			}

			ALLOC_GROW(bb->commits, bb->commits_nr + 1, bb->commits_alloc);
			bb->commits[bb->commits_nr++] = commit;
		}

		for (p = commit->parents; p; p = p->next) {
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

		bitmap_free(c_ent->commit_mask);
		c_ent->commit_mask = NULL;
	}

	trace2_data_intmax("pack-bitmap-write", the_repository,
			   "num_selected_commits", writer->selected_nr);
	trace2_data_intmax("pack-bitmap-write", the_repository,
			   "num_maximal_commits", num_maximal);
}

static void bitmap_builder_clear(struct bitmap_builder *bb)
{
	clear_bb_data(&bb->data);
	free(bb->commits);
	bb->commits_nr = bb->commits_alloc = 0;
}

static void fill_bitmap_tree(struct bitmap *bitmap,
			     struct tree *tree)
{
	uint32_t pos;
	struct tree_desc desc;
	struct name_entry entry;

	/*
	 * If our bit is already set, then there is nothing to do. Both this
	 * tree and all of its children will be set.
	 */
	pos = find_object_pos(&tree->object.oid);
	if (bitmap_get(bitmap, pos))
		return;
	bitmap_set(bitmap, pos);

	if (parse_tree(tree) < 0)
		die("unable to load tree object %s",
		    oid_to_hex(&tree->object.oid));
	init_tree_desc(&desc, tree->buffer, tree->size);

	while (tree_entry(&desc, &entry)) {
		switch (object_type(entry.mode)) {
		case OBJ_TREE:
			fill_bitmap_tree(bitmap,
					 lookup_tree(the_repository, &entry.oid));
			break;
		case OBJ_BLOB:
			bitmap_set(bitmap, find_object_pos(&entry.oid));
			break;
		default:
			/* Gitlink, etc; not reachable */
			break;
		}
	}

	free_tree_buffer(tree);
}

static void fill_bitmap_commit(struct bb_commit *ent,
			       struct commit *commit,
			       struct prio_queue *queue)
{
	if (!ent->bitmap)
		ent->bitmap = bitmap_new();

	bitmap_set(ent->bitmap, find_object_pos(&commit->object.oid));
	prio_queue_put(queue, commit);

	while (queue->nr) {
		struct commit_list *p;
		struct commit *c = prio_queue_get(queue);

		bitmap_set(ent->bitmap, find_object_pos(&c->object.oid));
		fill_bitmap_tree(ent->bitmap, get_commit_tree(c));

		for (p = c->parents; p; p = p->next) {
			int pos = find_object_pos(&p->item->object.oid);
			if (!bitmap_get(ent->bitmap, pos)) {
				bitmap_set(ent->bitmap, pos);
				prio_queue_put(queue, p->item);
			}
		}
	}
}

static void store_selected(struct bb_commit *ent, struct commit *commit)
{
	struct bitmapped_commit *stored = &writer.selected[ent->idx];
	khiter_t hash_pos;
	int hash_ret;

	/*
	 * the "reuse bitmaps" phase may have stored something here, but
	 * our new algorithm doesn't use it. Drop it.
	 */
	if (stored->bitmap)
		ewah_free(stored->bitmap);

	stored->bitmap = bitmap_to_ewah(ent->bitmap);

	hash_pos = kh_put_oid_map(writer.bitmaps, commit->object.oid, &hash_ret);
	if (hash_ret == 0)
		die("Duplicate entry when writing index: %s",
		    oid_to_hex(&commit->object.oid));
	kh_value(writer.bitmaps, hash_pos) = stored;
}

void bitmap_writer_build(struct packing_data *to_pack)
{
	struct bitmap_builder bb;
	size_t i;
	int nr_stored = 0; /* for progress */
	struct prio_queue queue = { compare_commits_by_gen_then_commit_date };

	writer.bitmaps = kh_init_oid_map();
	writer.to_pack = to_pack;

	if (writer.show_progress)
		writer.progress = start_progress("Building bitmaps", writer.selected_nr);
	trace2_region_enter("pack-bitmap-write", "building_bitmaps_total",
			    the_repository);

	bitmap_builder_init(&bb, &writer);
	for (i = bb.commits_nr; i > 0; i--) {
		struct commit *commit = bb.commits[i-1];
		struct bb_commit *ent = bb_data_at(&bb.data, commit);
		struct commit *child;
		int reused = 0;

		fill_bitmap_commit(ent, commit, &queue);

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
	bitmap_builder_clear(&bb);

	trace2_region_leave("pack-bitmap-write", "building_bitmaps_total",
			    the_repository);

	stop_progress(&writer.progress);

	compute_xor_offsets();
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

void bitmap_writer_reuse_bitmaps(struct packing_data *to_pack)
{
	struct bitmap_index *bitmap_git;
	if (!(bitmap_git = prepare_bitmap_git(to_pack->repo)))
		return;

	writer.reused = kh_init_oid_map();
	rebuild_existing_bitmaps(bitmap_git, to_pack, writer.reused,
				 writer.show_progress);
	/*
	 * NEEDSWORK: rebuild_existing_bitmaps() makes writer.reused reference
	 * some bitmaps in bitmap_git, so we can't free the latter.
	 */
}

static struct ewah_bitmap *find_reused_bitmap(const struct object_id *oid)
{
	khiter_t hash_pos;

	if (!writer.reused)
		return NULL;

	hash_pos = kh_get_oid_map(writer.reused, *oid);
	if (hash_pos >= kh_end(writer.reused))
		return NULL;

	return kh_value(writer.reused, hash_pos);
}

void bitmap_writer_select_commits(struct commit **indexed_commits,
				  unsigned int indexed_commits_nr,
				  int max_bitmaps)
{
	unsigned int i = 0, j, next;

	QSORT(indexed_commits, indexed_commits_nr, date_compare);

	if (writer.show_progress)
		writer.progress = start_progress("Selecting bitmap commits", 0);

	if (indexed_commits_nr < 100) {
		for (i = 0; i < indexed_commits_nr; ++i)
			push_bitmapped_commit(indexed_commits[i], NULL);
		return;
	}

	for (;;) {
		struct ewah_bitmap *reused_bitmap = NULL;
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
			reused_bitmap = find_reused_bitmap(&chosen->object.oid);
		} else {
			chosen = indexed_commits[i + next];

			for (j = 0; j <= next; ++j) {
				struct commit *cm = indexed_commits[i + j];

				reused_bitmap = find_reused_bitmap(&cm->object.oid);
				if (reused_bitmap || (cm->object.flags & NEEDS_BITMAP) != 0) {
					chosen = cm;
					break;
				}

				if (cm->parents && cm->parents->next)
					chosen = cm;
			}
		}

		push_bitmapped_commit(chosen, reused_bitmap);

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

static const unsigned char *sha1_access(size_t pos, void *table)
{
	struct pack_idx_entry **index = table;
	return index[pos]->oid.hash;
}

static void write_selected_commits_v1(struct hashfile *f,
				      struct pack_idx_entry **index,
				      uint32_t index_nr)
{
	int i;

	for (i = 0; i < writer.selected_nr; ++i) {
		struct bitmapped_commit *stored = &writer.selected[i];

		int commit_pos =
			sha1_pos(stored->commit->object.oid.hash, index, index_nr, sha1_access);

		if (commit_pos < 0)
			BUG("trying to write commit not in index");

		hashwrite_be32(f, commit_pos);
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
	dump_bitmap(f, writer.commits);
	dump_bitmap(f, writer.trees);
	dump_bitmap(f, writer.blobs);
	dump_bitmap(f, writer.tags);
	write_selected_commits_v1(f, index, index_nr);

	if (options & BITMAP_OPT_HASH_CACHE)
		write_hash_cache(f, index, index_nr);

	finalize_hashfile(f, NULL, CSUM_HASH_IN_STREAM | CSUM_FSYNC | CSUM_CLOSE);

	if (adjust_shared_perm(tmp_file.buf))
		die_errno("unable to make temporary bitmap file readable");

	if (rename(tmp_file.buf, filename))
		die_errno("unable to rename temporary bitmap file to '%s'", filename);

	strbuf_release(&tmp_file);
}
