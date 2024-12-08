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

#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "ewok.h"
#include "strbuf.h"

int ewah_serialize_to(struct ewah_bitmap *self,
		      int (*write_fun)(void *, const void *, size_t),
		      void *data)
{
	size_t i;
	eword_t dump[2048];
	const size_t words_per_dump = sizeof(dump) / sizeof(eword_t);
	uint32_t bitsize, word_count, rlw_pos;

	const eword_t *buffer;
	size_t words_left;

	/* 32 bit -- bit size for the map */
	bitsize =  htonl((uint32_t)self->bit_size);
	if (write_fun(data, &bitsize, 4) != 4)
		return -1;

	/** 32 bit -- number of compressed 64-bit words */
	word_count =  htonl((uint32_t)self->buffer_size);
	if (write_fun(data, &word_count, 4) != 4)
		return -1;

	/** 64 bit x N -- compressed words */
	buffer = self->buffer;
	words_left = self->buffer_size;

	while (words_left >= words_per_dump) {
		for (i = 0; i < words_per_dump; ++i, ++buffer)
			dump[i] = htonll(*buffer);

		if (write_fun(data, dump, sizeof(dump)) != sizeof(dump))
			return -1;

		words_left -= words_per_dump;
	}

	if (words_left) {
		for (i = 0; i < words_left; ++i, ++buffer)
			dump[i] = htonll(*buffer);

		if (write_fun(data, dump, words_left * 8) != words_left * 8)
			return -1;
	}

	/** 32 bit -- position for the RLW */
	rlw_pos = (uint8_t*)self->rlw - (uint8_t *)self->buffer;
	rlw_pos = htonl(rlw_pos / sizeof(eword_t));

	if (write_fun(data, &rlw_pos, 4) != 4)
		return -1;

	return (3 * 4) + (self->buffer_size * 8);
}

static int write_strbuf(void *user_data, const void *data, size_t len)
{
	struct strbuf *sb = user_data;
	strbuf_add(sb, data, len);
	return len;
}

int ewah_serialize_strbuf(struct ewah_bitmap *self, struct strbuf *sb)
{
	return ewah_serialize_to(self, write_strbuf, sb);
}

ssize_t ewah_read_mmap(struct ewah_bitmap *self, const void *map, size_t len)
{
	const uint8_t *ptr = map;
	size_t data_len;
	size_t i;

	if (len < sizeof(uint32_t))
		return error("corrupt ewah bitmap: eof before bit size");
	self->bit_size = get_be32(ptr);
	ptr += sizeof(uint32_t);
	len -= sizeof(uint32_t);

	if (len < sizeof(uint32_t))
		return error("corrupt ewah bitmap: eof before length");
	self->buffer_size = self->alloc_size = get_be32(ptr);
	ptr += sizeof(uint32_t);
	len -= sizeof(uint32_t);

	REALLOC_ARRAY(self->buffer, self->alloc_size);

	/*
	 * Copy the raw data for the bitmap as a whole chunk;
	 * if we're in a little-endian platform, we'll perform
	 * the endianness conversion in a separate pass to ensure
	 * we're loading 8-byte aligned words.
	 */
	data_len = st_mult(self->buffer_size, sizeof(eword_t));
	if (len < data_len)
		return error("corrupt ewah bitmap: eof in data "
			     "(%"PRIuMAX" bytes short)",
			     (uintmax_t)(data_len - len));
	memcpy(self->buffer, ptr, data_len);
	ptr += data_len;
	len -= data_len;

	for (i = 0; i < self->buffer_size; ++i)
		self->buffer[i] = ntohll(self->buffer[i]);

	if (len < sizeof(uint32_t))
		return error("corrupt ewah bitmap: eof before rlw");
	self->rlw = self->buffer + get_be32(ptr);
	ptr += sizeof(uint32_t);
	len -= sizeof(uint32_t);

	return ptr - (const uint8_t *)map;
}
