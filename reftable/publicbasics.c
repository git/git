/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"
#include "reftable-malloc.h"

#include "basics.h"

static void *(*reftable_malloc_ptr)(size_t sz);
static void *(*reftable_realloc_ptr)(void *, size_t);
static void (*reftable_free_ptr)(void *);

void *reftable_malloc(size_t sz)
{
	if (reftable_malloc_ptr)
		return (*reftable_malloc_ptr)(sz);
	return malloc(sz);
}

void *reftable_realloc(void *p, size_t sz)
{
	if (reftable_realloc_ptr)
		return (*reftable_realloc_ptr)(p, sz);
	return realloc(p, sz);
}

void reftable_free(void *p)
{
	if (reftable_free_ptr)
		reftable_free_ptr(p);
	else
		free(p);
}

void *reftable_calloc(size_t nelem, size_t elsize)
{
	size_t sz = st_mult(nelem, elsize);
	void *p = reftable_malloc(sz);
	memset(p, 0, sz);
	return p;
}

void reftable_set_alloc(void *(*malloc)(size_t),
			void *(*realloc)(void *, size_t), void (*free)(void *))
{
	reftable_malloc_ptr = malloc;
	reftable_realloc_ptr = realloc;
	reftable_free_ptr = free;
}

int hash_size(uint32_t id)
{
	switch (id) {
	case 0:
	case GIT_SHA1_FORMAT_ID:
		return GIT_SHA1_RAWSZ;
	case GIT_SHA256_FORMAT_ID:
		return GIT_SHA256_RAWSZ;
	}
	abort();
}
