/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_ERROR_H
#define REFTABLE_ERROR_H

/*
 * Errors in reftable calls are signaled with negative integer return values. 0
 * means success.
 */
enum reftable_error {
	/* Unexpected file system behavior */
	REFTABLE_IO_ERROR = -2,

	/* Format inconsistency on reading data */
	REFTABLE_FORMAT_ERROR = -3,

	/* File does not exist. Returned from block_source_from_file(), because
	 * it needs special handling in stack.
	 */
	REFTABLE_NOT_EXIST_ERROR = -4,

	/* Trying to write out-of-date data. */
	REFTABLE_LOCK_ERROR = -5,

	/* Misuse of the API:
	 *  - on writing a record with NULL refname.
	 *  - on writing a reftable_ref_record outside the table limits
	 *  - on writing a ref or log record before the stack's
	 * next_update_inde*x
	 *  - on writing a log record with multiline message with
	 *  exact_log_message unset
	 *  - on reading a reftable_ref_record from log iterator, or vice versa.
	 *
	 * When a call misuses the API, the internal state of the library is
	 * kept unchanged.
	 */
	REFTABLE_API_ERROR = -6,

	/* Decompression error */
	REFTABLE_ZLIB_ERROR = -7,

	/* Wrote a table without blocks. */
	REFTABLE_EMPTY_TABLE_ERROR = -8,

	/* Dir/file conflict. */
	REFTABLE_NAME_CONFLICT = -9,

	/* Invalid ref name. */
	REFTABLE_REFNAME_ERROR = -10,

	/* Entry does not fit. This can happen when writing outsize reflog
	   messages. */
	REFTABLE_ENTRY_TOO_BIG_ERROR = -11,
};

/* convert the numeric error code to a string. The string should not be
 * deallocated. */
const char *reftable_error_str(int err);

#endif
