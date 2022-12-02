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
struct bitmap_index *prepare_bitmap_walk(struct rev_info *revs,
					 int filter_provided_objects);
uint32_t midx_preferred_pack(struct bitmap_index *bitmap_git);
int reuse_partial_packfile_from_bitmap(struct bitmap_index *,
				       struct packed_git **packfile,
				       uint32_t *entries,
				       struct bitmap **reuse_out);
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

void bitmap_writer_show_progress(int show);
void bitmap_writer_set_checksum(const unsigned char *sha1);
void bitmap_writer_build_type_index(struct packing_data *to_pack,
				    struct pack_idx_entry **index,
				    uint32_t index_nr);
uint32_t *create_bitmap_mapping(struct bitmap_index *bitmap_git,
				struct packing_data *mapping);
int rebuild_bitmap(const uint32_t *reposition,
		   struct ewah_bitmap *source,
		   struct bitmap *dest);
struct ewah_bitmap *bitmap_for_commit(struct bitmap_index *bitmap_git,
				      struct commit *commit);
void bitmap_writer_select_commits(struct commit **indexed_commits,
		unsigned int indexed_commits_nr, int max_bitmaps);
int bitmap_writer_build(struct packing_data *to_pack);
void bitmap_writer_finish(struct pack_idx_entry **index,
			  uint32_t index_nr,
			  const char *filename,
			  uint16_t options);
char *midx_bitmap_filename(struct multi_pack_index *midx);
char *pack_bitmap_filename(struct packed_git *p);

int bitmap_is_midx(struct bitmap_index *bitmap_git);

const struct string_list *bitmap_preferred_tips(struct repository *r);
int bitmap_is_preferred_refname(struct repository *r, const char *refname);

#endif
