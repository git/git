/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_RECORD_H
#define REFTABLE_RECORD_H

#include <stdint.h>

/*
 * Basic data types
 *
 * Reftables store the state of each ref in struct reftable_ref_record, and they
 * store a sequence of reflog updates in struct reftable_log_record.
 */

/* reftable_ref_record holds a ref database entry target_value */
struct reftable_ref_record {
	char *refname; /* Name of the ref, malloced. */
	uint64_t update_index; /* Logical timestamp at which this value is
				* written */

	enum {
		/* tombstone to hide deletions from earlier tables */
		REFTABLE_REF_DELETION = 0x0,

		/* a simple ref */
		REFTABLE_REF_VAL1 = 0x1,
		/* a tag, plus its peeled hash */
		REFTABLE_REF_VAL2 = 0x2,

		/* a symbolic reference */
		REFTABLE_REF_SYMREF = 0x3,
#define REFTABLE_NR_REF_VALUETYPES 4
	} value_type;
	union {
		uint8_t *val1; /* malloced hash. */
		struct {
			uint8_t *value; /* first value, malloced hash  */
			uint8_t *target_value; /* second value, malloced hash */
		} val2;
		char *symref; /* referent, malloced 0-terminated string */
	} value;
};

/* Returns the first hash, or NULL if `rec` is not of type
 * REFTABLE_REF_VAL1 or REFTABLE_REF_VAL2. */
uint8_t *reftable_ref_record_val1(const struct reftable_ref_record *rec);

/* Returns the second hash, or NULL if `rec` is not of type
 * REFTABLE_REF_VAL2. */
uint8_t *reftable_ref_record_val2(const struct reftable_ref_record *rec);

/* returns whether 'ref' represents a deletion */
int reftable_ref_record_is_deletion(const struct reftable_ref_record *ref);

/* prints a reftable_ref_record onto stdout. Useful for debugging. */
void reftable_ref_record_print(const struct reftable_ref_record *ref,
			       uint32_t hash_id);

/* frees and nulls all pointer values inside `ref`. */
void reftable_ref_record_release(struct reftable_ref_record *ref);

/* returns whether two reftable_ref_records are the same. Useful for testing. */
int reftable_ref_record_equal(const struct reftable_ref_record *a,
			      const struct reftable_ref_record *b, int hash_size);

/* reftable_log_record holds a reflog entry */
struct reftable_log_record {
	char *refname;
	uint64_t update_index; /* logical timestamp of a transactional update.
				*/

	enum {
		/* tombstone to hide deletions from earlier tables */
		REFTABLE_LOG_DELETION = 0x0,

		/* a simple update */
		REFTABLE_LOG_UPDATE = 0x1,
#define REFTABLE_NR_LOG_VALUETYPES 2
	} value_type;

	union {
		struct {
			uint8_t *new_hash;
			uint8_t *old_hash;
			char *name;
			char *email;
			uint64_t time;
			int16_t tz_offset;
			char *message;
		} update;
	} value;
};

/* returns whether 'ref' represents the deletion of a log record. */
int reftable_log_record_is_deletion(const struct reftable_log_record *log);

/* frees and nulls all pointer values. */
void reftable_log_record_release(struct reftable_log_record *log);

/* returns whether two records are equal. Useful for testing. */
int reftable_log_record_equal(const struct reftable_log_record *a,
			      const struct reftable_log_record *b, int hash_size);

/* dumps a reftable_log_record on stdout, for debugging/testing. */
void reftable_log_record_print(struct reftable_log_record *log,
			       uint32_t hash_id);

#endif
