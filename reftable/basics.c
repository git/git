/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#define REFTABLE_ALLOW_BANNED_ALLOCATORS
#include "basics.h"
#include "reftable-basics.h"
#include "reftable-error.h"

static void *(*reftable_malloc_ptr)(size_t sz);
static void *(*reftable_realloc_ptr)(void *, size_t);
static void (*reftable_free_ptr)(void *);

void *reftable_malloc(size_t sz)
{
	if (!sz)
		return NULL;
	if (reftable_malloc_ptr)
		return (*reftable_malloc_ptr)(sz);
	return malloc(sz);
}

void *reftable_realloc(void *p, size_t sz)
{
	if (!sz) {
		reftable_free(p);
		return NULL;
	}

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
	void *p;

	if (nelem && elsize > SIZE_MAX / nelem)
		return NULL;

	p = reftable_malloc(nelem * elsize);
	if (!p)
		return NULL;

	memset(p, 0, nelem * elsize);
	return p;
}

char *reftable_strdup(const char *str)
{
	size_t len = strlen(str);
	char *result = reftable_malloc(len + 1);
	if (!result)
		return NULL;
	memcpy(result, str, len + 1);
	return result;
}

void reftable_set_alloc(void *(*malloc)(size_t),
			void *(*realloc)(void *, size_t), void (*free)(void *))
{
	reftable_malloc_ptr = malloc;
	reftable_realloc_ptr = realloc;
	reftable_free_ptr = free;
}

void reftable_buf_init(struct reftable_buf *buf)
{
	struct reftable_buf empty = REFTABLE_BUF_INIT;
	*buf = empty;
}

void reftable_buf_release(struct reftable_buf *buf)
{
	reftable_free(buf->buf);
	reftable_buf_init(buf);
}

void reftable_buf_reset(struct reftable_buf *buf)
{
	if (buf->alloc) {
		buf->len = 0;
		buf->buf[0] = '\0';
	}
}

int reftable_buf_setlen(struct reftable_buf *buf, size_t len)
{
	if (len > buf->len)
		return -1;
	if (len == buf->len)
		return 0;
	buf->buf[len] = '\0';
	buf->len = len;
	return 0;
}

int reftable_buf_cmp(const struct reftable_buf *a, const struct reftable_buf *b)
{
	size_t len = a->len < b->len ? a->len : b->len;
	if (len) {
		int cmp = memcmp(a->buf, b->buf, len);
		if (cmp)
			return cmp;
	}
	return a->len < b->len ? -1 : a->len != b->len;
}

int reftable_buf_add(struct reftable_buf *buf, const void *data, size_t len)
{
	size_t newlen = buf->len + len;

	if (newlen + 1 > buf->alloc) {
		if (REFTABLE_ALLOC_GROW(buf->buf, newlen + 1, buf->alloc))
			return REFTABLE_OUT_OF_MEMORY_ERROR;
	}

	memcpy(buf->buf + buf->len, data, len);
	buf->buf[newlen] = '\0';
	buf->len = newlen;

	return 0;
}

int reftable_buf_addstr(struct reftable_buf *buf, const char *s)
{
	return reftable_buf_add(buf, s, strlen(s));
}

char *reftable_buf_detach(struct reftable_buf *buf)
{
	char *result = buf->buf;
	reftable_buf_init(buf);
	return result;
}

void put_be24(uint8_t *out, uint32_t i)
{
	out[0] = (uint8_t)((i >> 16) & 0xff);
	out[1] = (uint8_t)((i >> 8) & 0xff);
	out[2] = (uint8_t)(i & 0xff);
}

uint32_t get_be24(uint8_t *in)
{
	return (uint32_t)(in[0]) << 16 | (uint32_t)(in[1]) << 8 |
	       (uint32_t)(in[2]);
}

void put_be16(uint8_t *out, uint16_t i)
{
	out[0] = (uint8_t)((i >> 8) & 0xff);
	out[1] = (uint8_t)(i & 0xff);
}

size_t binsearch(size_t sz, int (*f)(size_t k, void *args), void *args)
{
	size_t lo = 0;
	size_t hi = sz;

	/* Invariants:
	 *
	 *  (hi == sz) || f(hi) == true
	 *  (lo == 0 && f(0) == true) || fi(lo) == false
	 */
	while (hi - lo > 1) {
		size_t mid = lo + (hi - lo) / 2;
		int ret = f(mid, args);
		if (ret < 0)
			return sz;

		if (ret > 0)
			hi = mid;
		else
			lo = mid;
	}

	if (lo)
		return hi;

	return f(0, args) ? 0 : 1;
}

void free_names(char **a)
{
	char **p;
	if (!a) {
		return;
	}
	for (p = a; *p; p++) {
		reftable_free(*p);
	}
	reftable_free(a);
}

size_t names_length(const char **names)
{
	const char **p = names;
	while (*p)
		p++;
	return p - names;
}

char **parse_names(char *buf, int size)
{
	char **names = NULL;
	size_t names_cap = 0;
	size_t names_len = 0;
	char *p = buf;
	char *end = buf + size;

	while (p < end) {
		char *next = strchr(p, '\n');
		if (next && next < end) {
			*next = 0;
		} else {
			next = end;
		}
		if (p < next) {
			if (REFTABLE_ALLOC_GROW(names, names_len + 1,
						names_cap))
				goto err;

			names[names_len] = reftable_strdup(p);
			if (!names[names_len++])
				goto err;
		}
		p = next + 1;
	}

	if (REFTABLE_ALLOC_GROW(names, names_len + 1, names_cap))
		goto err;
	names[names_len] = NULL;

	return names;

err:
	for (size_t i = 0; i < names_len; i++)
		reftable_free(names[i]);
	reftable_free(names);
	return NULL;
}

int names_equal(const char **a, const char **b)
{
	size_t i = 0;
	for (; a[i] && b[i]; i++)
		if (strcmp(a[i], b[i]))
			return 0;
	return a[i] == b[i];
}

int common_prefix_size(struct reftable_buf *a, struct reftable_buf *b)
{
	int p = 0;
	for (; p < a->len && p < b->len; p++) {
		if (a->buf[p] != b->buf[p])
			break;
	}

	return p;
}

int hash_size(enum reftable_hash id)
{
	if (!id)
		return REFTABLE_HASH_SIZE_SHA1;
	switch (id) {
	case REFTABLE_HASH_SHA1:
		return REFTABLE_HASH_SIZE_SHA1;
	case REFTABLE_HASH_SHA256:
		return REFTABLE_HASH_SIZE_SHA256;
	}
	abort();
}
