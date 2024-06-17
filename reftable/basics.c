/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "basics.h"

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

void parse_names(char *buf, int size, char ***namesp)
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
			REFTABLE_ALLOC_GROW(names, names_len + 1, names_cap);
			names[names_len++] = xstrdup(p);
		}
		p = next + 1;
	}

	REFTABLE_REALLOC_ARRAY(names, names_len + 1);
	names[names_len] = NULL;
	*namesp = names;
}

int names_equal(const char **a, const char **b)
{
	size_t i = 0;
	for (; a[i] && b[i]; i++)
		if (strcmp(a[i], b[i]))
			return 0;
	return a[i] == b[i];
}

int common_prefix_size(struct strbuf *a, struct strbuf *b)
{
	int p = 0;
	for (; p < a->len && p < b->len; p++) {
		if (a->buf[p] != b->buf[p])
			break;
	}

	return p;
}
