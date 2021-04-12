/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef GENERIC_H
#define GENERIC_H

#include "record.h"
#include "reftable-generic.h"

/* generic interface to reftables */
struct reftable_table_vtable {
	int (*seek_record)(void *tab, struct reftable_iterator *it,
			   struct reftable_record *);
	uint32_t (*hash_id)(void *tab);
	uint64_t (*min_update_index)(void *tab);
	uint64_t (*max_update_index)(void *tab);
};

struct reftable_iterator_vtable {
	int (*next)(void *iter_arg, struct reftable_record *rec);
	void (*close)(void *iter_arg);
};

void iterator_set_empty(struct reftable_iterator *it);
int iterator_next(struct reftable_iterator *it, struct reftable_record *rec);

#endif
