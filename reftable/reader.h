/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef READER_H
#define READER_H

#include "block.h"
#include "record.h"
#include "reftable.h"

uint64_t block_source_size(struct block_source source);

int block_source_read_block(struct block_source source, struct block *dest,
			    uint64_t off, uint32_t size);
void block_source_return_block(struct block_source source, struct block *ret);
void block_source_close(struct block_source *source);

struct reader_offsets {
	bool present;
	uint64_t offset;
	uint64_t index_offset;
};

struct reader {
	struct block_source source;
	char *name;
	int hash_size;
	uint64_t size;
	uint32_t block_size;
	uint64_t min_update_index;
	uint64_t max_update_index;
	int object_id_len;

	struct reader_offsets ref_offsets;
	struct reader_offsets obj_offsets;
	struct reader_offsets log_offsets;
};

int init_reader(struct reader *r, struct block_source source, const char *name);
int reader_seek(struct reader *r, struct iterator *it, struct record rec);
void reader_close(struct reader *r);
const char *reader_name(struct reader *r);
void reader_return_block(struct reader *r, struct block *p);
int reader_init_block_reader(struct reader *r, struct block_reader *br,
			     uint64_t next_off, byte want_typ);

#endif
