#ifndef PACK_BITMAP_H
#define PACK_BITMAP_H

#include "ewah/ewok.h"
#include "khash.h"
#include "pack.h"
#include "pack-objects.h"
#include "string-list.h"

struct commit;
struct repository;
struct rev_info;

static const char BITMAP_IDX_SIGNATURE[] = {'B', 'I', 'T', 'M'};

struct bitmap_disk_header {
	char magic[ARRAY_SIZE(BITMAP_IDX_SIGNATURE)];
	uint16_t version;
	uint16_t options;
	uint32_t entry_count;
	unsigned char checksum[GIT_MAX_RAWSZ];
};

#define BITMAP_PSEUDO_MERGE (1u<<21)
#define NEEDS_BITMAP (1u<<22)

/*
 * The width in bytes of a single triplet in the lookup table
 * extension:
 *     (commit_pos, offset, xor_row)
 *
 * whose fields ar 32-, 64-, 32- bits wide, respectively.
 */
#define BITMAP_LOOKUP_TABLE_TRIPLET_WIDTH (16)

enum pack_bitmap_opts {
	BITMAP_OPT_FULL_DAG = 0x1,
	BITMAP_OPT_HASH_CACHE = 0x4,
	BITMAP_OPT_LOOKUP_TABLE = 0x10,
	BITMAP_OPT_PSEUDO_MERGES = 0x20,
};

enum pack_bitmap_flags {
	BITMAP_FLAG_REUSE = 0x1
};

typedef int (*show_reachable_fn)(
	const struct object_id *oid,
	enum object_type type,
	int flags,
	uint32_t hash,
	struct packed_git *found_pack,
	off_t found_offset);

struct bitmap_index;

struct bitmapped_pack {
	struct packed_git *p;

	uint32_t bitmap_pos;
	uint32_t bitmap_nr;

	struct multi_pack_index *from_midx; /* MIDX only */
	uint32_t pack_int_id; /* MIDX only */
};

struct bitmap_index *prepare_bitmap_git(struct repository *r);
struct bitmap_index *prepare_midx_bitmap_git(struct multi_pack_index *midx);
void count_bitmap_commit_list(struct bitmap_index *, uint32_t *commits,
			      uint32_t *trees, uint32_t *blobs, uint32_t *tags);
void traverse_bitmap_commit_list(struct bitmap_index *,
				 struct rev_info *revs,
				 show_reachable_fn show_reachable);
void test_bitmap_walk(struct rev_info *revs);
int test_bitmap_commits(struct repository *r);
int test_bitmap_hashes(struct repository *r);
int test_bitmap_pseudo_merges(struct repository *r);
int test_bitmap_pseudo_merge_commits(struct repository *r, uint32_t n);
int test_bitmap_pseudo_merge_objects(struct repository *r, uint32_t n);

#define GIT_TEST_PACK_USE_BITMAP_BOUNDARY_TRAVERSAL \
	"GIT_TEST_PACK_USE_BITMAP_BOUNDARY_TRAVERSAL"

struct bitmap_index *prepare_bitmap_walk(struct rev_info *revs,
					 int filter_provided_objects);
void reuse_partial_packfile_from_bitmap(struct bitmap_index *bitmap_git,
					struct bitmapped_pack **packs_out,
					size_t *packs_nr_out,
					struct bitmap **reuse_out,
					int multi_pack_reuse);
int rebuild_existing_bitmaps(struct bitmap_index *, struct packing_data *mapping,
			     kh_oid_map_t *reused_bitmaps, int show_progress);
void free_bitmap_index(struct bitmap_index *);
int bitmap_walk_contains(struct bitmap_index *,
			 struct bitmap *bitmap, const struct object_id *oid);

/*
 * After a traversal has been performed by prepare_bitmap_walk(), this can be
 * queried to see if a particular object was reachable from any of the
 * objects flagged as UNINTERESTING.
 */
int bitmap_has_oid_in_uninteresting(struct bitmap_index *, const struct object_id *oid);

off_t get_disk_usage_from_bitmap(struct bitmap_index *, struct rev_info *);

struct bitmap_writer {
	struct ewah_bitmap *commits;
	struct ewah_bitmap *trees;
	struct ewah_bitmap *blobs;
	struct ewah_bitmap *tags;

	kh_oid_map_t *bitmaps;
	struct packing_data *to_pack;

	struct bitmapped_commit *selected;
	unsigned int selected_nr, selected_alloc;

	struct string_list pseudo_merge_groups;
	kh_oid_map_t *pseudo_merge_commits; /* oid -> pseudo merge(s) */
	uint32_t pseudo_merges_nr;

	struct progress *progress;
	int show_progress;
	unsigned char pack_checksum[GIT_MAX_RAWSZ];
};

void bitmap_writer_init(struct bitmap_writer *writer, struct repository *r,
			struct packing_data *pdata);
void bitmap_writer_show_progress(struct bitmap_writer *writer, int show);
void bitmap_writer_set_checksum(struct bitmap_writer *writer,
				const unsigned char *sha1);
void bitmap_writer_build_type_index(struct bitmap_writer *writer,
				    struct pack_idx_entry **index);
int bitmap_writer_has_bitmapped_object_id(struct bitmap_writer *writer,
					  const struct object_id *oid);
void bitmap_writer_push_commit(struct bitmap_writer *writer,
			       struct commit *commit, unsigned pseudo_merge);
uint32_t *create_bitmap_mapping(struct bitmap_index *bitmap_git,
				struct packing_data *mapping);
int rebuild_bitmap(const uint32_t *reposition,
		   struct ewah_bitmap *source,
		   struct bitmap *dest);
struct ewah_bitmap *bitmap_for_commit(struct bitmap_index *bitmap_git,
				      struct commit *commit);
struct ewah_bitmap *pseudo_merge_bitmap_for_commit(struct bitmap_index *bitmap_git,
						   struct commit *commit);
void bitmap_writer_select_commits(struct bitmap_writer *writer,
				  struct commit **indexed_commits,
				  unsigned int indexed_commits_nr);
int bitmap_writer_build(struct bitmap_writer *writer);
void bitmap_writer_finish(struct bitmap_writer *writer,
			  struct pack_idx_entry **index,
			  const char *filename,
			  uint16_t options);
void bitmap_writer_free(struct bitmap_writer *writer);
char *midx_bitmap_filename(struct multi_pack_index *midx);
char *pack_bitmap_filename(struct packed_git *p);

int bitmap_is_midx(struct bitmap_index *bitmap_git);

const struct string_list *bitmap_preferred_tips(struct repository *r);
int bitmap_is_preferred_refname(struct repository *r, const char *refname);

int verify_bitmap_files(struct repository *r);

struct ewah_bitmap *read_bitmap(const unsigned char *map,
				size_t map_size, size_t *map_pos);
#endif
