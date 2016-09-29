#include "cache.h"
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

	khash_sha1 *bitmaps;
	khash_sha1 *reused;
	struct packing_data *to_pack;

	struct bitmapped_commit *selected;
	unsigned int selected_nr, selected_alloc;

	struct progress *progress;
	int show_progress;
	unsigned char pack_checksum[20];
};

static struct bitmap_writer writer;

void bitmap_writer_show_progress(int show)
{
	writer.show_progress = show;
}

/**
 * Build the initial type index for the packfile
 */
void bitmap_writer_build_type_index(struct pack_idx_entry **index,
				    uint32_t index_nr)
{
	uint32_t i;

	writer.commits = ewah_new();
	writer.trees = ewah_new();
	writer.blobs = ewah_new();
	writer.tags = ewah_new();

	for (i = 0; i < index_nr; ++i) {
		struct object_entry *entry = (struct object_entry *)index[i];
		enum object_type real_type;

		entry->in_pack_pos = i;

		switch (entry->type) {
		case OBJ_COMMIT:
		case OBJ_TREE:
		case OBJ_BLOB:
		case OBJ_TAG:
			real_type = entry->type;
			break;

		default:
			real_type = sha1_object_info(entry->idx.sha1, NULL);
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
			    sha1_to_hex(entry->idx.sha1), real_type, entry->type);
		}
	}
}

/**
 * Compute the actual bitmaps
 */
static struct object **seen_objects;
static unsigned int seen_objects_nr, seen_objects_alloc;

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

static inline void mark_as_seen(struct object *object)
{
	ALLOC_GROW(seen_objects, seen_objects_nr + 1, seen_objects_alloc);
	seen_objects[seen_objects_nr++] = object;
}

static inline void reset_all_seen(void)
{
	unsigned int i;
	for (i = 0; i < seen_objects_nr; ++i) {
		seen_objects[i]->flags &= ~(SEEN | ADDED | SHOWN);
	}
	seen_objects_nr = 0;
}

static uint32_t find_object_pos(const unsigned char *sha1)
{
	struct object_entry *entry = packlist_find(writer.to_pack, sha1, NULL);

	if (!entry) {
		die("Failed to write bitmap index. Packfile doesn't have full closure "
			"(object %s is missing)", sha1_to_hex(sha1));
	}

	return entry->in_pack_pos;
}

static void show_object(struct object *object, const char *name, void *data)
{
	struct bitmap *base = data;
	bitmap_set(base, find_object_pos(object->oid.hash));
	mark_as_seen(object);
}

static void show_commit(struct commit *commit, void *data)
{
	mark_as_seen((struct object *)commit);
}

static int
add_to_include_set(struct bitmap *base, struct commit *commit)
{
	khiter_t hash_pos;
	uint32_t bitmap_pos = find_object_pos(commit->object.oid.hash);

	if (bitmap_get(base, bitmap_pos))
		return 0;

	hash_pos = kh_get_sha1(writer.bitmaps, commit->object.oid.hash);
	if (hash_pos < kh_end(writer.bitmaps)) {
		struct bitmapped_commit *bc = kh_value(writer.bitmaps, hash_pos);
		bitmap_or_ewah(base, bc->bitmap);
		return 0;
	}

	bitmap_set(base, bitmap_pos);
	return 1;
}

static int
should_include(struct commit *commit, void *_data)
{
	struct bitmap *base = _data;

	if (!add_to_include_set(base, commit)) {
		struct commit_list *parent = commit->parents;

		mark_as_seen((struct object *)commit);

		while (parent) {
			parent->item->object.flags |= SEEN;
			mark_as_seen((struct object *)parent->item);
			parent = parent->next;
		}

		return 0;
	}

	return 1;
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

void bitmap_writer_build(struct packing_data *to_pack)
{
	static const double REUSE_BITMAP_THRESHOLD = 0.2;

	int i, reuse_after, need_reset;
	struct bitmap *base = bitmap_new();
	struct rev_info revs;

	writer.bitmaps = kh_init_sha1();
	writer.to_pack = to_pack;

	if (writer.show_progress)
		writer.progress = start_progress("Building bitmaps", writer.selected_nr);

	init_revisions(&revs, NULL);
	revs.tag_objects = 1;
	revs.tree_objects = 1;
	revs.blob_objects = 1;
	revs.no_walk = 0;

	revs.include_check = should_include;
	reset_revision_walk();

	reuse_after = writer.selected_nr * REUSE_BITMAP_THRESHOLD;
	need_reset = 0;

	for (i = writer.selected_nr - 1; i >= 0; --i) {
		struct bitmapped_commit *stored;
		struct object *object;

		khiter_t hash_pos;
		int hash_ret;

		stored = &writer.selected[i];
		object = (struct object *)stored->commit;

		if (stored->bitmap == NULL) {
			if (i < writer.selected_nr - 1 &&
			    (need_reset ||
			     !in_merge_bases(writer.selected[i + 1].commit,
					     stored->commit))) {
			    bitmap_reset(base);
			    reset_all_seen();
			}

			add_pending_object(&revs, object, "");
			revs.include_check_data = base;

			if (prepare_revision_walk(&revs))
				die("revision walk setup failed");

			traverse_commit_list(&revs, show_commit, show_object, base);

			revs.pending.nr = 0;
			revs.pending.alloc = 0;
			revs.pending.objects = NULL;

			stored->bitmap = bitmap_to_ewah(base);
			need_reset = 0;
		} else
			need_reset = 1;

		if (i >= reuse_after)
			stored->flags |= BITMAP_FLAG_REUSE;

		hash_pos = kh_put_sha1(writer.bitmaps, object->oid.hash, &hash_ret);
		if (hash_ret == 0)
			die("Duplicate entry when writing index: %s",
			    oid_to_hex(&object->oid));

		kh_value(writer.bitmaps, hash_pos) = stored;
		display_progress(writer.progress, writer.selected_nr - i);
	}

	bitmap_free(base);
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
	if (prepare_bitmap_git() < 0)
		return;

	writer.reused = kh_init_sha1();
	rebuild_existing_bitmaps(to_pack, writer.reused, writer.show_progress);
}

static struct ewah_bitmap *find_reused_bitmap(const unsigned char *sha1)
{
	khiter_t hash_pos;

	if (!writer.reused)
		return NULL;

	hash_pos = kh_get_sha1(writer.reused, sha1);
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
			reused_bitmap = find_reused_bitmap(chosen->object.oid.hash);
		} else {
			chosen = indexed_commits[i + next];

			for (j = 0; j <= next; ++j) {
				struct commit *cm = indexed_commits[i + j];

				reused_bitmap = find_reused_bitmap(cm->object.oid.hash);
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


static int sha1write_ewah_helper(void *f, const void *buf, size_t len)
{
	/* sha1write will die on error */
	sha1write(f, buf, len);
	return len;
}

/**
 * Write the bitmap index to disk
 */
static inline void dump_bitmap(struct sha1file *f, struct ewah_bitmap *bitmap)
{
	if (ewah_serialize_to(bitmap, sha1write_ewah_helper, f) < 0)
		die("Failed to write bitmap index");
}

static const unsigned char *sha1_access(size_t pos, void *table)
{
	struct pack_idx_entry **index = table;
	return index[pos]->sha1;
}

static void write_selected_commits_v1(struct sha1file *f,
				      struct pack_idx_entry **index,
				      uint32_t index_nr)
{
	int i;

	for (i = 0; i < writer.selected_nr; ++i) {
		struct bitmapped_commit *stored = &writer.selected[i];

		int commit_pos =
			sha1_pos(stored->commit->object.oid.hash, index, index_nr, sha1_access);

		if (commit_pos < 0)
			die("BUG: trying to write commit not in index");

		sha1write_be32(f, commit_pos);
		sha1write_u8(f, stored->xor_offset);
		sha1write_u8(f, stored->flags);

		dump_bitmap(f, stored->write_as);
	}
}

static void write_hash_cache(struct sha1file *f,
			     struct pack_idx_entry **index,
			     uint32_t index_nr)
{
	uint32_t i;

	for (i = 0; i < index_nr; ++i) {
		struct object_entry *entry = (struct object_entry *)index[i];
		uint32_t hash_value = htonl(entry->hash);
		sha1write(f, &hash_value, sizeof(hash_value));
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
	static char tmp_file[PATH_MAX];
	static uint16_t default_version = 1;
	static uint16_t flags = BITMAP_OPT_FULL_DAG;
	struct sha1file *f;

	struct bitmap_disk_header header;

	int fd = odb_mkstemp(tmp_file, sizeof(tmp_file), "pack/tmp_bitmap_XXXXXX");

	if (fd < 0)
		die_errno("unable to create '%s'", tmp_file);
	f = sha1fd(fd, tmp_file);

	memcpy(header.magic, BITMAP_IDX_SIGNATURE, sizeof(BITMAP_IDX_SIGNATURE));
	header.version = htons(default_version);
	header.options = htons(flags | options);
	header.entry_count = htonl(writer.selected_nr);
	hashcpy(header.checksum, writer.pack_checksum);

	sha1write(f, &header, sizeof(header));
	dump_bitmap(f, writer.commits);
	dump_bitmap(f, writer.trees);
	dump_bitmap(f, writer.blobs);
	dump_bitmap(f, writer.tags);
	write_selected_commits_v1(f, index, index_nr);

	if (options & BITMAP_OPT_HASH_CACHE)
		write_hash_cache(f, index, index_nr);

	sha1close(f, NULL, CSUM_FSYNC);

	if (adjust_shared_perm(tmp_file))
		die_errno("unable to make temporary bitmap file readable");

	if (rename(tmp_file, filename))
		die_errno("unable to rename temporary bitmap file to '%s'", filename);
}
