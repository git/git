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
#include "reftable-basics.h"

struct reftable_buf {
	size_t alloc;
	size_t len;
	char *buf;
};
#define REFTABLE_BUF_INIT { 0 }

/*
 * Initialize the buffer such that it is ready for use. This is equivalent to
 * using REFTABLE_BUF_INIT for stack-allocated variables.
 */
void reftable_buf_init(struct reftable_buf *buf);

/*
 * Release memory associated with the buffer. The buffer is reinitialized such
 * that it can be reused for subsequent operations.
 */
void reftable_buf_release(struct reftable_buf *buf);

/*
 * Reset the buffer such that it is effectively empty, without releasing the
 * memory that this structure holds on to. This is equivalent to calling
 * `reftable_buf_setlen(buf, 0)`.
 */
void reftable_buf_reset(struct reftable_buf *buf);

/*
 * Trim the buffer to a shorter length by updating the `len` member and writing
 * a NUL byte to `buf[len]`. Returns 0 on success, -1 when `len` points outside
 * of the array.
 */
int reftable_buf_setlen(struct reftable_buf *buf, size_t len);

/*
 * Lexicographically compare the two buffers. Returns 0 when both buffers have
 * the same contents, -1 when `a` is lexicographically smaller than `b`, and 1
 * otherwise.
 */
int reftable_buf_cmp(const struct reftable_buf *a, const struct reftable_buf *b);

/*
 * Append `len` bytes from `data` to the buffer. This function works with
 * arbitrary byte sequences, including ones that contain embedded NUL
 * characters. As such, we use `void *` as input type. Returns 0 on success,
 * REFTABLE_OUT_OF_MEMORY_ERROR on allocation failure.
 */
int reftable_buf_add(struct reftable_buf *buf, const void *data, size_t len);

/* Equivalent to `reftable_buf_add(buf, s, strlen(s))`. */
int reftable_buf_addstr(struct reftable_buf *buf, const char *s);

/*
 * Detach the buffer from the structure such that the underlying memory is now
 * owned by the caller. The buffer is reinitialized such that it can be reused
 * for subsequent operations.
 */
char *reftable_buf_detach(struct reftable_buf *buf);

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

/*
 * Parse a newline separated list of names. `size` is the length of the buffer,
 * without terminating '\0'. Empty names are discarded. Returns a `NULL`
 * pointer when allocations fail.
 */
char **parse_names(char *buf, int size);

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
char *reftable_strdup(const char *str);

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
#define REFTABLE_FREE_AND_NULL(p) do { reftable_free(p); (p) = NULL; } while (0)

#ifndef REFTABLE_ALLOW_BANNED_ALLOCATORS
# define REFTABLE_BANNED(func) use_reftable_##func##_instead
# undef malloc
# define malloc(sz) REFTABLE_BANNED(malloc)
# undef realloc
# define realloc(ptr, sz) REFTABLE_BANNED(realloc)
# undef free
# define free(ptr) REFTABLE_BANNED(free)
# undef calloc
# define calloc(nelem, elsize) REFTABLE_BANNED(calloc)
# undef strdup
# define strdup(str) REFTABLE_BANNED(strdup)
#endif

/* Find the longest shared prefix size of `a` and `b` */
int common_prefix_size(struct reftable_buf *a, struct reftable_buf *b);

int hash_size(uint32_t id);

#endif
