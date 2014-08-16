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

int ewah_serialize_native(struct ewah_bitmap *self, int fd)
{
	uint32_t write32;
	size_t to_write = self->buffer_size * 8;

	/* 32 bit -- bit size for the map */
	write32 = (uint32_t)self->bit_size;
	if (write(fd, &write32, 4) != 4)
		return -1;

	/** 32 bit -- number of compressed 64-bit words */
	write32 = (uint32_t)self->buffer_size;
	if (write(fd, &write32, 4) != 4)
		return -1;

	if (write(fd, self->buffer, to_write) != to_write)
		return -1;

	/** 32 bit -- position for the RLW */
	write32 = self->rlw - self->buffer;
	if (write(fd, &write32, 4) != 4)
		return -1;

	return (3 * 4) + to_write;
}

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

static int write_helper(void *fd, const void *buf, size_t len)
{
	return write((intptr_t)fd, buf, len);
}

int ewah_serialize(struct ewah_bitmap *self, int fd)
{
	return ewah_serialize_to(self, write_helper, (void *)(intptr_t)fd);
}

int ewah_read_mmap(struct ewah_bitmap *self, const void *map, size_t len)
{
	const uint8_t *ptr = map;
	size_t i;

	self->bit_size = get_be32(ptr);
	ptr += sizeof(uint32_t);

	self->buffer_size = self->alloc_size = get_be32(ptr);
	ptr += sizeof(uint32_t);

	self->buffer = ewah_realloc(self->buffer,
		self->alloc_size * sizeof(eword_t));

	if (!self->buffer)
		return -1;

	/*
	 * Copy the raw data for the bitmap as a whole chunk;
	 * if we're in a little-endian platform, we'll perform
	 * the endianness conversion in a separate pass to ensure
	 * we're loading 8-byte aligned words.
	 */
	memcpy(self->buffer, ptr, self->buffer_size * sizeof(uint64_t));
	ptr += self->buffer_size * sizeof(uint64_t);

	for (i = 0; i < self->buffer_size; ++i)
		self->buffer[i] = ntohll(self->buffer[i]);

	self->rlw = self->buffer + get_be32(ptr);

	return (3 * 4) + (self->buffer_size * 8);
}

int ewah_deserialize(struct ewah_bitmap *self, int fd)
{
	size_t i;
	eword_t dump[2048];
	const size_t words_per_dump = sizeof(dump) / sizeof(eword_t);
	uint32_t bitsize, word_count, rlw_pos;

	eword_t *buffer = NULL;
	size_t words_left;

	ewah_clear(self);

	/* 32 bit -- bit size for the map */
	if (read(fd, &bitsize, 4) != 4)
		return -1;

	self->bit_size = (size_t)ntohl(bitsize);

	/** 32 bit -- number of compressed 64-bit words */
	if (read(fd, &word_count, 4) != 4)
		return -1;

	self->buffer_size = self->alloc_size = (size_t)ntohl(word_count);
	self->buffer = ewah_realloc(self->buffer,
		self->alloc_size * sizeof(eword_t));

	if (!self->buffer)
		return -1;

	/** 64 bit x N -- compressed words */
	buffer = self->buffer;
	words_left = self->buffer_size;

	while (words_left >= words_per_dump) {
		if (read(fd, dump, sizeof(dump)) != sizeof(dump))
			return -1;

		for (i = 0; i < words_per_dump; ++i, ++buffer)
			*buffer = ntohll(dump[i]);

		words_left -= words_per_dump;
	}

	if (words_left) {
		if (read(fd, dump, words_left * 8) != words_left * 8)
			return -1;

		for (i = 0; i < words_left; ++i, ++buffer)
			*buffer = ntohll(dump[i]);
	}

	/** 32 bit -- position for the RLW */
	if (read(fd, &rlw_pos, 4) != 4)
		return -1;

	self->rlw = self->buffer + ntohl(rlw_pos);
	return 0;
}
