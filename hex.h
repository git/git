#ifndef HEX_H
#define HEX_H

#include "hash-ll.h"

extern const signed char hexval_table[256];
static inline unsigned int hexval(unsigned char c)
{
	return hexval_table[c];
}

/*
 * Convert two consecutive hexadecimal digits into a char.  Return a
 * negative value on error.  Don't run over the end of short strings.
 */
static inline int hex2chr(const char *s)
{
	unsigned int val = hexval(s[0]);
	return (val & ~0xf) ? val : (val << 4) | hexval(s[1]);
}

/*
 * Try to read a SHA1 in hexadecimal format from the 40 characters
 * starting at hex.  Write the 20-byte result to sha1 in binary form.
 * Return 0 on success.  Reading stops if a NUL is encountered in the
 * input, so it is safe to pass this function an arbitrary
 * null-terminated string.
 */
int get_sha1_hex(const char *hex, unsigned char *sha1);
int get_oid_hex(const char *hex, struct object_id *sha1);

/* Like get_oid_hex, but for an arbitrary hash algorithm. */
int get_oid_hex_algop(const char *hex, struct object_id *oid, const struct git_hash_algo *algop);

/*
 * Read `len` pairs of hexadecimal digits from `hex` and write the
 * values to `binary` as `len` bytes. Return 0 on success, or -1 if
 * the input does not consist of hex digits).
 */
int hex_to_bytes(unsigned char *binary, const char *hex, size_t len);

/*
 * Convert a binary hash in "unsigned char []" or an object name in
 * "struct object_id *" to its hex equivalent. The `_r` variant is reentrant,
 * and writes the NUL-terminated output to the buffer `out`, which must be at
 * least `GIT_MAX_HEXSZ + 1` bytes, and returns a pointer to out for
 * convenience.
 *
 * The non-`_r` variant returns a static buffer, but uses a ring of 4
 * buffers, making it safe to make multiple calls for a single statement, like:
 *
 *   printf("%s -> %s", hash_to_hex(one), hash_to_hex(two));
 *   printf("%s -> %s", oid_to_hex(one), oid_to_hex(two));
 */
char *hash_to_hex_algop_r(char *buffer, const unsigned char *hash, const struct git_hash_algo *);
char *oid_to_hex_r(char *out, const struct object_id *oid);
char *hash_to_hex_algop(const unsigned char *hash, const struct git_hash_algo *);	/* static buffer result! */
char *hash_to_hex(const unsigned char *hash);						/* same static buffer */
char *oid_to_hex(const struct object_id *oid);						/* same static buffer */

/*
 * Parse a 40-character hexadecimal object ID starting from hex, updating the
 * pointer specified by end when parsing stops.  The resulting object ID is
 * stored in oid.  Returns 0 on success.  Parsing will stop on the first NUL or
 * other invalid character.  end is only updated on success; otherwise, it is
 * unmodified.
 */
int parse_oid_hex(const char *hex, struct object_id *oid, const char **end);

/* Like parse_oid_hex, but for an arbitrary hash algorithm. */
int parse_oid_hex_algop(const char *hex, struct object_id *oid, const char **end,
			const struct git_hash_algo *algo);


/*
 * These functions work like get_oid_hex and parse_oid_hex, but they will parse
 * a hex value for any algorithm. The algorithm is detected based on the length
 * and the algorithm in use is returned. If this is not a hex object ID in any
 * algorithm, returns GIT_HASH_UNKNOWN.
 */
int get_oid_hex_any(const char *hex, struct object_id *oid);
int parse_oid_hex_any(const char *hex, struct object_id *oid, const char **end);

#endif
