/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_BASICS_H
#define REFTABLE_BASICS_H

#include <stddef.h>

/*
 * Hash functions understood by the reftable library. Note that the values are
 * arbitrary and somewhat random such that we can easily detect cases where the
 * hash hasn't been properly set up.
 */
enum reftable_hash {
	REFTABLE_HASH_SHA1   = 89,
	REFTABLE_HASH_SHA256 = 247,
};
#define REFTABLE_HASH_SIZE_SHA1   20
#define REFTABLE_HASH_SIZE_SHA256 32
#define REFTABLE_HASH_SIZE_MAX    REFTABLE_HASH_SIZE_SHA256

/* Overrides the functions to use for memory management. */
void reftable_set_alloc(void *(*malloc)(size_t),
			void *(*realloc)(void *, size_t), void (*free)(void *));

#endif
