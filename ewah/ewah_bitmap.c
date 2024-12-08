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
#include "git-compat-util.h"
#include "ewok.h"
#include "ewok_rlw.h"

static inline size_t min_size(size_t a, size_t b)
{
	return a < b ? a : b;
}

static inline size_t max_size(size_t a, size_t b)
{
	return a > b ? a : b;
}

static inline void buffer_grow(struct ewah_bitmap *self, size_t new_size)
{
	size_t rlw_offset = (uint8_t *)self->rlw - (uint8_t *)self->buffer;
	ALLOC_GROW(self->buffer, new_size, self->alloc_size);
	self->rlw = self->buffer + (rlw_offset / sizeof(eword_t));
}

static inline void buffer_push(struct ewah_bitmap *self, eword_t value)
{
	buffer_grow(self, self->buffer_size + 1);
	self->buffer[self->buffer_size++] = value;
}

static void buffer_push_rlw(struct ewah_bitmap *self, eword_t value)
{
	buffer_push(self, value);
	self->rlw = self->buffer + self->buffer_size - 1;
}

static size_t add_empty_words(struct ewah_bitmap *self, int v, size_t number)
{
	size_t added = 0;
	eword_t runlen, can_add;

	if (rlw_get_run_bit(self->rlw) != v && rlw_size(self->rlw) == 0) {
		rlw_set_run_bit(self->rlw, v);
	} else if (rlw_get_literal_words(self->rlw) != 0 ||
			rlw_get_run_bit(self->rlw) != v) {
		buffer_push_rlw(self, 0);
		if (v) rlw_set_run_bit(self->rlw, v);
		added++;
	}

	runlen = rlw_get_running_len(self->rlw);
	can_add = min_size(number, RLW_LARGEST_RUNNING_COUNT - runlen);

	rlw_set_running_len(self->rlw, runlen + can_add);
	number -= can_add;

	while (number >= RLW_LARGEST_RUNNING_COUNT) {
		buffer_push_rlw(self, 0);
		added++;
		if (v) rlw_set_run_bit(self->rlw, v);
		rlw_set_running_len(self->rlw, RLW_LARGEST_RUNNING_COUNT);
		number -= RLW_LARGEST_RUNNING_COUNT;
	}

	if (number > 0) {
		buffer_push_rlw(self, 0);
		added++;

		if (v) rlw_set_run_bit(self->rlw, v);
		rlw_set_running_len(self->rlw, number);
	}

	return added;
}

size_t ewah_add_empty_words(struct ewah_bitmap *self, int v, size_t number)
{
	if (number == 0)
		return 0;

	self->bit_size += number * BITS_IN_EWORD;
	return add_empty_words(self, v, number);
}

static size_t add_literal(struct ewah_bitmap *self, eword_t new_data)
{
	eword_t current_num = rlw_get_literal_words(self->rlw);

	if (current_num >= RLW_LARGEST_LITERAL_COUNT) {
		buffer_push_rlw(self, 0);

		rlw_set_literal_words(self->rlw, 1);
		buffer_push(self, new_data);
		return 2;
	}

	rlw_set_literal_words(self->rlw, current_num + 1);

	/* sanity check */
	assert(rlw_get_literal_words(self->rlw) == current_num + 1);

	buffer_push(self, new_data);
	return 1;
}

void ewah_add_dirty_words(
	struct ewah_bitmap *self, const eword_t *buffer,
	size_t number, int negate)
{
	size_t literals, can_add;

	while (1) {
		literals = rlw_get_literal_words(self->rlw);
		can_add = min_size(number, RLW_LARGEST_LITERAL_COUNT - literals);

		rlw_set_literal_words(self->rlw, literals + can_add);

		buffer_grow(self, self->buffer_size + can_add);

		if (negate) {
			size_t i;
			for (i = 0; i < can_add; ++i)
				self->buffer[self->buffer_size++] = ~buffer[i];
		} else {
			memcpy(self->buffer + self->buffer_size,
				buffer, can_add * sizeof(eword_t));
			self->buffer_size += can_add;
		}

		self->bit_size += can_add * BITS_IN_EWORD;

		if (number - can_add == 0)
			break;

		buffer_push_rlw(self, 0);
		buffer += can_add;
		number -= can_add;
	}
}

static size_t add_empty_word(struct ewah_bitmap *self, int v)
{
	int no_literal = (rlw_get_literal_words(self->rlw) == 0);
	eword_t run_len = rlw_get_running_len(self->rlw);

	if (no_literal && run_len == 0) {
		rlw_set_run_bit(self->rlw, v);
		assert(rlw_get_run_bit(self->rlw) == v);
	}

	if (no_literal && rlw_get_run_bit(self->rlw) == v &&
		run_len < RLW_LARGEST_RUNNING_COUNT) {
		rlw_set_running_len(self->rlw, run_len + 1);
		assert(rlw_get_running_len(self->rlw) == run_len + 1);
		return 0;
	} else {
		buffer_push_rlw(self, 0);

		assert(rlw_get_running_len(self->rlw) == 0);
		assert(rlw_get_run_bit(self->rlw) == 0);
		assert(rlw_get_literal_words(self->rlw) == 0);

		rlw_set_run_bit(self->rlw, v);
		assert(rlw_get_run_bit(self->rlw) == v);

		rlw_set_running_len(self->rlw, 1);
		assert(rlw_get_running_len(self->rlw) == 1);
		assert(rlw_get_literal_words(self->rlw) == 0);
		return 1;
	}
}

size_t ewah_add(struct ewah_bitmap *self, eword_t word)
{
	self->bit_size += BITS_IN_EWORD;

	if (word == 0)
		return add_empty_word(self, 0);

	if (word == (eword_t)(~0))
		return add_empty_word(self, 1);

	return add_literal(self, word);
}

void ewah_set(struct ewah_bitmap *self, size_t i)
{
	const size_t dist =
		DIV_ROUND_UP(i + 1, BITS_IN_EWORD) -
		DIV_ROUND_UP(self->bit_size, BITS_IN_EWORD);

	assert(i >= self->bit_size);

	self->bit_size = i + 1;

	if (dist > 0) {
		if (dist > 1)
			add_empty_words(self, 0, dist - 1);

		add_literal(self, (eword_t)1 << (i % BITS_IN_EWORD));
		return;
	}

	if (rlw_get_literal_words(self->rlw) == 0) {
		rlw_set_running_len(self->rlw,
			rlw_get_running_len(self->rlw) - 1);
		add_literal(self, (eword_t)1 << (i % BITS_IN_EWORD));
		return;
	}

	self->buffer[self->buffer_size - 1] |=
		((eword_t)1 << (i % BITS_IN_EWORD));

	/* check if we just completed a stream of 1s */
	if (self->buffer[self->buffer_size - 1] == (eword_t)(~0)) {
		self->buffer[--self->buffer_size] = 0;
		rlw_set_literal_words(self->rlw,
			rlw_get_literal_words(self->rlw) - 1);
		add_empty_word(self, 1);
	}
}

void ewah_each_bit(struct ewah_bitmap *self, void (*callback)(size_t, void*), void *payload)
{
	size_t pos = 0;
	size_t pointer = 0;
	size_t k;

	while (pointer < self->buffer_size) {
		eword_t *word = &self->buffer[pointer];

		if (rlw_get_run_bit(word)) {
			size_t len = rlw_get_running_len(word) * BITS_IN_EWORD;
			for (k = 0; k < len; ++k, ++pos)
				callback(pos, payload);
		} else {
			pos += rlw_get_running_len(word) * BITS_IN_EWORD;
		}

		++pointer;

		for (k = 0; k < rlw_get_literal_words(word); ++k) {
			int c;

			/* todo: zero count optimization */
			for (c = 0; c < BITS_IN_EWORD; ++c, ++pos) {
				if ((self->buffer[pointer] & ((eword_t)1 << c)) != 0)
					callback(pos, payload);
			}

			++pointer;
		}
	}
}

/**
 * Clear all the bits in the bitmap. Does not free or resize
 * memory.
 */
static void ewah_clear(struct ewah_bitmap *self)
{
	self->buffer_size = 1;
	self->buffer[0] = 0;
	self->bit_size = 0;
	self->rlw = self->buffer;
}

struct ewah_bitmap *ewah_new(void)
{
	struct ewah_bitmap *self;

	self = xmalloc(sizeof(struct ewah_bitmap));
	self->alloc_size = 32;
	ALLOC_ARRAY(self->buffer, self->alloc_size);

	ewah_clear(self);
	return self;
}

void ewah_free(struct ewah_bitmap *self)
{
	if (!self)
		return;

	if (self->alloc_size)
		free(self->buffer);

	free(self);
}

static void read_new_rlw(struct ewah_iterator *it)
{
	const eword_t *word = NULL;

	it->literals = 0;
	it->compressed = 0;

	while (1) {
		word = &it->buffer[it->pointer];

		it->rl = rlw_get_running_len(word);
		it->lw = rlw_get_literal_words(word);
		it->b = rlw_get_run_bit(word);

		if (it->rl || it->lw)
			return;

		if (it->pointer < it->buffer_size - 1) {
			it->pointer++;
		} else {
			it->pointer = it->buffer_size;
			return;
		}
	}
}

int ewah_iterator_next(eword_t *next, struct ewah_iterator *it)
{
	if (it->pointer >= it->buffer_size)
		return 0;

	if (it->compressed < it->rl) {
		it->compressed++;
		*next = it->b ? (eword_t)(~0) : 0;
	} else {
		assert(it->literals < it->lw);

		it->literals++;
		it->pointer++;

		assert(it->pointer < it->buffer_size);

		*next = it->buffer[it->pointer];
	}

	if (it->compressed == it->rl && it->literals == it->lw) {
		if (++it->pointer < it->buffer_size)
			read_new_rlw(it);
	}

	return 1;
}

void ewah_iterator_init(struct ewah_iterator *it, struct ewah_bitmap *parent)
{
	it->buffer = parent->buffer;
	it->buffer_size = parent->buffer_size;
	it->pointer = 0;

	it->lw = 0;
	it->rl = 0;
	it->compressed = 0;
	it->literals = 0;
	it->b = 0;

	if (it->pointer < it->buffer_size)
		read_new_rlw(it);
}

void ewah_or_iterator_init(struct ewah_or_iterator *it,
			   struct ewah_bitmap **parents, size_t nr)
{
	size_t i;

	memset(it, 0, sizeof(*it));

	ALLOC_ARRAY(it->its, nr);
	for (i = 0; i < nr; i++)
		ewah_iterator_init(&it->its[it->nr++], parents[i]);
}

int ewah_or_iterator_next(eword_t *next, struct ewah_or_iterator *it)
{
	eword_t buf, out = 0;
	size_t i;
	int ret = 0;

	for (i = 0; i < it->nr; i++)
		if (ewah_iterator_next(&buf, &it->its[i])) {
			out |= buf;
			ret = 1;
		}

	*next = out;
	return ret;
}

void ewah_or_iterator_free(struct ewah_or_iterator *it)
{
	free(it->its);
}

void ewah_xor(
	struct ewah_bitmap *ewah_i,
	struct ewah_bitmap *ewah_j,
	struct ewah_bitmap *out)
{
	struct rlw_iterator rlw_i;
	struct rlw_iterator rlw_j;
	size_t literals;

	rlwit_init(&rlw_i, ewah_i);
	rlwit_init(&rlw_j, ewah_j);

	while (rlwit_word_size(&rlw_i) > 0 && rlwit_word_size(&rlw_j) > 0) {
		while (rlw_i.rlw.running_len > 0 || rlw_j.rlw.running_len > 0) {
			struct rlw_iterator *prey, *predator;
			size_t index;
			int negate_words;

			if (rlw_i.rlw.running_len < rlw_j.rlw.running_len) {
				prey = &rlw_i;
				predator = &rlw_j;
			} else {
				prey = &rlw_j;
				predator = &rlw_i;
			}

			negate_words = !!predator->rlw.running_bit;
			index = rlwit_discharge(prey, out,
				predator->rlw.running_len, negate_words);

			ewah_add_empty_words(out, negate_words,
				predator->rlw.running_len - index);

			rlwit_discard_first_words(predator,
				predator->rlw.running_len);
		}

		literals = min_size(
			rlw_i.rlw.literal_words,
			rlw_j.rlw.literal_words);

		if (literals) {
			size_t k;

			for (k = 0; k < literals; ++k) {
				ewah_add(out,
					rlw_i.buffer[rlw_i.literal_word_start + k] ^
					rlw_j.buffer[rlw_j.literal_word_start + k]
				);
			}

			rlwit_discard_first_words(&rlw_i, literals);
			rlwit_discard_first_words(&rlw_j, literals);
		}
	}

	if (rlwit_word_size(&rlw_i) > 0)
		rlwit_discharge(&rlw_i, out, ~0, 0);
	else
		rlwit_discharge(&rlw_j, out, ~0, 0);

	out->bit_size = max_size(ewah_i->bit_size, ewah_j->bit_size);
}

#define BITMAP_POOL_MAX 16
static struct ewah_bitmap *bitmap_pool[BITMAP_POOL_MAX];
static size_t bitmap_pool_size;

struct ewah_bitmap *ewah_pool_new(void)
{
	if (bitmap_pool_size)
		return bitmap_pool[--bitmap_pool_size];

	return ewah_new();
}

void ewah_pool_free(struct ewah_bitmap *self)
{
	if (!self)
		return;

	if (bitmap_pool_size == BITMAP_POOL_MAX ||
		self->alloc_size == 0) {
		ewah_free(self);
		return;
	}

	ewah_clear(self);
	bitmap_pool[bitmap_pool_size++] = self;
}

uint32_t ewah_checksum(struct ewah_bitmap *self)
{
	const uint8_t *p = (uint8_t *)self->buffer;
	uint32_t crc = (uint32_t)self->bit_size;
	size_t size = self->buffer_size * sizeof(eword_t);

	while (size--)
		crc = (crc << 5) - crc + (uint32_t)*p++;

	return crc;
}
