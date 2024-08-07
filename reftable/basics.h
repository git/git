/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef BASICS_H
#define BASICS_H

/*
 * miscellaneous utilities that are not provided by Git.
 */

#include "system.h"

/* Bigendian en/decoding of integers */

void put_be24(uint8_t *out, uint32_t i);
uint32_t get_be24(uint8_t *in);
void put_be16(uint8_t *out, uint16_t i);

/*
 * find smallest index i in [0, sz) at which `f(i) > 0`, assuming that f is
 * ascending. Return sz if `f(i) == 0` for all indices. The search is aborted
 * and `sz` is returned in case `f(i) < 0`.
 *
 * Contrary to bsearch(3), this returns something useful if the argument is not
 * found.
 */
size_t binsearch(size_t sz, int (*f)(size_t k, void *args), void *args);

/*
 * Frees a NULL terminated array of malloced strings. The array itself is also
 * freed.
 */
void free_names(char **a);

/* parse a newline separated list of names. `size` is the length of the buffer,
 * without terminating '\0'. Empty names are discarded. */
void parse_names(char *buf, int size, char ***namesp);

/* compares two NULL-terminated arrays of strings. */
int names_equal(const char **a, const char **b);

/* returns the array size of a NULL-terminated array of strings. */
size_t names_length(const char **names);

/* Allocation routines; they invoke the functions set through
 * reftable_set_alloc() */
void *reftable_malloc(size_t sz);
void *reftable_realloc(void *p, size_t sz);
void reftable_free(void *p);
void *reftable_calloc(size_t nelem, size_t elsize);

#define REFTABLE_ALLOC_ARRAY(x, alloc) (x) = reftable_malloc(st_mult(sizeof(*(x)), (alloc)))
#define REFTABLE_CALLOC_ARRAY(x, alloc) (x) = reftable_calloc((alloc), sizeof(*(x)))
#define REFTABLE_REALLOC_ARRAY(x, alloc) (x) = reftable_realloc((x), st_mult(sizeof(*(x)), (alloc)))
#define REFTABLE_ALLOC_GROW(x, nr, alloc) \
	do { \
		if ((nr) > alloc) { \
			alloc = 2 * (alloc) + 1; \
			if (alloc < (nr)) \
				alloc = (nr); \
			REFTABLE_REALLOC_ARRAY(x, alloc); \
		} \
	} while (0)

/* Find the longest shared prefix size of `a` and `b` */
struct strbuf;
int common_prefix_size(struct strbuf *a, struct strbuf *b);

#endif
