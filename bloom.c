#include "git-compat-util.h"
#include "bloom.h"

static uint32_t rotate_left(uint32_t value, int32_t count)
{
	uint32_t mask = 8 * sizeof(uint32_t) - 1;
	count &= mask;
	return ((value << count) | (value >> ((-count) & mask)));
}

/*
 * Calculate the murmur3 32-bit hash value for the given data
 * using the given seed.
 * Produces a uniformly distributed hash value.
 * Not considered to be cryptographically secure.
 * Implemented as described in https://en.wikipedia.org/wiki/MurmurHash#Algorithm
 */
uint32_t murmur3_seeded(uint32_t seed, const char *data, size_t len)
{
	const uint32_t c1 = 0xcc9e2d51;
	const uint32_t c2 = 0x1b873593;
	const uint32_t r1 = 15;
	const uint32_t r2 = 13;
	const uint32_t m = 5;
	const uint32_t n = 0xe6546b64;
	int i;
	uint32_t k1 = 0;
	const char *tail;

	int len4 = len / sizeof(uint32_t);

	uint32_t k;
	for (i = 0; i < len4; i++) {
		uint32_t byte1 = (uint32_t)data[4*i];
		uint32_t byte2 = ((uint32_t)data[4*i + 1]) << 8;
		uint32_t byte3 = ((uint32_t)data[4*i + 2]) << 16;
		uint32_t byte4 = ((uint32_t)data[4*i + 3]) << 24;
		k = byte1 | byte2 | byte3 | byte4;
		k *= c1;
		k = rotate_left(k, r1);
		k *= c2;

		seed ^= k;
		seed = rotate_left(seed, r2) * m + n;
	}

	tail = (data + len4 * sizeof(uint32_t));

	switch (len & (sizeof(uint32_t) - 1)) {
	case 3:
		k1 ^= ((uint32_t)tail[2]) << 16;
		/*-fallthrough*/
	case 2:
		k1 ^= ((uint32_t)tail[1]) << 8;
		/*-fallthrough*/
	case 1:
		k1 ^= ((uint32_t)tail[0]) << 0;
		k1 *= c1;
		k1 = rotate_left(k1, r1);
		k1 *= c2;
		seed ^= k1;
		break;
	}

	seed ^= (uint32_t)len;
	seed ^= (seed >> 16);
	seed *= 0x85ebca6b;
	seed ^= (seed >> 13);
	seed *= 0xc2b2ae35;
	seed ^= (seed >> 16);

	return seed;
}