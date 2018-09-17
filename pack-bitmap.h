#ifndef PACK_BITMAP_H
#define PACK_BITMAP_H

#include "ewah/ewok.h"
#include "khash.h"
#include "pack-objects.h"

struct commit;
struct rev_info;

struct bitmap_disk_header {
	char magic[4];
	uint16_t version;
	uint16_t options;
	uint32_t entry_count;
	unsigned char checksum[20];
};

static const char BITMAP_IDX_SIGNATURE[] = {'B', 'I', 'T', 'M'};

#define NEEDS_BITMAP (1u<<22)

enum pack_bitmap_opts {
	BITMAP_OPT_FULL_DAG = 1,
	BITMAP_OPT_HASH_CACHE = 4,
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

struct bitmap_index *prepare_bitmap_git(void);
void count_bitmap_commit_list(struct bitmap_index *, uint32_t *commits,
			      uint32_t *trees, uint32_t *blobs, uint32_t *tags);
void traverse_bitmap_commit_list(struct bitmap_index *,
				 show_reachable_fn show_reachable);
void test_bitmap_walk(struct rev_info *revs);
struct bitmap_index *prepare_bitmap_walk(struct rev_info *revs);
int reuse_partial_packfile_from_bitmap(struct bitmap_index *,
				       struct packed_git **packfile,
				       uint32_t *entries, off_t *up_to);
int rebuild_existing_bitmaps(struct bitmap_index *, struct packing_data *mapping,
			     khash_sha1 *reused_bitmaps, int show_progress);
void free_bitmap_index(struct bitmap_index *);

/*
 * After a traversal has been performed by prepare_bitmap_walk(), this can be
 * queried to see if a particular object was reachable from any of the
 * objects flagged as UNINTERESTING.
 */
int bitmap_has_sha1_in_uninteresting(struct bitmap_index *, const unsigned char *sha1);

void bitmap_writer_show_progress(int show);
void bitmap_writer_set_checksum(unsigned char *sha1);
void bitmap_writer_build_type_index(struct packing_data *to_pack,
				    struct pack_idx_entry **index,
				    uint32_t index_nr);
void bitmap_writer_reuse_bitmaps(struct packing_data *to_pack);
void bitmap_writer_select_commits(struct commit **indexed_commits,
		unsigned int indexed_commits_nr, int max_bitmaps);
void bitmap_writer_build(struct packing_data *to_pack);
void bitmap_writer_finish(struct pack_idx_entry **index,
			  uint32_t index_nr,
			  const char *filename,
			  uint16_t options);

#endif
