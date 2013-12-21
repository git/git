#ifndef PACK_BITMAP_H
#define PACK_BITMAP_H

#include "ewah/ewok.h"
#include "khash.h"

struct bitmap_disk_entry {
	uint32_t object_pos;
	uint8_t xor_offset;
	uint8_t flags;
} __attribute__((packed));

struct bitmap_disk_header {
	char magic[4];
	uint16_t version;
	uint16_t options;
	uint32_t entry_count;
	unsigned char checksum[20];
};

static const char BITMAP_IDX_SIGNATURE[] = {'B', 'I', 'T', 'M'};

enum pack_bitmap_opts {
	BITMAP_OPT_FULL_DAG = 1
};

typedef int (*show_reachable_fn)(
	const unsigned char *sha1,
	enum object_type type,
	int flags,
	uint32_t hash,
	struct packed_git *found_pack,
	off_t found_offset);

int prepare_bitmap_git(void);
void count_bitmap_commit_list(uint32_t *commits, uint32_t *trees, uint32_t *blobs, uint32_t *tags);
void traverse_bitmap_commit_list(show_reachable_fn show_reachable);
void test_bitmap_walk(struct rev_info *revs);
char *pack_bitmap_filename(struct packed_git *p);
int prepare_bitmap_walk(struct rev_info *revs);
int reuse_partial_packfile_from_bitmap(struct packed_git **packfile, uint32_t *entries, off_t *up_to);

#endif
