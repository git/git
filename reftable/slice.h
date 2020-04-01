/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef SLICE_H
#define SLICE_H

#include "basics.h"
#include "reftable.h"

struct slice {
	byte *buf;
	int len;
	int cap;
};

void slice_set_string(struct slice *dest, const char *);
void slice_append_string(struct slice *dest, const char *);
char *slice_to_string(struct slice src);
const char *slice_as_string(struct slice *src);
bool slice_equal(struct slice a, struct slice b);
byte *slice_yield(struct slice *s);
void slice_copy(struct slice *dest, struct slice src);
void slice_consume(struct slice *s, int n);
void slice_resize(struct slice *s, int l);
int slice_compare(struct slice a, struct slice b);
int slice_write(struct slice *b, byte *data, int sz);
int slice_write_void(void *b, byte *data, int sz);
void slice_append(struct slice *dest, struct slice add);
int common_prefix_size(struct slice a, struct slice b);

struct reftable_block_source;
void block_source_from_slice(struct reftable_block_source *bs,
			     struct slice *buf);

struct reftable_block_source malloc_block_source(void);

#endif
