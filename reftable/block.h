/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef BLOCK_H
#define BLOCK_H

#include "basics.h"
#include "record.h"
#include "reftable.h"

struct block_writer {
	byte *buf;
	uint32_t block_size;
	uint32_t header_off;
	int restart_interval;
	int hash_size;

	uint32_t next;
	uint32_t *restarts;
	uint32_t restart_len;
	uint32_t restart_cap;
	struct slice last_key;
	int entries;
};

void block_writer_init(struct block_writer *bw, byte typ, byte *buf,
		       uint32_t block_size, uint32_t header_off, int hash_size);
byte block_writer_type(struct block_writer *bw);
int block_writer_add(struct block_writer *w, struct record rec);
int block_writer_finish(struct block_writer *w);
void block_writer_reset(struct block_writer *bw);
void block_writer_clear(struct block_writer *bw);

struct block_reader {
	uint32_t header_off;
	struct block block;
	int hash_size;

	/* size of the data, excluding restart data. */
	uint32_t block_len;
	byte *restart_bytes;
	uint32_t full_block_size;
	uint16_t restart_count;
};

struct block_iter {
	struct block_reader *br;
	struct slice last_key;
	uint32_t next_off;
};

int block_reader_init(struct block_reader *br, struct block *bl,
		      uint32_t header_off, uint32_t table_block_size,
		      int hash_size);
void block_reader_start(struct block_reader *br, struct block_iter *it);
int block_reader_seek(struct block_reader *br, struct block_iter *it,
		      struct slice want);
byte block_reader_type(struct block_reader *r);
int block_reader_first_key(struct block_reader *br, struct slice *key);

void block_iter_copy_from(struct block_iter *dest, struct block_iter *src);
int block_iter_next(struct block_iter *it, struct record rec);
int block_iter_seek(struct block_iter *it, struct slice want);
void block_iter_close(struct block_iter *it);

#endif
