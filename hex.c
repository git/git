#include "cache.h"

const signed char hexval_table[256] = {
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 00-07 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 08-0f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 10-17 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 18-1f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 20-27 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 28-2f */
	  0,  1,  2,  3,  4,  5,  6,  7,		/* 30-37 */
	  8,  9, -1, -1, -1, -1, -1, -1,		/* 38-3f */
	 -1, 10, 11, 12, 13, 14, 15, -1,		/* 40-47 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 48-4f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 50-57 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 58-5f */
	 -1, 10, 11, 12, 13, 14, 15, -1,		/* 60-67 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 68-67 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 70-77 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 78-7f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 80-87 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 88-8f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 90-97 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 98-9f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* a0-a7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* a8-af */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* b0-b7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* b8-bf */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* c0-c7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* c8-cf */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* d0-d7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* d8-df */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* e0-e7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* e8-ef */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* f0-f7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* f8-ff */
};

int get_sha1_hex(const char *hex, unsigned char *sha1)
{
	int i;
	for (i = 0; i < GIT_SHA1_RAWSZ; i++) {
		int val = hex2chr(hex);
		if (val < 0)
			return -1;
		*sha1++ = val;
		hex += 2;
	}
	return 0;
}

int get_oid_hex(const char *hex, struct object_id *oid)
{
	return get_sha1_hex(hex, oid->hash);
}

int parse_oid_hex(const char *hex, struct object_id *oid, const char **end)
{
	int ret = get_oid_hex(hex, oid);
	if (!ret)
		*end = hex + GIT_SHA1_HEXSZ;
	return ret;
}

char *sha1_to_hex_r(char *buffer, const unsigned char *sha1)
{
	static const char hex[] = "0123456789abcdef";
	char *buf = buffer;
	int i;

	for (i = 0; i < GIT_SHA1_RAWSZ; i++) {
		unsigned int val = *sha1++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	*buf = '\0';

	return buffer;
}

char *oid_to_hex_r(char *buffer, const struct object_id *oid)
{
	return sha1_to_hex_r(buffer, oid->hash);
}

char *sha1_to_hex(const unsigned char *sha1)
{
	static int bufno;
	static char hexbuffer[4][GIT_MAX_HEXSZ + 1];
	bufno = (bufno + 1) % ARRAY_SIZE(hexbuffer);
	return sha1_to_hex_r(hexbuffer[bufno], sha1);
}

char *oid_to_hex(const struct object_id *oid)
{
	return sha1_to_hex(oid->hash);
}
