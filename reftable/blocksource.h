/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef BLOCKSOURCE_H
#define BLOCKSOURCE_H

#include "system.h"

struct reftable_block_source;
struct reftable_buf;

/* Create an in-memory block source for reading reftables */
void block_source_from_buf(struct reftable_block_source *bs,
			   struct reftable_buf *buf);

#endif
