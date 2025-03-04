/**
 * Copyright 2013, GitHub, Inc
 * Copyright 2009-2013, Daniel Lemire, Cliff Moon,
 *	David McIntosh, Robert Becho, Google Inc. and Veronika Zenz
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __EWOK_BITMAP_H__
#define __EWOK_BITMAP_H__

struct strbuf;
typedef uint64_t eword_t;
#define BITS_IN_EWORD (sizeof(eword_t) * 8)

/**
 * Do not use __builtin_popcountll. The GCC implementation
 * is notoriously slow on all platforms.
 *
 * See: http://gcc.gnu.org/bugzilla/show_bug.cgi?id=36041
 */
static inline uint32_t ewah_bit_popcount64(uint64_t x)
{
	x = (x & 0x5555555555555555ULL) + ((x >>  1) & 0x5555555555555555ULL);
	x = (x & 0x3333333333333333ULL) + ((x >>  2) & 0x3333333333333333ULL);
	x = (x & 0x0F0F0F0F0F0F0F0FULL) + ((x >>  4) & 0x0F0F0F0F0F0F0F0FULL);
	return (x * 0x0101010101010101ULL) >> 56;
}

/* __builtin_ctzll was not available until 3.4.0 */
#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3  && __GNUC_MINOR > 3))
#define ewah_bit_ctz64(x) __builtin_ctzll(x)
#else
static inline int ewah_bit_ctz64(uint64_t x)
{
	int n = 0;
	if ((x & 0xffffffff) == 0) { x >>= 32; n += 32; }
	if ((x &     0xffff) == 0) { x >>= 16; n += 16; }
	if ((x &       0xff) == 0) { x >>=  8; n +=  8; }
	if ((x &        0xf) == 0) { x >>=  4; n +=  4; }
	if ((x &        0x3) == 0) { x >>=  2; n +=  2; }
	if ((x &        0x1) == 0) { x >>=  1; n +=  1; }
	return n + !x;
}
#endif

struct ewah_bitmap {
	eword_t *buffer;
	size_t buffer_size;
	size_t alloc_size;
	size_t bit_size;
	eword_t *rlw;
};

typedef void (*ewah_callback)(size_t pos, void *);

struct ewah_bitmap *ewah_pool_new(void);
void ewah_pool_free(struct ewah_bitmap *self);

/**
 * Allocate a new EWAH Compressed bitmap
 */
struct ewah_bitmap *ewah_new(void);

/**
 * Free all the memory of the bitmap
 */
void ewah_free(struct ewah_bitmap *self);

int ewah_serialize_to(struct ewah_bitmap *self,
		      int (*write_fun)(void *out, const void *buf, size_t len),
		      void *out);
int ewah_serialize_strbuf(struct ewah_bitmap *self, struct strbuf *);

ssize_t ewah_read_mmap(struct ewah_bitmap *self, const void *map, size_t len);

uint32_t ewah_checksum(struct ewah_bitmap *self);

/**
 * Call the given callback with the position of every single bit
 * that has been set on the bitmap.
 *
 * This is an efficient operation that does not fully decompress
 * the bitmap.
 */
void ewah_each_bit(struct ewah_bitmap *self, ewah_callback callback, void *payload);

/**
 * Set a given bit on the bitmap.
 *
 * The bit at position `pos` will be set to true. Because of the
 * way that the bitmap is compressed, a set bit cannot be unset
 * later on.
 *
 * Furthermore, since the bitmap uses streaming compression, bits
 * can only set incrementally.
 *
 * E.g.
 *		ewah_set(bitmap, 1); // ok
 *		ewah_set(bitmap, 76); // ok
 *		ewah_set(bitmap, 77); // ok
 *		ewah_set(bitmap, 8712800127); // ok
 *		ewah_set(bitmap, 25); // failed, assert raised
 */
void ewah_set(struct ewah_bitmap *self, size_t i);

struct ewah_iterator {
	const eword_t *buffer;
	size_t buffer_size;

	size_t pointer;
	eword_t compressed, literals;
	eword_t rl, lw;
	int b;
};

/**
 * Initialize a new iterator to run through the bitmap in uncompressed form.
 *
 * The iterator can be stack allocated. The underlying bitmap must not be freed
 * before the iteration is over.
 *
 * E.g.
 *
 *		struct ewah_bitmap *bitmap = ewah_new();
 *		struct ewah_iterator it;
 *
 *		ewah_iterator_init(&it, bitmap);
 */
void ewah_iterator_init(struct ewah_iterator *it, struct ewah_bitmap *parent);

/**
 * Yield every single word in the bitmap in uncompressed form. This is:
 * yield single words (32-64 bits) where each bit represents an actual
 * bit from the bitmap.
 *
 * Return: true if a word was yield, false if there are no words left
 */
int ewah_iterator_next(eword_t *next, struct ewah_iterator *it);

struct ewah_or_iterator {
	struct ewah_iterator *its;
	size_t nr;
};

void ewah_or_iterator_init(struct ewah_or_iterator *it,
			   struct ewah_bitmap **parents, size_t nr);

int ewah_or_iterator_next(eword_t *next, struct ewah_or_iterator *it);

void ewah_or_iterator_free(struct ewah_or_iterator *it);

void ewah_xor(
	struct ewah_bitmap *ewah_i,
	struct ewah_bitmap *ewah_j,
	struct ewah_bitmap *out);

/**
 * Direct word access
 */
size_t ewah_add_empty_words(struct ewah_bitmap *self, int v, size_t number);
void ewah_add_dirty_words(
	struct ewah_bitmap *self, const eword_t *buffer, size_t number, int negate);
size_t ewah_add(struct ewah_bitmap *self, eword_t word);


/**
 * Uncompressed, old-school bitmap that can be efficiently compressed
 * into an `ewah_bitmap`.
 */
struct bitmap {
	eword_t *words;
	size_t word_alloc;
};

struct bitmap *bitmap_new(void);
struct bitmap *bitmap_word_alloc(size_t word_alloc);
struct bitmap *bitmap_dup(const struct bitmap *src);
void bitmap_set(struct bitmap *self, size_t pos);
void bitmap_unset(struct bitmap *self, size_t pos);
int bitmap_get(struct bitmap *self, size_t pos);
void bitmap_free(struct bitmap *self);
int bitmap_equals(struct bitmap *self, struct bitmap *other);
int bitmap_equals_ewah(struct bitmap *self, struct ewah_bitmap *other);

/*
 * Both `bitmap_is_subset()` and `ewah_bitmap_is_subset()` return 1 if the set
 * of bits in 'self' are a subset of the bits in 'other'. Returns 0 otherwise.
 */
int bitmap_is_subset(struct bitmap *self, struct bitmap *other);
int ewah_bitmap_is_subset(struct ewah_bitmap *self, struct bitmap *other);

struct ewah_bitmap * bitmap_to_ewah(struct bitmap *bitmap);
struct bitmap *ewah_to_bitmap(struct ewah_bitmap *ewah);

void bitmap_and_not(struct bitmap *self, struct bitmap *other);
void bitmap_or_ewah(struct bitmap *self, struct ewah_bitmap *other);
void bitmap_or(struct bitmap *self, const struct bitmap *other);

size_t bitmap_popcount(struct bitmap *self);
size_t ewah_bitmap_popcount(struct ewah_bitmap *self);
int bitmap_is_empty(struct bitmap *self);

#endif
