/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef RECORD_H
#define RECORD_H

#include "basics.h"
#include "system.h"

#include <stdint.h>

#include "reftable-record.h"

/*
 * A substring of existing string data. This structure takes no responsibility
 * for the lifetime of the data it points to.
 */
struct string_view {
	uint8_t *buf;
	size_t len;
};

/* Advance `s.buf` by `n`, and decrease length. */
static inline void string_view_consume(struct string_view *s, int n)
{
	s->buf += n;
	s->len -= n;
}

/*
 * Decode and encode a varint. Returns the number of bytes read/written, or a
 * negative value in case encoding/decoding the varint has failed.
 */
int get_var_int(uint64_t *dest, struct string_view *in);
int put_var_int(struct string_view *dest, uint64_t val);

/* Methods for records. */
struct reftable_record_vtable {
	/* encode the key of to a uint8_t reftable_buf. */
	int (*key)(const void *rec, struct reftable_buf *dest);

	/* The record type of ('r' for ref). */
	uint8_t type;

	int (*copy_from)(void *dest, const void *src, uint32_t hash_size);

	/* a value of [0..7], indicating record subvariants (eg. ref vs. symref
	 * vs ref deletion) */
	uint8_t (*val_type)(const void *rec);

	/* encodes rec into dest, returning how much space was used. */
	int (*encode)(const void *rec, struct string_view dest, uint32_t hash_size);

	/* decode data from `src` into the record. */
	int (*decode)(void *rec, struct reftable_buf key, uint8_t extra,
		      struct string_view src, uint32_t hash_size,
		      struct reftable_buf *scratch);

	/* deallocate and null the record. */
	void (*release)(void *rec);

	/* is this a tombstone? */
	int (*is_deletion)(const void *rec);

	/* Are two records equal? This assumes they have the same type. Returns 0 for non-equal. */
	int (*equal)(const void *a, const void *b, uint32_t hash_size);

	/*
	 * Compare keys of two records with each other. The records must have
	 * the same type.
	 */
	int (*cmp)(const void *a, const void *b);
};

/* returns true for recognized block types. Block start with the block type. */
int reftable_is_block_type(uint8_t typ);

/* Encode `key` into `dest`. Sets `is_restart` to indicate a restart. Returns
 * number of bytes written. */
int reftable_encode_key(int *is_restart, struct string_view dest,
			struct reftable_buf prev_key, struct reftable_buf key,
			uint8_t extra);

/* Decode a record's key lengths. */
int reftable_decode_keylen(struct string_view in,
			   uint64_t *prefix_len,
			   uint64_t *suffix_len,
			   uint8_t *extra);

/*
 * Decode into `last_key` and `extra` from `in`. `last_key` is expected to
 * contain the decoded key of the preceding record, if any.
 */
int reftable_decode_key(struct reftable_buf *last_key, uint8_t *extra,
			struct string_view in);

/* reftable_index_record are used internally to speed up lookups. */
struct reftable_index_record {
	uint64_t offset; /* Offset of block */
	struct reftable_buf last_key; /* Last key of the block. */
};

/* reftable_obj_record stores an object ID => ref mapping. */
struct reftable_obj_record {
	uint8_t *hash_prefix; /* leading bytes of the object ID */
	int hash_prefix_len; /* number of leading bytes. Constant
			      * across a single table. */
	uint64_t *offsets; /* a vector of file offsets. */
	int offset_len;
};

/* record is a generic wrapper for different types of records. It is normally
 * created on the stack, or embedded within another struct. If the type is
 * known, a fresh instance can be initialized explicitly. Otherwise, use
 * `reftable_record_init()` to initialize generically (as the index_record is
 * not valid as 0-initialized structure)
 */
struct reftable_record {
	uint8_t type;
	union {
		struct reftable_ref_record ref;
		struct reftable_log_record log;
		struct reftable_obj_record obj;
		struct reftable_index_record idx;
	} u;
};

/* Initialize the reftable record for the given type */
void reftable_record_init(struct reftable_record *rec, uint8_t typ);

/* see struct record_vtable */
int reftable_record_cmp(struct reftable_record *a, struct reftable_record *b);
int reftable_record_equal(struct reftable_record *a, struct reftable_record *b, uint32_t hash_size);
int reftable_record_key(struct reftable_record *rec, struct reftable_buf *dest);
int reftable_record_copy_from(struct reftable_record *rec,
			      struct reftable_record *src, uint32_t hash_size);
uint8_t reftable_record_val_type(struct reftable_record *rec);
int reftable_record_encode(struct reftable_record *rec, struct string_view dest,
			   uint32_t hash_size);
int reftable_record_decode(struct reftable_record *rec, struct reftable_buf key,
			   uint8_t extra, struct string_view src,
			   uint32_t hash_size, struct reftable_buf *scratch);
int reftable_record_is_deletion(struct reftable_record *rec);

static inline uint8_t reftable_record_type(struct reftable_record *rec)
{
	return rec->type;
}

/* frees and zeroes out the embedded record */
void reftable_record_release(struct reftable_record *rec);

/* for qsort. */
int reftable_ref_record_compare_name(const void *a, const void *b);

/* for qsort. */
int reftable_log_record_compare_key(const void *a, const void *b);

#endif
