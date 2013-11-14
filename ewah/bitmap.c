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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "git-compat-util.h"
#include "ewok.h"

#define MASK(x) ((eword_t)1 << (x % BITS_IN_WORD))
#define BLOCK(x) (x / BITS_IN_WORD)

struct bitmap *bitmap_new(void)
{
	struct bitmap *bitmap = ewah_malloc(sizeof(struct bitmap));
	bitmap->words = ewah_calloc(32, sizeof(eword_t));
	bitmap->word_alloc = 32;
	return bitmap;
}

void bitmap_set(struct bitmap *self, size_t pos)
{
	size_t block = BLOCK(pos);

	if (block >= self->word_alloc) {
		size_t old_size = self->word_alloc;
		self->word_alloc = block * 2;
		self->words = ewah_realloc(self->words,
			self->word_alloc * sizeof(eword_t));

		memset(self->words + old_size, 0x0,
			(self->word_alloc - old_size) * sizeof(eword_t));
	}

	self->words[block] |= MASK(pos);
}

void bitmap_clear(struct bitmap *self, size_t pos)
{
	size_t block = BLOCK(pos);

	if (block < self->word_alloc)
		self->words[block] &= ~MASK(pos);
}

int bitmap_get(struct bitmap *self, size_t pos)
{
	size_t block = BLOCK(pos);
	return block < self->word_alloc &&
		(self->words[block] & MASK(pos)) != 0;
}

struct ewah_bitmap *bitmap_to_ewah(struct bitmap *bitmap)
{
	struct ewah_bitmap *ewah = ewah_new();
	size_t i, running_empty_words = 0;
	eword_t last_word = 0;

	for (i = 0; i < bitmap->word_alloc; ++i) {
		if (bitmap->words[i] == 0) {
			running_empty_words++;
			continue;
		}

		if (last_word != 0)
			ewah_add(ewah, last_word);

		if (running_empty_words > 0) {
			ewah_add_empty_words(ewah, 0, running_empty_words);
			running_empty_words = 0;
		}

		last_word = bitmap->words[i];
	}

	ewah_add(ewah, last_word);
	return ewah;
}

struct bitmap *ewah_to_bitmap(struct ewah_bitmap *ewah)
{
	struct bitmap *bitmap = bitmap_new();
	struct ewah_iterator it;
	eword_t blowup;
	size_t i = 0;

	ewah_iterator_init(&it, ewah);

	while (ewah_iterator_next(&blowup, &it)) {
		if (i >= bitmap->word_alloc) {
			bitmap->word_alloc *= 1.5;
			bitmap->words = ewah_realloc(
				bitmap->words, bitmap->word_alloc * sizeof(eword_t));
		}

		bitmap->words[i++] = blowup;
	}

	bitmap->word_alloc = i;
	return bitmap;
}

void bitmap_and_not(struct bitmap *self, struct bitmap *other)
{
	const size_t count = (self->word_alloc < other->word_alloc) ?
		self->word_alloc : other->word_alloc;

	size_t i;

	for (i = 0; i < count; ++i)
		self->words[i] &= ~other->words[i];
}

void bitmap_or_ewah(struct bitmap *self, struct ewah_bitmap *other)
{
	size_t original_size = self->word_alloc;
	size_t other_final = (other->bit_size / BITS_IN_WORD) + 1;
	size_t i = 0;
	struct ewah_iterator it;
	eword_t word;

	if (self->word_alloc < other_final) {
		self->word_alloc = other_final;
		self->words = ewah_realloc(self->words,
			self->word_alloc * sizeof(eword_t));
		memset(self->words + original_size, 0x0,
			(self->word_alloc - original_size) * sizeof(eword_t));
	}

	ewah_iterator_init(&it, other);

	while (ewah_iterator_next(&word, &it))
		self->words[i++] |= word;
}

void bitmap_each_bit(struct bitmap *self, ewah_callback callback, void *data)
{
	size_t pos = 0, i;

	for (i = 0; i < self->word_alloc; ++i) {
		eword_t word = self->words[i];
		uint32_t offset;

		if (word == (eword_t)~0) {
			for (offset = 0; offset < BITS_IN_WORD; ++offset)
				callback(pos++, data);
		} else {
			for (offset = 0; offset < BITS_IN_WORD; ++offset) {
				if ((word >> offset) == 0)
					break;

				offset += ewah_bit_ctz64(word >> offset);
				callback(pos + offset, data);
			}
			pos += BITS_IN_WORD;
		}
	}
}

size_t bitmap_popcount(struct bitmap *self)
{
	size_t i, count = 0;

	for (i = 0; i < self->word_alloc; ++i)
		count += ewah_bit_popcount64(self->words[i]);

	return count;
}

int bitmap_equals(struct bitmap *self, struct bitmap *other)
{
	struct bitmap *big, *small;
	size_t i;

	if (self->word_alloc < other->word_alloc) {
		small = self;
		big = other;
	} else {
		small = other;
		big = self;
	}

	for (i = 0; i < small->word_alloc; ++i) {
		if (small->words[i] != big->words[i])
			return 0;
	}

	for (; i < big->word_alloc; ++i) {
		if (big->words[i] != 0)
			return 0;
	}

	return 1;
}

void bitmap_reset(struct bitmap *bitmap)
{
	memset(bitmap->words, 0x0, bitmap->word_alloc * sizeof(eword_t));
}

void bitmap_free(struct bitmap *bitmap)
{
	if (bitmap == NULL)
		return;

	free(bitmap->words);
	free(bitmap);
}
