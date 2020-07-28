/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "basics.h"

#include "system.h"

void put_u24(byte *out, uint32_t i)
{
	out[0] = (byte)((i >> 16) & 0xff);
	out[1] = (byte)((i >> 8) & 0xff);
	out[2] = (byte)((i)&0xff);
}

uint32_t get_u24(byte *in)
{
	return (uint32_t)(in[0]) << 16 | (uint32_t)(in[1]) << 8 |
	       (uint32_t)(in[2]);
}

void put_u32(byte *out, uint32_t i)
{
	out[0] = (byte)((i >> 24) & 0xff);
	out[1] = (byte)((i >> 16) & 0xff);
	out[2] = (byte)((i >> 8) & 0xff);
	out[3] = (byte)((i)&0xff);
}

uint32_t get_u32(byte *in)
{
	return (uint32_t)(in[0]) << 24 | (uint32_t)(in[1]) << 16 |
	       (uint32_t)(in[2]) << 8 | (uint32_t)(in[3]);
}

void put_u64(byte *out, uint64_t v)
{
	int i = 0;
	for (i = sizeof(uint64_t); i--;) {
		out[i] = (byte)(v & 0xff);
		v >>= 8;
	}
}

uint64_t get_u64(byte *out)
{
	uint64_t v = 0;
	int i = 0;
	for (i = 0; i < sizeof(uint64_t); i++) {
		v = (v << 8) | (byte)(out[i] & 0xff);
	}
	return v;
}

void put_u16(byte *out, uint16_t i)
{
	out[0] = (byte)((i >> 8) & 0xff);
	out[1] = (byte)((i)&0xff);
}

uint16_t get_u16(byte *in)
{
	return (uint32_t)(in[0]) << 8 | (uint32_t)(in[1]);
}

/*
  find smallest index i in [0, sz) at which f(i) is true, assuming
  that f is ascending. Return sz if f(i) is false for all indices.
*/
int binsearch(int sz, int (*f)(int k, void *args), void *args)
{
	int lo = 0;
	int hi = sz;

	/* invariant: (hi == sz) || f(hi) == true
	   (lo == 0 && f(0) == true) || fi(lo) == false
	 */
	while (hi - lo > 1) {
		int mid = lo + (hi - lo) / 2;

		int val = f(mid, args);
		if (val) {
			hi = mid;
		} else {
			lo = mid;
		}
	}

	if (lo == 0) {
		if (f(0, args)) {
			return 0;
		} else {
			return 1;
		}
	}

	return hi;
}

void free_names(char **a)
{
	char **p = a;
	if (p == NULL) {
		return;
	}
	while (*p) {
		free(*p);
		p++;
	}
	free(a);
}

int names_length(char **names)
{
	int len = 0;
	for (char **p = names; *p; p++) {
		len++;
	}
	return len;
}

/* parse a newline separated list of names. Empty names are discarded. */
void parse_names(char *buf, int size, char ***namesp)
{
	char **names = NULL;
	int names_cap = 0;
	int names_len = 0;

	char *p = buf;
	char *end = buf + size;
	while (p < end) {
		char *next = strchr(p, '\n');
		if (next != NULL) {
			*next = 0;
		} else {
			next = end;
		}
		if (p < next) {
			if (names_len == names_cap) {
				names_cap = 2 * names_cap + 1;
				names = realloc(names,
						names_cap * sizeof(char *));
			}
			names[names_len++] = strdup(p);
		}
		p = next + 1;
	}

	if (names_len == names_cap) {
		names_cap = 2 * names_cap + 1;
		names = realloc(names, names_cap * sizeof(char *));
	}

	names[names_len] = NULL;
	*namesp = names;
}

int names_equal(char **a, char **b)
{
	while (*a && *b) {
		if (strcmp(*a, *b)) {
			return 0;
		}

		a++;
		b++;
	}

	return *a == *b;
}

const char *error_str(int err)
{
	switch (err) {
	case IO_ERROR:
		return "I/O error";
	case FORMAT_ERROR:
		return "FORMAT_ERROR";
	case NOT_EXIST_ERROR:
		return "NOT_EXIST_ERROR";
	case LOCK_ERROR:
		return "LOCK_ERROR";
	case API_ERROR:
		return "API_ERROR";
	case ZLIB_ERROR:
		return "ZLIB_ERROR";
	case -1:
		return "general error";
	default:
		return "unknown error code";
	}
}
