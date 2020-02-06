/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "slice.h"

#include "system.h"

#include "reftable.h"

void slice_set_string(struct slice *s, const char *str)
{
	if (str == NULL) {
		s->len = 0;
		return;
	}

	{
		int l = strlen(str);
		l++; /* \0 */
		slice_resize(s, l);
		memcpy(s->buf, str, l);
		s->len = l - 1;
	}
}

void slice_resize(struct slice *s, int l)
{
	if (s->cap < l) {
		int c = s->cap * 2;
		if (c < l) {
			c = l;
		}
		s->cap = c;
		s->buf = realloc(s->buf, s->cap);
	}
	s->len = l;
}

void slice_append_string(struct slice *d, const char *s)
{
	int l1 = d->len;
	int l2 = strlen(s);

	slice_resize(d, l2 + l1);
	memcpy(d->buf + l1, s, l2);
}

void slice_append(struct slice *s, struct slice a)
{
	int end = s->len;
	slice_resize(s, s->len + a.len);
	memcpy(s->buf + end, a.buf, a.len);
}

byte *slice_yield(struct slice *s)
{
	byte *p = s->buf;
	s->buf = NULL;
	s->cap = 0;
	s->len = 0;
	return p;
}

void slice_copy(struct slice *dest, struct slice src)
{
	slice_resize(dest, src.len);
	memcpy(dest->buf, src.buf, src.len);
}

/* return the underlying data as char*. len is left unchanged, but
   a \0 is added at the end. */
const char *slice_as_string(struct slice *s)
{
	if (s->cap == s->len) {
		int l = s->len;
		slice_resize(s, l + 1);
		s->len = l;
	}
	s->buf[s->len] = 0;
	return (const char *)s->buf;
}

/* return a newly malloced string for this slice */
char *slice_to_string(struct slice in)
{
	struct slice s = {};
	slice_resize(&s, in.len + 1);
	s.buf[in.len] = 0;
	memcpy(s.buf, in.buf, in.len);
	return (char *)slice_yield(&s);
}

bool slice_equal(struct slice a, struct slice b)
{
	if (a.len != b.len) {
		return 0;
	}
	return memcmp(a.buf, b.buf, a.len) == 0;
}

int slice_compare(struct slice a, struct slice b)
{
	int min = a.len < b.len ? a.len : b.len;
	int res = memcmp(a.buf, b.buf, min);
	if (res != 0) {
		return res;
	}
	if (a.len < b.len) {
		return -1;
	} else if (a.len > b.len) {
		return 1;
	} else {
		return 0;
	}
}

int slice_write(struct slice *b, byte *data, int sz)
{
	if (b->len + sz > b->cap) {
		int newcap = 2 * b->cap + 1;
		if (newcap < b->len + sz) {
			newcap = (b->len + sz);
		}
		b->buf = realloc(b->buf, newcap);
		b->cap = newcap;
	}

	memcpy(b->buf + b->len, data, sz);
	b->len += sz;
	return sz;
}

int slice_write_void(void *b, byte *data, int sz)
{
	return slice_write((struct slice *)b, data, sz);
}

static uint64_t slice_size(void *b)
{
	return ((struct slice *)b)->len;
}

static void slice_return_block(void *b, struct block *dest)
{
	memset(dest->data, 0xff, dest->len);
	free(dest->data);
}

static void slice_close(void *b)
{
}

static int slice_read_block(void *v, struct block *dest, uint64_t off,
			    uint32_t size)
{
	struct slice *b = (struct slice *)v;
	assert(off + size <= b->len);
	dest->data = calloc(size, 1);
	memcpy(dest->data, b->buf + off, size);
	dest->len = size;
	return size;
}

struct block_source_vtable slice_vtable = {
	.size = &slice_size,
	.read_block = &slice_read_block,
	.return_block = &slice_return_block,
	.close = &slice_close,
};

void block_source_from_slice(struct block_source *bs, struct slice *buf)
{
	bs->ops = &slice_vtable;
	bs->arg = buf;
}

static void malloc_return_block(void *b, struct block *dest)
{
	memset(dest->data, 0xff, dest->len);
	free(dest->data);
}

struct block_source_vtable malloc_vtable = {
	.return_block = &malloc_return_block,
};

struct block_source malloc_block_source_instance = {
	.ops = &malloc_vtable,
};

struct block_source malloc_block_source(void)
{
	return malloc_block_source_instance;
}
