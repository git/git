/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_ITERATOR_H
#define REFTABLE_ITERATOR_H

#include "reftable-record.h"

struct reftable_iterator_vtable;

/* iterator is the generic interface for walking over data stored in a
 * reftable.
 */
struct reftable_iterator {
	struct reftable_iterator_vtable *ops;
	void *iter_arg;
};

/* reads the next reftable_ref_record. Returns < 0 for error, 0 for OK and > 0:
 * end of iteration.
 */
int reftable_iterator_next_ref(struct reftable_iterator *it,
			       struct reftable_ref_record *ref);

/* reads the next reftable_log_record. Returns < 0 for error, 0 for OK and > 0:
 * end of iteration.
 */
int reftable_iterator_next_log(struct reftable_iterator *it,
			       struct reftable_log_record *log);

/* releases resources associated with an iterator. */
void reftable_iterator_destroy(struct reftable_iterator *it);

#endif
