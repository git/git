/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef RECORD_H
#define RECORD_H

#include "reftable.h"
#include "slice.h"

/* utilities for de/encoding varints */

int get_var_int(uint64_t *dest, struct slice in);
int put_var_int(struct slice dest, uint64_t val);

/* Methods for records. */
struct record_vtable {
	/* encode the key of to a byte slice. */
	void (*key)(const void *rec, struct slice *dest);

	/* The record type of ('r' for ref). */
	byte (*type)(void);

	void (*copy_from)(void *dest, const void *src, int hash_size);

	/* a value of [0..7], indicating record subvariants (eg. ref vs. symref
	 * vs ref deletion) */
	byte (*val_type)(const void *rec);

	/* encodes rec into dest, returning how much space was used. */
	int (*encode)(const void *rec, struct slice dest, int hash_size);

	/* decode data from `src` into the record. */
	int (*decode)(void *rec, struct slice key, byte extra, struct slice src,
		      int hash_size);

	/* deallocate and null the record. */
	void (*clear)(void *rec);
};

/* record is a generic wrapper for different types of records. */
struct record {
	void *data;
	struct record_vtable *ops;
};

int is_block_type(byte typ);

struct record new_record(byte typ);

extern struct record_vtable reftable_ref_record_vtable;

int encode_key(bool *restart, struct slice dest, struct slice prev_key,
	       struct slice key, byte extra);
int decode_key(struct slice *key, byte *extra, struct slice last_key,
	       struct slice in);

/* index_record are used internally to speed up lookups. */
struct index_record {
	uint64_t offset; /* Offset of block */
	struct slice last_key; /* Last key of the block. */
};

/* obj_record stores an object ID => ref mapping. */
struct obj_record {
	byte *hash_prefix; /* leading bytes of the object ID */
	int hash_prefix_len; /* number of leading bytes. Constant
			      * across a single table. */
	uint64_t *offsets; /* a vector of file offsets. */
	int offset_len;
};

/* see struct record_vtable */

void record_key(struct record rec, struct slice *dest);
byte record_type(struct record rec);
void record_copy_from(struct record rec, struct record src, int hash_size);
byte record_val_type(struct record rec);
int record_encode(struct record rec, struct slice dest, int hash_size);
int record_decode(struct record rec, struct slice key, byte extra,
		  struct slice src, int hash_size);
void record_clear(struct record rec);

/* clear out the record, yielding the record data that was encapsulated. */
void *record_yield(struct record *rec);

/* initialize generic records from concrete records. The generic record should
 * be zeroed out. */

void record_from_obj(struct record *rec, struct obj_record *objrec);
void record_from_index(struct record *rec, struct index_record *idxrec);
void record_from_ref(struct record *rec, struct reftable_ref_record *refrec);
void record_from_log(struct record *rec, struct reftable_log_record *logrec);
struct reftable_ref_record *record_as_ref(struct record ref);

/* for qsort. */
int reftable_ref_record_compare_name(const void *a, const void *b);

/* for qsort. */
int reftable_log_record_compare_key(const void *a, const void *b);

#endif
