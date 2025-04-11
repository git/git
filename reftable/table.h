/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
 */

#ifndef TABLE_H
#define TABLE_H

#include "block.h"
#include "record.h"
#include "reftable-iterator.h"
#include "reftable-table.h"

const char *reftable_table_name(struct reftable_table *t);

int table_init_iter(struct reftable_table *t,
		    struct reftable_iterator *it,
		    uint8_t typ);

/*
 * Initialize a block by reading from the given table and offset.
 */
int table_init_block(struct reftable_table *t, struct reftable_block *block,
		     uint64_t next_off, uint8_t want_typ);

#endif
