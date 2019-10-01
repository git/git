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
#ifndef __EWOK_RLW_H__
#define __EWOK_RLW_H__

#include "ewok.h"

#define RLW_RUNNING_BITS (sizeof(eword_t) * 4)
#define RLW_LITERAL_BITS (sizeof(eword_t) * 8 - 1 - RLW_RUNNING_BITS)

#define RLW_LARGEST_RUNNING_COUNT (((eword_t)1 << RLW_RUNNING_BITS) - 1)
#define RLW_LARGEST_LITERAL_COUNT (((eword_t)1 << RLW_LITERAL_BITS) - 1)

#define RLW_LARGEST_RUNNING_COUNT_SHIFT (RLW_LARGEST_RUNNING_COUNT << 1)

#define RLW_RUNNING_LEN_PLUS_BIT (((eword_t)1 << (RLW_RUNNING_BITS + 1)) - 1)

static inline int rlw_get_run_bit(const eword_t *word)
{
	return *word & (eword_t)1;
}

static inline void rlw_set_run_bit(eword_t *word, int b)
{
	if (b) {
		*word |= (eword_t)1;
	} else {
		*word &= (eword_t)(~1);
	}
}

static inline void rlw_xor_run_bit(eword_t *word)
{
	if (*word & 1) {
		*word &= (eword_t)(~1);
	} else {
		*word |= (eword_t)1;
	}
}

static inline void rlw_set_running_len(eword_t *word, eword_t l)
{
	*word |= RLW_LARGEST_RUNNING_COUNT_SHIFT;
	*word &= (l << 1) | (~RLW_LARGEST_RUNNING_COUNT_SHIFT);
}

static inline eword_t rlw_get_running_len(const eword_t *word)
{
	return (*word >> 1) & RLW_LARGEST_RUNNING_COUNT;
}

static inline eword_t rlw_get_literal_words(const eword_t *word)
{
	return *word >> (1 + RLW_RUNNING_BITS);
}

static inline void rlw_set_literal_words(eword_t *word, eword_t l)
{
	*word |= ~RLW_RUNNING_LEN_PLUS_BIT;
	*word &= (l << (RLW_RUNNING_BITS + 1)) | RLW_RUNNING_LEN_PLUS_BIT;
}

static inline eword_t rlw_size(const eword_t *self)
{
	return rlw_get_running_len(self) + rlw_get_literal_words(self);
}

struct rlw_iterator {
	const eword_t *buffer;
	size_t size;
	size_t pointer;
	size_t literal_word_start;

	struct {
		const eword_t *word;
		int literal_words;
		int running_len;
		int literal_word_offset;
		int running_bit;
	} rlw;
};

void rlwit_init(struct rlw_iterator *it, struct ewah_bitmap *bitmap);
void rlwit_discard_first_words(struct rlw_iterator *it, size_t x);
size_t rlwit_discharge(
	struct rlw_iterator *it, struct ewah_bitmap *out, size_t max, int negate);

static inline size_t rlwit_word_size(struct rlw_iterator *it)
{
	return it->rlw.running_len + it->rlw.literal_words;
}

static inline size_t rlwit_literal_words(struct rlw_iterator *it)
{
	return it->pointer - it->rlw.literal_words;
}

#endif
