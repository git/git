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
#include "reftable-iterator.h"
#include "reftable-reader.h"

uint64_t block_source_size(struct reftable_block_source *source);

int block_source_read_block(struct reftable_block_source *source,
			    struct reftable_block *dest, uint64_t off,
			    uint32_t size);
void block_source_close(struct reftable_block_source *source);

/* metadata for a block type */
struct reftable_reader_offsets {
	int is_present;
	uint64_t offset;
	uint64_t index_offset;
};

/* The state for reading a reftable file. */
struct reftable_reader {
	/* for convience, associate a name with the instance. */
	char *name;
	struct reftable_block_source source;

	/* Size of the file, excluding the footer. */
	uint64_t size;

	/* 'sha1' for SHA1, 's256' for SHA-256 */
	uint32_t hash_id;

	uint32_t block_size;
	uint64_t min_update_index;
	uint64_t max_update_index;
	/* Length of the OID keys in the 'o' section */
	int object_id_len;
	int version;

	struct reftable_reader_offsets ref_offsets;
	struct reftable_reader_offsets obj_offsets;
	struct reftable_reader_offsets log_offsets;
};

int init_reader(struct reftable_reader *r, struct reftable_block_source *source,
		const char *name);
int reader_seek(struct reftable_reader *r, struct reftable_iterator *it,
		struct reftable_record *rec);
void reader_close(struct reftable_reader *r);
const char *reader_name(struct reftable_reader *r);

/* initialize a block reader to read from `r` */
int reader_init_block_reader(struct reftable_reader *r, struct block_reader *br,
			     uint64_t next_off, uint8_t want_typ);

#endif
