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
	void (*init_iter)(void *tab, struct reftable_iterator *it, uint8_t typ);
	uint32_t (*hash_id)(void *tab);
	uint64_t (*min_update_index)(void *tab);
	uint64_t (*max_update_index)(void *tab);
};

void table_init_iter(struct reftable_table *tab,
		     struct reftable_iterator *it,
		     uint8_t typ);

#endif
