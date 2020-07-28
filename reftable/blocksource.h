/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef BLOCKSOURCE_H
#define BLOCKSOURCE_H

#include "reftable.h"

uint64_t block_source_size(struct block_source source);
int block_source_read_block(struct block_source source, struct block *dest,
			    uint64_t off, uint32_t size);
void block_source_return_block(struct block_source source, struct block *ret);
void block_source_close(struct block_source source);

#endif
